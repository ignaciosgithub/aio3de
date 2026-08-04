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

namespace GameSettings
{
    //! Singleton access to the persistent per-user settings store.
    //! Values (floats) and bindings (input channel names keyed by arbitrary
    //! binding keys) load from and save to `@user@/gamesettings.json`.
    class GameSettingsRequests
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Named float setting (e.g. "fov", "mouse_sensitivity"); returns defaultValue if unset.
        virtual float GetValue(const AZStd::string& name, float defaultValue) = 0;
        //! Sets a named float setting and raises OnSettingChanged.
        virtual void SetValue(const AZStd::string& name, float value) = 0;

        //! Input-channel override for a binding key; returns defaultChannel if unset.
        virtual AZStd::string GetBinding(const AZStd::string& bindingKey, const AZStd::string& defaultChannel) = 0;
        //! Stores an input-channel override and raises OnBindingChanged.
        virtual void SetBinding(const AZStd::string& bindingKey, const AZStd::string& channelName) = 0;
        //! Captures the next pressed key/button/trigger as the binding for
        //! bindingKey, then raises OnBindingChanged. Cancel with an empty key.
        virtual void StartRebind(const AZStd::string& bindingKey) = 0;
        //! True while a StartRebind capture is waiting for input.
        virtual bool IsRebinding() = 0;

        //! Writes all settings to `@user@/gamesettings.json` (also called on deactivate).
        virtual void Save() = 0;
    };
    using GameSettingsRequestBus = AZ::EBus<GameSettingsRequests>;

    class GameSettingsNotifications
        : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        virtual void OnSettingChanged([[maybe_unused]] const AZStd::string& name, [[maybe_unused]] float value) {}
        virtual void OnBindingChanged([[maybe_unused]] const AZStd::string& bindingKey, [[maybe_unused]] const AZStd::string& channelName) {}
    };
    using GameSettingsNotificationBus = AZ::EBus<GameSettingsNotifications>;
} // namespace GameSettings
