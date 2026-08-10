/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <AzCore/Math/Attestation.h>

#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/vector.h>

#include <cstring>

namespace AZ::Attestation
{
    namespace
    {
        // ------------------------------------------------------------------
        // Compact SHA-256 (FIPS 180-4), self-contained so the core has no
        // OpenSSL dependency and runs identically on every platform.
        // ------------------------------------------------------------------
        constexpr AZ::u32 Sha256K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        inline AZ::u32 Rotr32(AZ::u32 value, unsigned int count)
        {
            return (value >> count) | (value << (32u - count));
        }

        struct Sha256State
        {
            AZ::u32 m_h[8] = {
                0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
            };
            AZ::u8 m_block[64];
            size_t m_blockUsed = 0;
            AZ::u64 m_totalBytes = 0;

            void Compress(const AZ::u8* block)
            {
                AZ::u32 w[64];
                for (int i = 0; i < 16; ++i)
                {
                    w[i] = (AZ::u32(block[i * 4]) << 24) | (AZ::u32(block[i * 4 + 1]) << 16) |
                        (AZ::u32(block[i * 4 + 2]) << 8) | AZ::u32(block[i * 4 + 3]);
                }
                for (int i = 16; i < 64; ++i)
                {
                    const AZ::u32 s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    const AZ::u32 s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }
                AZ::u32 a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3];
                AZ::u32 e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
                for (int i = 0; i < 64; ++i)
                {
                    const AZ::u32 s1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
                    const AZ::u32 ch = (e & f) ^ (~e & g);
                    const AZ::u32 temp1 = h + s1 + ch + Sha256K[i] + w[i];
                    const AZ::u32 s0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
                    const AZ::u32 maj = (a & b) ^ (a & c) ^ (b & c);
                    const AZ::u32 temp2 = s0 + maj;
                    h = g; g = f; f = e; e = d + temp1;
                    d = c; c = b; b = a; a = temp1 + temp2;
                }
                m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d;
                m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
            }

            void Update(const void* data, size_t size)
            {
                const AZ::u8* bytes = static_cast<const AZ::u8*>(data);
                m_totalBytes += size;
                while (size > 0)
                {
                    const size_t take = AZStd::min(size, sizeof(m_block) - m_blockUsed);
                    memcpy(m_block + m_blockUsed, bytes, take);
                    m_blockUsed += take;
                    bytes += take;
                    size -= take;
                    if (m_blockUsed == sizeof(m_block))
                    {
                        Compress(m_block);
                        m_blockUsed = 0;
                    }
                }
            }

            void Finish(AZ::u8 out[32])
            {
                const AZ::u64 bitLength = m_totalBytes * 8;
                const AZ::u8 padByte = 0x80;
                Update(&padByte, 1);
                const AZ::u8 zero = 0;
                while (m_blockUsed != 56)
                {
                    Update(&zero, 1);
                }
                AZ::u8 lengthBytes[8];
                for (int i = 0; i < 8; ++i)
                {
                    lengthBytes[i] = AZ::u8(bitLength >> (56 - i * 8));
                }
                // bypass Update so m_totalBytes stays untouched during padding accounting
                memcpy(m_block + m_blockUsed, lengthBytes, 8);
                Compress(m_block);
                for (int i = 0; i < 8; ++i)
                {
                    out[i * 4] = AZ::u8(m_h[i] >> 24);
                    out[i * 4 + 1] = AZ::u8(m_h[i] >> 16);
                    out[i * 4 + 2] = AZ::u8(m_h[i] >> 8);
                    out[i * 4 + 3] = AZ::u8(m_h[i]);
                }
            }
        };

        Digest DigestFromBytes(const AZ::u8 bytes[32])
        {
            Digest digest{};
            memcpy(digest.data(), bytes, sizeof(Digest));
            return digest;
        }

        // xorshift64* - deterministic program generator
        inline AZ::u64 NextRandom(AZ::u64& state)
        {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            return state * 0x2545f4914f6cdd1dull;
        }

        inline AZ::u64 Rotl64(AZ::u64 value, unsigned int count)
        {
            count &= 63u;
            return count == 0 ? value : (value << count) | (value >> (64u - count));
        }

        inline AZ::u64 ReadDataset(const AZ::u8* dataset, size_t datasetSize, AZ::u64 index)
        {
            if (!dataset || datasetSize < sizeof(AZ::u64))
            {
                return 0x9e3779b97f4a7c15ull; // fixed constant keeps the program well-defined without data
            }
            AZ::u64 value = 0;
            memcpy(&value, dataset + (index % (datasetSize - sizeof(AZ::u64) + 1)), sizeof(value));
            return value;
        }
    } // namespace

    Digest ExecuteProgram(AZ::u64 seed, AZ::u32 opCount, const AZ::u8* dataset, size_t datasetSize)
    {
        AZ::u64 prng = seed ^ 0xa076bc9f8fb1c2d3ull;
        if (prng == 0)
        {
            prng = 1;
        }
        AZ::u64 reg[8];
        for (AZ::u64& r : reg)
        {
            r = NextRandom(prng);
        }
        for (AZ::u32 op = 0; op < opCount; ++op)
        {
            const AZ::u64 instruction = NextRandom(prng);
            const unsigned int dst = unsigned(instruction) & 7u;
            const unsigned int src = unsigned(instruction >> 3) & 7u;
            switch (unsigned(instruction >> 6) & 7u)
            {
            case 0: // data-dependent dataset load (the RandomX-style part)
                reg[dst] ^= ReadDataset(dataset, datasetSize, reg[src] ^ (instruction >> 9));
                break;
            case 1:
                reg[dst] += reg[src] * 0x9e3779b97f4a7c15ull;
                break;
            case 2:
                reg[dst] = Rotl64(reg[dst] ^ reg[src], unsigned(instruction >> 9));
                break;
            case 3:
                reg[dst] *= (reg[src] | 1ull);
                break;
            case 4:
                reg[dst] ^= Rotl64(reg[src], unsigned(instruction >> 9));
                break;
            case 5:
                reg[dst] -= reg[src] ^ (instruction >> 9);
                break;
            case 6: // second dataset read pattern, address mixed differently
                reg[dst] += ReadDataset(dataset, datasetSize, Rotl64(reg[src], 17) + op);
                break;
            default:
                reg[dst] = Rotl64(reg[dst], 29) + (reg[src] >> 7) + instruction;
                break;
            }
        }
        Sha256State sha;
        sha.Update(&seed, sizeof(seed));
        sha.Update(&opCount, sizeof(opCount));
        sha.Update(reg, sizeof(reg));
        AZ::u8 out[32];
        sha.Finish(out);
        return DigestFromBytes(out);
    }

    Digest EvaluatePow(const PowParams& params, AZ::u64 nonce)
    {
        const size_t bufferBytes = size_t(AZStd::max(params.m_memoryKib, 1u)) * 1024;
        const size_t words = bufferBytes / sizeof(AZ::u64);

        // sequential memory-hard fill: each 32-byte stripe is the SHA-256 of
        // the previous stripe plus a counter, so the fill cannot be parallelized
        AZStd::vector<AZ::u64> storage(words);
        AZ::u8* buffer = reinterpret_cast<AZ::u8*>(storage.data());
        AZ::u8 stripe[32];
        {
            Sha256State sha;
            sha.Update(&params.m_seed, sizeof(params.m_seed));
            sha.Update(&nonce, sizeof(nonce));
            sha.Finish(stripe);
        }
        for (size_t offset = 0; offset < bufferBytes; offset += sizeof(stripe))
        {
            const size_t take = AZStd::min(sizeof(stripe), bufferBytes - offset);
            memcpy(buffer + offset, stripe, take);
            Sha256State sha;
            sha.Update(stripe, sizeof(stripe));
            sha.Update(&offset, sizeof(offset));
            sha.Finish(stripe);
        }

        // data-dependent random walk: forces the whole buffer to stay resident
        AZ::u64 accumulator = params.m_seed ^ Rotl64(nonce, 31);
        AZ::u64* wordBuffer = storage.data();
        const AZ::u64 walkSteps = AZ::u64(AZStd::max(params.m_passes, 1u)) * words;
        for (AZ::u64 step = 0; step < walkSteps; ++step)
        {
            const size_t index = size_t(accumulator % words);
            accumulator = Rotl64(accumulator ^ wordBuffer[index], 13) * 0x9e3779b97f4a7c15ull + step;
            wordBuffer[index] ^= accumulator;
        }

        Sha256State sha;
        sha.Update(&accumulator, sizeof(accumulator));
        sha.Update(buffer, AZStd::min(bufferBytes, size_t(4096))); // sample of the mutated buffer
        sha.Update(buffer + bufferBytes - AZStd::min(bufferBytes, size_t(4096)),
            AZStd::min(bufferBytes, size_t(4096)));
        AZ::u8 out[32];
        sha.Finish(out);
        return DigestFromBytes(out);
    }

    bool PowDigestMeetsDifficulty(const Digest& digest, AZ::u32 difficultyBits)
    {
        // digest words hold the SHA-256 bytes in order; count leading zero bits byte-wise
        const AZ::u8* bytes = reinterpret_cast<const AZ::u8*>(digest.data());
        AZ::u32 needed = difficultyBits;
        for (size_t i = 0; i < sizeof(Digest) && needed > 0; ++i)
        {
            if (needed >= 8)
            {
                if (bytes[i] != 0)
                {
                    return false;
                }
                needed -= 8;
            }
            else
            {
                return (bytes[i] >> (8 - needed)) == 0;
            }
        }
        return needed == 0;
    }

    bool VerifyPow(const PowParams& params, AZ::u64 nonce)
    {
        return PowDigestMeetsDifficulty(EvaluatePow(params, nonce), params.m_difficultyBits);
    }

    bool SolvePow(const PowParams& params, AZ::u64 maxAttempts, AZ::u64& outNonce)
    {
        AZ::u64 nonce = 0;
        for (AZ::u64 attempt = 0; maxAttempts == 0 || attempt < maxAttempts; ++attempt, ++nonce)
        {
            if (VerifyPow(params, nonce))
            {
                outNonce = nonce;
                return true;
            }
        }
        return false;
    }
} // namespace AZ::Attestation
