/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include "ArenaAttestDataset.h"

#include <AzCore/std/parallel/mutex.h>

#include <cstring>

namespace ArenaShooterNet::ArenaAttestDataset
{
    namespace
    {
        // Baked gameplay constants both binaries compile in. A client build
        // with patched values (or a hex-edited binary section holding them)
        // produces different attestation digests. Extend via AppendRegion.
        constexpr AZ::u64 BakedConstants[] = {
            0x41494f3344452121ull, // dataset magic
            1ull,                  // dataset version
            // gameplay invariants mirrored from the arena kit (movement,
            // weapon cadence, health) - keep in sync with the Lua bindings
            100ull,                // base health
            25ull,                 // base damage
            600ull,                // rifle rounds per minute
            7ull,                  // move speed m/s (x100 = 700)
            420ull,                // jump impulse (x100)
        };

        AZStd::mutex s_mutex;

        AZStd::vector<AZ::u8>& Storage()
        {
            static AZStd::vector<AZ::u8> s_dataset = []
            {
                AZStd::vector<AZ::u8> initial(sizeof(BakedConstants));
                memcpy(initial.data(), BakedConstants, sizeof(BakedConstants));
                return initial;
            }();
            return s_dataset;
        }
    } // namespace

    void AppendRegion(AZStd::string_view name, const void* data, size_t size)
    {
        AZStd::lock_guard<AZStd::mutex> lock(s_mutex);
        auto& dataset = Storage();
        dataset.insert(dataset.end(), name.begin(), name.end());
        const auto* bytes = static_cast<const AZ::u8*>(data);
        dataset.insert(dataset.end(), bytes, bytes + size);
    }

    const AZStd::vector<AZ::u8>& Get()
    {
        AZStd::lock_guard<AZStd::mutex> lock(s_mutex);
        return Storage();
    }
} // namespace ArenaShooterNet::ArenaAttestDataset
