/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "ServerAdminSystemComponent.h"

#include <AzCore/Console/IConsole.h>
#include <AzCore/Console/ILogger.h>
#include <AzCore/Debug/TraceMessageBus.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Socket/AzSocket.h>
#include <AzCore/std/chrono/chrono.h>

#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace ServerAdmin
{
    AZ_CVAR(bool, admin_enable, false, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Enable the rcon-style remote admin TCP channel (requires admin_password)");
    AZ_CVAR(AZ::CVarFixedString, admin_password, "", nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Shared secret for remote admin authentication (HMAC challenge-response; never sent over the wire)");
    AZ_CVAR(uint16_t, admin_port, 33470, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "TCP port the remote admin channel listens on");

    namespace
    {
        constexpr const char* EndMarker = "<<<END>>>\n";
        constexpr size_t NonceBytes = 32;

        AZStd::string ToHex(const uint8_t* data, size_t size)
        {
            static constexpr char Digits[] = "0123456789abcdef";
            AZStd::string hex;
            hex.reserve(size * 2);
            for (size_t i = 0; i < size; ++i)
            {
                hex.push_back(Digits[data[i] >> 4]);
                hex.push_back(Digits[data[i] & 0x0F]);
            }
            return hex;
        }

        bool ConstantTimeEquals(const AZStd::string& lhs, const AZStd::string& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            unsigned char diff = 0;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                diff |= static_cast<unsigned char>(lhs[i]) ^ static_cast<unsigned char>(rhs[i]);
            }
            return diff == 0;
        }

        AZStd::string HmacHex(const AZStd::string& key, const AZStd::string& message)
        {
            uint8_t mac[SHA256_DIGEST_LENGTH] = {};
            unsigned int macLen = 0;
            HMAC(EVP_sha256(),
                key.data(), aznumeric_cast<int>(key.size()),
                reinterpret_cast<const unsigned char*>(message.data()), message.size(),
                mac, &macLen);
            return ToHex(mac, macLen);
        }

        bool SendAll(AZSOCKET sock, const char* data, size_t size)
        {
            size_t sent = 0;
            while (sent < size)
            {
                const AZ::s32 result =
                    AZ::AzSock::Send(sock, data + sent, aznumeric_cast<AZ::s32>(size - sent), 0);
                if (result <= 0)
                {
                    return false;
                }
                sent += aznumeric_cast<size_t>(result);
            }
            return true;
        }

        //! Reads one '\n'-terminated line (CR stripped); false on disconnect.
        bool RecvLine(AZSOCKET sock, AZStd::string& line)
        {
            line.clear();
            char ch = 0;
            while (true)
            {
                const AZ::s32 result = AZ::AzSock::Recv(sock, &ch, 1, 0);
                if (result <= 0)
                {
                    return false;
                }
                if (ch == '\n')
                {
                    return true;
                }
                if (ch != '\r')
                {
                    line.push_back(ch);
                    if (line.size() > 4096)
                    {
                        return false; // oversized line: treat as protocol abuse
                    }
                }
            }
        }

        //! Collects console/log output while an admin command executes.
        class OutputCapture
            : public AZ::Debug::TraceMessageBus::Handler
        {
        public:
            explicit OutputCapture(AZStd::string& sink)
                : m_sink(sink)
            {
                BusConnect();
            }

            ~OutputCapture() override
            {
                BusDisconnect();
            }

            bool OnOutput([[maybe_unused]] const char* window, const char* message) override
            {
                m_sink += message;
                if (!m_sink.empty() && m_sink.back() != '\n')
                {
                    m_sink.push_back('\n');
                }
                return false;
            }

        private:
            AZStd::string& m_sink;
        };
    } // namespace

    void ServerAdminSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ServerAdminSystemComponent, AZ::Component>()
                ->Version(1);
        }
    }

    void ServerAdminSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("ServerAdminService"));
    }

    void ServerAdminSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("ServerAdminService"));
    }

    void ServerAdminSystemComponent::Activate()
    {
        AZ::TickBus::Handler::BusConnect();
    }

    void ServerAdminSystemComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        StopListener();
    }

    void ServerAdminSystemComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        // cvars arrive via cfg files after activation, so (re)check periodically
        if (!m_running)
        {
            m_retryTimer -= deltaTime;
            if (m_retryTimer <= 0.0f)
            {
                m_retryTimer = 2.0f;
                StartListenerIfConfigured();
            }
        }

        // execute at most one queued admin command per frame, on the main thread
        AZStd::string command;
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_commandMutex);
            if (!m_commandReady)
            {
                return;
            }
            command = m_pendingCommand;
            m_commandReady = false;
        }

        AZStd::string output;
        {
            OutputCapture capture(output);
            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                const AZ::PerformCommandResult result = console->PerformCommand(command.c_str());
                if (!result)
                {
                    output += "error: " + result.GetError() + "\n";
                }
            }
            else
            {
                output += "error: console unavailable\n";
            }
        }

        {
            AZStd::lock_guard<AZStd::mutex> lock(m_commandMutex);
            m_commandOutput = AZStd::move(output);
            m_commandFinished = true;
        }
        m_commandDone.notify_all();
    }

    void ServerAdminSystemComponent::StartListenerIfConfigured()
    {
        const AZ::CVarFixedString password = admin_password;
        if (!admin_enable || password.empty())
        {
            return;
        }

        AZ::AzSock::Startup();
        AZSOCKET listenSocket = AZ::AzSock::Socket();
        if (!AZ::AzSock::IsAzSocketValid(listenSocket))
        {
            AZLOG_ERROR("ServerAdmin: could not create the listen socket");
            return;
        }

        AZ::AzSock::AzSocketAddress address;
        address.SetAddress(0, admin_port); // INADDR_ANY
        if (AZ::AzSock::SocketErrorOccured(AZ::AzSock::Bind(listenSocket, address)) ||
            AZ::AzSock::SocketErrorOccured(AZ::AzSock::Listen(listenSocket, 1)))
        {
            AZLOG_ERROR("ServerAdmin: could not bind/listen on port %u", uint32_t(uint16_t(admin_port)));
            AZ::AzSock::CloseSocket(listenSocket);
            return;
        }

        m_listenSocket = listenSocket;
        m_running = true;
        m_listenerThread = AZStd::thread(
            AZStd::thread_desc{ "ServerAdmin listener" },
            [this]()
            {
                ListenerThread();
            });
        AZLOG_INFO("ServerAdmin: remote admin listening on TCP port %u", uint32_t(uint16_t(admin_port)));
    }

    void ServerAdminSystemComponent::StopListener()
    {
        if (!m_running)
        {
            return;
        }
        m_running = false;
        if (m_listenSocket >= 0)
        {
            AZ::AzSock::CloseSocket(aznumeric_cast<AZSOCKET>(m_listenSocket));
            m_listenSocket = -1;
        }
        m_commandDone.notify_all();
        if (m_listenerThread.joinable())
        {
            m_listenerThread.join();
        }
    }

    void ServerAdminSystemComponent::ListenerThread()
    {
        while (m_running)
        {
            AZ::AzSock::AzSocketAddress clientAddress;
            const AZSOCKET client =
                AZ::AzSock::Accept(aznumeric_cast<AZSOCKET>(m_listenSocket), clientAddress);
            if (!AZ::AzSock::IsAzSocketValid(client))
            {
                continue; // listen socket closed on shutdown, or transient error
            }
            AZLOG_INFO("ServerAdmin: admin connection from %s", clientAddress.GetAddress().c_str());
            ServeClient(client);
            AZ::AzSock::CloseSocket(client);
        }
    }

    void ServerAdminSystemComponent::ServeClient(AZ::s64 clientSocket)
    {
        const AZSOCKET sock = aznumeric_cast<AZSOCKET>(clientSocket);

        // challenge-response auth: the password itself never crosses the wire
        uint8_t nonce[NonceBytes] = {};
        if (RAND_bytes(nonce, sizeof(nonce)) != 1)
        {
            return;
        }
        const AZStd::string nonceHex = ToHex(nonce, sizeof(nonce));
        const AZStd::string challenge = AZStd::string("CHALLENGE ") + nonceHex + "\n";
        if (!SendAll(sock, challenge.c_str(), challenge.size()))
        {
            return;
        }

        AZStd::string authLine;
        if (!RecvLine(sock, authLine) || authLine.rfind("AUTH ", 0) != 0)
        {
            return;
        }
        const AZStd::string presented = authLine.substr(5);
        const AZ::CVarFixedString password = admin_password;
        const AZStd::string expected = HmacHex(AZStd::string(password.c_str()), nonceHex);
        if (!ConstantTimeEquals(presented, expected))
        {
            AZLOG_WARN("ServerAdmin: failed authentication attempt");
            SendAll(sock, "DENIED\n", 7);
            return;
        }
        if (!SendAll(sock, "OK\n", 3))
        {
            return;
        }

        AZStd::string commandLine;
        while (m_running && RecvLine(sock, commandLine))
        {
            if (commandLine.empty())
            {
                continue;
            }
            if (commandLine == "quit" || commandLine == "exit")
            {
                break;
            }

            // hand the command to the main thread and wait for its output
            AZStd::string output;
            {
                AZStd::unique_lock<AZStd::mutex> lock(m_commandMutex);
                m_pendingCommand = commandLine;
                m_commandReady = true;
                m_commandFinished = false;
                m_commandDone.wait_for(lock, AZStd::chrono::seconds(10),
                    [this]()
                    {
                        return m_commandFinished || !m_running;
                    });
                if (!m_commandFinished)
                {
                    output = "error: command timed out (server busy or shutting down)\n";
                    m_commandReady = false;
                }
                else
                {
                    output = AZStd::move(m_commandOutput);
                }
            }

            if (!SendAll(sock, output.c_str(), output.size()) ||
                !SendAll(sock, EndMarker, strlen(EndMarker)))
            {
                break;
            }
        }
    }
} // namespace ServerAdmin
