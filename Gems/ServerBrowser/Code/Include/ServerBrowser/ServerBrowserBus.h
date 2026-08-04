/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/std/string/string.h>

namespace ServerBrowser
{
    //! Client-side access to the master-server list.
    class ServerBrowserRequests
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Fetches the server list from sb_master_url asynchronously; entries
        //! arrive as OnServerListEntry notifications followed by
        //! OnServerListRefreshed (or OnServerListError).
        virtual void RefreshServerList() = 0;

        //! Connects the multiplayer client to address:port (console `connect`).
        virtual void JoinServer(const AZStd::string& address, AZ::u32 port) = 0;
    };
    using ServerBrowserRequestBus = AZ::EBus<ServerBrowserRequests>;

    class ServerBrowserNotifications
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! One server from the refreshed list.
        virtual void OnServerListEntry(
            [[maybe_unused]] const AZStd::string& name,
            [[maybe_unused]] const AZStd::string& address,
            [[maybe_unused]] AZ::u32 port,
            [[maybe_unused]] const AZStd::string& map,
            [[maybe_unused]] AZ::u32 players,
            [[maybe_unused]] AZ::u32 maxPlayers) {}

        //! The refresh finished; count entries were delivered.
        virtual void OnServerListRefreshed([[maybe_unused]] AZ::u32 count) {}

        //! The refresh failed (unreachable master, bad response...).
        virtual void OnServerListError([[maybe_unused]] const AZStd::string& error) {}
    };
    using ServerBrowserNotificationBus = AZ::EBus<ServerBrowserNotifications>;
} // namespace ServerBrowser
