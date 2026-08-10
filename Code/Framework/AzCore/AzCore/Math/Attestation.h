/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/array.h>

namespace AZ
{
    //! RandomX-style randomized-program attestation plus a memory-hard
    //! proof-of-work, for anti-tamper / anti-abuse use in multiplayer games.
    //!
    //! Attestation: from a 64-bit seed, both sides deterministically generate
    //! the same random program (register mixing + data-dependent reads from a
    //! shared dataset) and execute it. Because the program differs per
    //! challenge, responses cannot be precomputed; a client whose dataset
    //! (gameplay constants, scripts, binaries) differs from the server's
    //! reference copy produces a different digest. Detection only - a client
    //! that keeps a pristine copy of the dataset can still answer; combine
    //! with response deadlines and server-authoritative simulation.
    //!
    //! Proof of work: Hashcash-style search over a memory-hard function
    //! (sequential SHA-256 memory fill + data-dependent random walk).
    //! Verification is a single evaluation of the function; the search is
    //! expected 2^difficultyBits evaluations. Use to make throwaway accounts
    //! and reconnect-after-kick loops costly.
    //!
    //! Everything here is self-contained (embedded SHA-256), deterministic
    //! across platforms and endianness-stable for little-endian targets
    //! (all shipped platforms), and has no dependencies beyond AzCore.
    namespace Attestation
    {
        using Digest = AZStd::array<AZ::u64, 4>; //!< 256-bit result

        //! Executes the random program derived from `seed` over `dataset`.
        //! @param seed      challenge seed (server-generated, unpredictable)
        //! @param opCount   number of program operations (>= 64 recommended)
        //! @param dataset   shared reference bytes (must be identical on both
        //!                  sides); may be null/empty, which degrades the
        //!                  attestation to pure computation
        //! @param datasetSize size of dataset in bytes
        //! @return 256-bit digest of the final VM state
        AZCORE_API Digest ExecuteProgram(AZ::u64 seed, AZ::u32 opCount, const AZ::u8* dataset, size_t datasetSize);

        struct PowParams
        {
            AZ::u64 m_seed = 0;          //!< challenge seed (server-generated)
            AZ::u32 m_memoryKib = 1024;  //!< memory-hard buffer size in KiB
            AZ::u32 m_passes = 2;        //!< random-walk passes over the buffer
            AZ::u32 m_difficultyBits = 12; //!< required leading zero bits
        };

        //! Evaluates the memory-hard function once for (params, nonce).
        AZCORE_API Digest EvaluatePow(const PowParams& params, AZ::u64 nonce);

        //! True when `digest` satisfies `difficultyBits` leading zero bits.
        AZCORE_API bool PowDigestMeetsDifficulty(const Digest& digest, AZ::u32 difficultyBits);

        //! Cheap single-evaluation check that `nonce` solves the challenge.
        AZCORE_API bool VerifyPow(const PowParams& params, AZ::u64 nonce);

        //! Searches for a solving nonce (expected 2^difficultyBits evaluations).
        //! @param params      challenge parameters
        //! @param maxAttempts upper bound on evaluations (0 = unbounded)
        //! @param outNonce    receives the solution when found
        //! @return true when a solution was found within maxAttempts
        AZCORE_API bool SolvePow(const PowParams& params, AZ::u64 maxAttempts, AZ::u64& outNonce);
    } // namespace Attestation
} // namespace AZ
