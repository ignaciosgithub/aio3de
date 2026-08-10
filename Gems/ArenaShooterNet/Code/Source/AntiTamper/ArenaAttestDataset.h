/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string_view.h>

namespace ArenaShooterNet
{
    //! Shared attestation dataset: the reference bytes that randomized-program
    //! attestation challenges read from. Server and client must build an
    //! identical dataset (both run the same registration code paths), so a
    //! client whose registered content differs - edited gameplay scripts,
    //! patched constants - produces mismatching attestation digests.
    //!
    //! The default dataset is a baked table of the gem's gameplay constants.
    //! Games can append more content (e.g. the bytes of critical Lua scripts)
    //! via AppendRegion before the first challenge is answered; regions must
    //! be appended in the same order on server and client.
    namespace ArenaAttestDataset
    {
        //! Appends named content to the dataset (order-sensitive; call
        //! identically on server and client during startup).
        void AppendRegion(AZStd::string_view name, const void* data, size_t size);

        //! The current dataset bytes (default constants + appended regions).
        const AZStd::vector<AZ::u8>& Get();
    } // namespace ArenaAttestDataset
} // namespace ArenaShooterNet
