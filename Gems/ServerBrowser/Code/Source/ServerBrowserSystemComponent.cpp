/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "ServerBrowserSystemComponent.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Console/ILogger.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/JSON/document.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Socket/AzSocket.h>
#include <AzCore/std/string/conversions.h>

namespace ServerBrowser
{
    AZ_CVAR(AZ::CVarFixedString, sb_master_url, "", nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Master server base URL, plain http only (e.g. http://master.example.com:27900)");
    AZ_CVAR(bool, sb_announce, false, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Announce this dedicated server to the master server (heartbeat every sb_announce_interval seconds)");
    AZ_CVAR(float, sb_announce_interval, 30.0f, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Seconds between master-server announce heartbeats");
    AZ_CVAR(AZ::CVarFixedString, sb_server_name, "aio3de server", nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Server name shown in the browser list");
    AZ_CVAR(uint16_t, sb_game_port, 33450, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Game UDP port announced to the master server");
    AZ_CVAR(AZ::CVarFixedString, sb_map, "", nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Current map name announced to the master server");
    AZ_CVAR(uint32_t, sb_players, 0, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Current player count announced to the master server");
    AZ_CVAR(uint32_t, sb_max_players, 16, nullptr, AZ::ConsoleFunctorFlags::DontReplicate,
        "Player capacity announced to the master server");

    namespace
    {
        //! Splits "http://host:port/path" into parts; only plain http is supported.
        bool ParseUrl(const AZStd::string& url, AZStd::string& host, AZ::u16& port, AZStd::string& basePath)
        {
            constexpr AZStd::string_view prefix = "http://";
            if (!AZStd::string_view(url).starts_with(prefix))
            {
                return false;
            }
            AZStd::string rest = url.substr(prefix.size());
            const size_t slash = rest.find('/');
            basePath = slash == AZStd::string::npos ? "" : rest.substr(slash);
            AZStd::string hostPort = slash == AZStd::string::npos ? rest : rest.substr(0, slash);
            const size_t colon = hostPort.find(':');
            port = 80;
            if (colon != AZStd::string::npos)
            {
                port = aznumeric_cast<AZ::u16>(AZStd::stoi(AZStd::string(hostPort.substr(colon + 1))));
                host = hostPort.substr(0, colon);
            }
            else
            {
                host = hostPort;
            }
            return !host.empty();
        }

        //! Minimal blocking HTTP/1.1 request; returns the response body or empty on failure.
        bool HttpRequest(
            const AZStd::string& method, const AZStd::string& url, const AZStd::string& body, AZStd::string& responseBody)
        {
            AZStd::string host;
            AZStd::string basePath;
            AZ::u16 port = 80;
            if (!ParseUrl(url, host, port, basePath))
            {
                return false;
            }
            if (basePath.empty())
            {
                basePath = "/";
            }

            AZ::AzSock::Startup();
            AZSOCKET sock = AZ::AzSock::Socket();
            if (!AZ::AzSock::IsAzSocketValid(sock))
            {
                return false;
            }
            AZ::AzSock::AzSocketAddress address;
            if (!address.SetAddress(host, port) || AZ::AzSock::SocketErrorOccured(AZ::AzSock::Connect(sock, address)))
            {
                AZ::AzSock::CloseSocket(sock);
                return false;
            }

            const AZStd::string request = AZStd::string::format(
                "%s %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                method.c_str(), basePath.c_str(), host.c_str(), body.size(), body.c_str());
            if (AZ::AzSock::SocketErrorOccured(
                    AZ::AzSock::Send(sock, request.c_str(), aznumeric_cast<AZ::s32>(request.size()), 0)))
            {
                AZ::AzSock::CloseSocket(sock);
                return false;
            }

            AZStd::string response;
            char buffer[4096];
            for (;;)
            {
                const AZ::s32 received = AZ::AzSock::Recv(sock, buffer, sizeof(buffer), 0);
                if (received <= 0)
                {
                    break;
                }
                response.append(buffer, received);
                if (response.size() > 1024 * 1024)
                {
                    break; // sanity cap
                }
            }
            AZ::AzSock::CloseSocket(sock);

            const size_t headerEnd = response.find("\r\n\r\n");
            if (headerEnd == AZStd::string::npos || !AZStd::string_view(response).starts_with("HTTP/1.1 200"))
            {
                return false;
            }
            responseBody = response.substr(headerEnd + 4);
            return true;
        }

        struct ServerEntry
        {
            AZStd::string m_name;
            AZStd::string m_address;
            AZ::u32 m_port = 0;
            AZStd::string m_map;
            AZ::u32 m_players = 0;
            AZ::u32 m_maxPlayers = 0;
        };
    } // namespace

    //! Script (Lua/Script Canvas) handler for ServerBrowserNotificationBus.
    class BehaviorServerBrowserNotificationBusHandler
        : public ServerBrowserNotificationBus::Handler
        , public AZ::BehaviorEBusHandler
    {
    public:
        AZ_EBUS_BEHAVIOR_BINDER(BehaviorServerBrowserNotificationBusHandler, "{8C2D91E5-4F60-4B3A-A7D8-1E95C3B0F642}", AZ::SystemAllocator,
            OnServerListEntry, OnServerListRefreshed, OnServerListError);

        void OnServerListEntry(
            const AZStd::string& name, const AZStd::string& address, AZ::u32 port,
            const AZStd::string& map, AZ::u32 players, AZ::u32 maxPlayers) override
        {
            Call(FN_OnServerListEntry, name, address, port, map, players, maxPlayers);
        }

        void OnServerListRefreshed(AZ::u32 count) override
        {
            Call(FN_OnServerListRefreshed, count);
        }

        void OnServerListError(const AZStd::string& error) override
        {
            Call(FN_OnServerListError, error);
        }
    };

    void ServerBrowserSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ServerBrowserSystemComponent, AZ::Component>()->Version(1);
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ServerBrowserRequestBus>("ServerBrowserRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Multiplayer")
                ->Event("RefreshServerList", &ServerBrowserRequestBus::Events::RefreshServerList)
                ->Event("JoinServer", &ServerBrowserRequestBus::Events::JoinServer);

            behaviorContext->EBus<ServerBrowserNotificationBus>("ServerBrowserNotificationBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Category, "Multiplayer")
                ->Handler<BehaviorServerBrowserNotificationBusHandler>();
        }
    }

    void ServerBrowserSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("ServerBrowserService"));
    }

    void ServerBrowserSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("ServerBrowserService"));
    }

    void ServerBrowserSystemComponent::Activate()
    {
        ServerBrowserRequestBus::Handler::BusConnect();
        AZ::TickBus::Handler::BusConnect();
    }

    void ServerBrowserSystemComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        ServerBrowserRequestBus::Handler::BusDisconnect();
        if (m_worker.joinable())
        {
            m_worker.join();
        }
    }

    void ServerBrowserSystemComponent::RefreshServerList()
    {
        if (m_busy.exchange(true))
        {
            return; // a refresh/announce is already in flight
        }
        if (m_worker.joinable())
        {
            m_worker.join();
        }
        const AZStd::string url = AZStd::string(static_cast<AZ::CVarFixedString>(sb_master_url).c_str()) + "/servers";
        m_worker = AZStd::thread(
            [this, url]()
            {
                AZStd::string body;
                const bool ok = HttpRequest("GET", url, "", body);

                AZStd::vector<ServerEntry> entries;
                bool parsed = false;
                if (ok)
                {
                    rapidjson::Document doc;
                    doc.Parse(body.c_str());
                    if (!doc.HasParseError() && doc.IsArray())
                    {
                        parsed = true;
                        for (const auto& item : doc.GetArray())
                        {
                            if (!item.IsObject())
                            {
                                continue;
                            }
                            ServerEntry entry;
                            if (auto it = item.FindMember("name"); it != item.MemberEnd() && it->value.IsString())
                            {
                                entry.m_name = it->value.GetString();
                            }
                            if (auto it = item.FindMember("address"); it != item.MemberEnd() && it->value.IsString())
                            {
                                entry.m_address = it->value.GetString();
                            }
                            if (auto it = item.FindMember("port"); it != item.MemberEnd() && it->value.IsUint())
                            {
                                entry.m_port = it->value.GetUint();
                            }
                            if (auto it = item.FindMember("map"); it != item.MemberEnd() && it->value.IsString())
                            {
                                entry.m_map = it->value.GetString();
                            }
                            if (auto it = item.FindMember("players"); it != item.MemberEnd() && it->value.IsUint())
                            {
                                entry.m_players = it->value.GetUint();
                            }
                            if (auto it = item.FindMember("max_players"); it != item.MemberEnd() && it->value.IsUint())
                            {
                                entry.m_maxPlayers = it->value.GetUint();
                            }
                            if (!entry.m_address.empty() && entry.m_port != 0)
                            {
                                entries.push_back(AZStd::move(entry));
                            }
                        }
                    }
                }

                // deliver on the main thread
                AZ::TickBus::QueueFunction(
                    [this, parsed, entries = AZStd::move(entries)]()
                    {
                        if (!parsed)
                        {
                            ServerBrowserNotificationBus::Broadcast(
                                &ServerBrowserNotificationBus::Events::OnServerListError,
                                AZStd::string("master server unreachable or returned an invalid response"));
                        }
                        else
                        {
                            for (const ServerEntry& entry : entries)
                            {
                                ServerBrowserNotificationBus::Broadcast(
                                    &ServerBrowserNotificationBus::Events::OnServerListEntry,
                                    entry.m_name, entry.m_address, entry.m_port, entry.m_map,
                                    entry.m_players, entry.m_maxPlayers);
                            }
                            ServerBrowserNotificationBus::Broadcast(
                                &ServerBrowserNotificationBus::Events::OnServerListRefreshed,
                                aznumeric_cast<AZ::u32>(entries.size()));
                        }
                        m_busy = false;
                    });
            });
    }

    void ServerBrowserSystemComponent::JoinServer(const AZStd::string& address, AZ::u32 port)
    {
        if (auto* console = AZ::Interface<AZ::IConsole>::Get())
        {
            const AZStd::string command = AZStd::string::format("connect %s:%u", address.c_str(), port);
            console->PerformCommand(command.c_str());
        }
    }

    void ServerBrowserSystemComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (!static_cast<bool>(sb_announce))
        {
            return;
        }
        m_announceTimer -= deltaTime;
        if (m_announceTimer <= 0.0f)
        {
            m_announceTimer = AZStd::max(static_cast<float>(sb_announce_interval), 5.0f);
            Announce();
        }
    }

    void ServerBrowserSystemComponent::Announce()
    {
        if (m_busy.exchange(true))
        {
            return;
        }
        if (m_worker.joinable())
        {
            m_worker.join();
        }
        const AZStd::string url = AZStd::string(static_cast<AZ::CVarFixedString>(sb_master_url).c_str()) + "/announce";
        const AZ::CVarFixedString name = sb_server_name;
        const AZ::CVarFixedString map = sb_map;
        const AZStd::string body = AZStd::string::format(
            "{\"name\":\"%s\",\"port\":%u,\"map\":\"%s\",\"players\":%u,\"max_players\":%u}",
            name.c_str(), static_cast<AZ::u32>(static_cast<uint16_t>(sb_game_port)), map.c_str(),
            static_cast<AZ::u32>(static_cast<uint32_t>(sb_players)),
            static_cast<AZ::u32>(static_cast<uint32_t>(sb_max_players)));
        m_worker = AZStd::thread(
            [this, url, body]()
            {
                AZStd::string response;
                if (!HttpRequest("POST", url, body, response))
                {
                    AZLOG_WARN("ServerBrowser: master-server announce failed (%s)", url.c_str());
                }
                m_busy = false;
            });
    }
} // namespace ServerBrowser
