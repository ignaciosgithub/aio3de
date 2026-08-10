/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Math/Attestation.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/UnitTest/TestTypes.h>

namespace UnitTest
{
    using AttestationTests = LeakDetectionFixture;

    static AZStd::vector<AZ::u8> MakeDataset(size_t size, AZ::u8 salt)
    {
        AZStd::vector<AZ::u8> dataset(size);
        for (size_t i = 0; i < size; ++i)
        {
            dataset[i] = AZ::u8((i * 31 + salt) & 0xff);
        }
        return dataset;
    }

    TEST_F(AttestationTests, ExecuteProgram_IsDeterministic)
    {
        const auto dataset = MakeDataset(4096, 7);
        const auto a = AZ::Attestation::ExecuteProgram(0x1234abcd5678ef01ull, 2048, dataset.data(), dataset.size());
        const auto b = AZ::Attestation::ExecuteProgram(0x1234abcd5678ef01ull, 2048, dataset.data(), dataset.size());
        EXPECT_EQ(a, b);
    }

    TEST_F(AttestationTests, ExecuteProgram_DifferentSeedsDiffer)
    {
        const auto dataset = MakeDataset(4096, 7);
        const auto a = AZ::Attestation::ExecuteProgram(1, 2048, dataset.data(), dataset.size());
        const auto b = AZ::Attestation::ExecuteProgram(2, 2048, dataset.data(), dataset.size());
        EXPECT_NE(a, b);
    }

    TEST_F(AttestationTests, ExecuteProgram_TamperedDatasetDetected)
    {
        const auto reference = MakeDataset(4096, 7);
        auto tampered = reference;
        tampered[1234] ^= 0x01; // single-bit "cheat patch"
        const AZ::u64 seed = 0xfeedface12345678ull;
        const auto expected = AZ::Attestation::ExecuteProgram(seed, 4096, reference.data(), reference.size());
        const auto actual = AZ::Attestation::ExecuteProgram(seed, 4096, tampered.data(), tampered.size());
        EXPECT_NE(expected, actual);
    }

    TEST_F(AttestationTests, ExecuteProgram_HandlesEmptyDataset)
    {
        const auto a = AZ::Attestation::ExecuteProgram(42, 256, nullptr, 0);
        const auto b = AZ::Attestation::ExecuteProgram(42, 256, nullptr, 0);
        EXPECT_EQ(a, b);
    }

    TEST_F(AttestationTests, Pow_SolveAndVerifyRoundTrip)
    {
        AZ::Attestation::PowParams params;
        params.m_seed = 0xdeadbeefcafef00dull;
        params.m_memoryKib = 64; // small for test speed
        params.m_passes = 1;
        params.m_difficultyBits = 6; // expected ~64 evaluations
        AZ::u64 nonce = 0;
        ASSERT_TRUE(AZ::Attestation::SolvePow(params, 100000, nonce));
        EXPECT_TRUE(AZ::Attestation::VerifyPow(params, nonce));
        EXPECT_FALSE(AZ::Attestation::VerifyPow(params, nonce + 1) &&
            AZ::Attestation::VerifyPow(params, nonce + 2) &&
            AZ::Attestation::VerifyPow(params, nonce + 3));
    }

    TEST_F(AttestationTests, Pow_DifferentSeedInvalidatesNonce)
    {
        AZ::Attestation::PowParams params;
        params.m_seed = 0x1111111111111111ull;
        params.m_memoryKib = 64;
        params.m_passes = 1;
        params.m_difficultyBits = 6;
        AZ::u64 nonce = 0;
        ASSERT_TRUE(AZ::Attestation::SolvePow(params, 100000, nonce));
        auto other = params;
        other.m_seed = 0x2222222222222222ull;
        // the same nonce is overwhelmingly unlikely to solve a different seed
        AZ::u64 otherNonce = 0;
        ASSERT_TRUE(AZ::Attestation::SolvePow(other, 100000, otherNonce));
        EXPECT_TRUE(AZ::Attestation::VerifyPow(other, otherNonce));
    }

    TEST_F(AttestationTests, Pow_DifficultyCheck)
    {
        AZ::Attestation::Digest zero{};
        EXPECT_TRUE(AZ::Attestation::PowDigestMeetsDifficulty(zero, 256));
        AZ::Attestation::Digest firstBitSet{};
        reinterpret_cast<AZ::u8*>(firstBitSet.data())[0] = 0x80;
        EXPECT_FALSE(AZ::Attestation::PowDigestMeetsDifficulty(firstBitSet, 1));
        EXPECT_TRUE(AZ::Attestation::PowDigestMeetsDifficulty(firstBitSet, 0));
        AZ::Attestation::Digest byteBoundary{};
        reinterpret_cast<AZ::u8*>(byteBoundary.data())[1] = 0xff;
        EXPECT_TRUE(AZ::Attestation::PowDigestMeetsDifficulty(byteBoundary, 8));
        EXPECT_FALSE(AZ::Attestation::PowDigestMeetsDifficulty(byteBoundary, 9));
    }
} // namespace UnitTest
