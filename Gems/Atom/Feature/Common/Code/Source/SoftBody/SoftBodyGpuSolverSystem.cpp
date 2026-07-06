/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <SoftBody/SoftBodyGpuSolverSystem.h>

#include <AzCore/Interface/Interface.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>

namespace AZ
{
    namespace Render
    {
        namespace
        {
            Data::Instance<RPI::Buffer> CreateStructuredBuffer(
                const char* name, RPI::CommonBufferPoolType poolType, uint32_t elementSize, uint32_t elementCount, const void* data)
            {
                RPI::CommonBufferDescriptor desc;
                desc.m_poolType = poolType;
                desc.m_bufferName = name;
                desc.m_elementSize = elementSize;
                desc.m_byteCount = static_cast<uint64_t>(elementSize) * AZStd::max<uint32_t>(1, elementCount);
                desc.m_bufferData = (elementCount > 0) ? data : nullptr;
                return RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(desc);
            }
        } // namespace

        void SoftBodyGpuSolverSystem::Activate()
        {
            AZ::Interface<SoftBodyGpuSolverInterface>::Register(this);
        }

        void SoftBodyGpuSolverSystem::Deactivate()
        {
            AZ::Interface<SoftBodyGpuSolverInterface>::Unregister(this);
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            m_bodies.clear();
            m_readbacks.clear();
        }

        SoftBodyGpuSolverSystem::BodyRecord* SoftBodyGpuSolverSystem::FindBody(SoftBodyGpuBodyHandle handle)
        {
            for (BodyRecord& body : m_bodies)
            {
                if (body.m_handle == handle)
                {
                    return &body;
                }
            }
            return nullptr;
        }

        SoftBodyGpuBodyHandle SoftBodyGpuSolverSystem::RegisterBody(const SoftBodyGpuBodyDesc& desc)
        {
            if (desc.m_particles.empty())
            {
                return InvalidSoftBodyGpuBodyHandle;
            }

            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            BodyRecord& body = m_bodies.emplace_back();
            body.m_handle = m_nextHandle++;
            body.m_desc = desc;
            ++m_generation;
            return body.m_handle;
        }

        void SoftBodyGpuSolverSystem::UnregisterBody(SoftBodyGpuBodyHandle handle)
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            for (size_t i = 0; i < m_bodies.size(); ++i)
            {
                if (m_bodies[i].m_handle == handle)
                {
                    m_bodies.erase(m_bodies.begin() + i);
                    ++m_generation;
                    return;
                }
            }
        }

        void SoftBodyGpuSolverSystem::QueueStep(SoftBodyGpuBodyHandle handle, float deltaTime)
        {
            if (deltaTime <= 0.0f)
            {
                return;
            }
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            if (BodyRecord* body = FindBody(handle))
            {
                body->m_pendingDt = deltaTime;
                body->m_stepQueued = true;
            }
        }

        bool SoftBodyGpuSolverSystem::TryGetPositions(SoftBodyGpuBodyHandle handle, AZStd::vector<Vector3>& outPositions)
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            BodyRecord* body = FindBody(handle);
            if (!body || body->m_latestPositions.empty())
            {
                return false;
            }
            outPositions = body->m_latestPositions;
            return true;
        }

        uint32_t SoftBodyGpuSolverSystem::GetGeneration() const
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            return m_generation;
        }

        void SoftBodyGpuSolverSystem::PackBuffers()
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            if (m_packedGeneration == m_generation && m_positionsBuffer)
            {
                return;
            }

            // Concatenate every body into shared arrays; constraint/triangle indices become
            // global particle indices so a single dispatch (one thread group per body) can
            // address everything through per-body offsets.
            AZStd::vector<Vector4> particles;
            AZStd::vector<uint32_t> constraintParticles; // 2 per constraint, global indices
            AZStd::vector<float> constraintParams;       // 2 per constraint: restLength, compliance
            AZStd::vector<uint32_t> triangles;           // 3 per triangle, global indices

            for (BodyRecord& body : m_bodies)
            {
                const SoftBodyGpuBodyDesc& desc = body.m_desc;
                body.m_particleOffset = static_cast<uint32_t>(particles.size());
                body.m_constraintOffset = static_cast<uint32_t>(constraintParticles.size() / 2);
                body.m_triangleOffset = static_cast<uint32_t>(triangles.size() / 3);

                // Seed from the latest simulated positions when re-packing mid-simulation
                // (a body added or removed), so existing bodies don't snap back to rest.
                const bool useLatest = body.m_latestPositions.size() == desc.m_particles.size();
                for (size_t i = 0; i < desc.m_particles.size(); ++i)
                {
                    Vector4 particle = desc.m_particles[i];
                    if (useLatest)
                    {
                        const Vector3& p = body.m_latestPositions[i];
                        particle.Set(p.GetX(), p.GetY(), p.GetZ(), particle.GetW());
                    }
                    particles.push_back(particle);
                }

                const size_t constraintCount = desc.m_constraintParticles.size() / 2;
                for (size_t c = 0; c < constraintCount; ++c)
                {
                    constraintParticles.push_back(desc.m_constraintParticles[2 * c] + body.m_particleOffset);
                    constraintParticles.push_back(desc.m_constraintParticles[2 * c + 1] + body.m_particleOffset);
                    constraintParams.push_back(c < desc.m_constraintRestLengths.size() ? desc.m_constraintRestLengths[c] : 0.0f);
                    constraintParams.push_back(c < desc.m_constraintCompliances.size() ? desc.m_constraintCompliances[c] : 0.0f);
                }

                for (uint32_t index : desc.m_triangles)
                {
                    triangles.push_back(index + body.m_particleOffset);
                }
            }

            const uint32_t particleCount = static_cast<uint32_t>(particles.size());
            const uint32_t constraintCount = static_cast<uint32_t>(constraintParticles.size() / 2);
            const uint32_t triangleCount = static_cast<uint32_t>(triangles.size() / 3);

            // Particle -> constraint adjacency: entry = (constraintIndex << 1) | isSecondEndpoint.
            AZStd::vector<uint32_t> adjacencyOffsets(particleCount + 1, 0);
            AZStd::vector<uint32_t> adjacency;
            {
                for (uint32_t c = 0; c < constraintCount; ++c)
                {
                    ++adjacencyOffsets[constraintParticles[2 * c] + 1];
                    ++adjacencyOffsets[constraintParticles[2 * c + 1] + 1];
                }
                for (uint32_t i = 1; i <= particleCount; ++i)
                {
                    adjacencyOffsets[i] += adjacencyOffsets[i - 1];
                }
                adjacency.resize(adjacencyOffsets[particleCount]);
                AZStd::vector<uint32_t> cursor(adjacencyOffsets.begin(), adjacencyOffsets.end() - 1);
                for (uint32_t c = 0; c < constraintCount; ++c)
                {
                    adjacency[cursor[constraintParticles[2 * c]]++] = (c << 1);
                    adjacency[cursor[constraintParticles[2 * c + 1]]++] = (c << 1) | 1;
                }
            }

            // Particle -> triangle adjacency: entry = (triangleIndex << 2) | corner.
            AZStd::vector<uint32_t> triAdjacencyOffsets(particleCount + 1, 0);
            AZStd::vector<uint32_t> triAdjacency;
            {
                for (uint32_t t = 0; t < triangleCount; ++t)
                {
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        ++triAdjacencyOffsets[triangles[3 * t + corner] + 1];
                    }
                }
                for (uint32_t i = 1; i <= particleCount; ++i)
                {
                    triAdjacencyOffsets[i] += triAdjacencyOffsets[i - 1];
                }
                triAdjacency.resize(triAdjacencyOffsets[particleCount]);
                AZStd::vector<uint32_t> cursor(triAdjacencyOffsets.begin(), triAdjacencyOffsets.end() - 1);
                for (uint32_t t = 0; t < triangleCount; ++t)
                {
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        triAdjacency[cursor[triangles[3 * t + corner]]++] = (t << 2) | corner;
                    }
                }
            }

            const AZStd::vector<Vector4> zeroVectors(AZStd::max<uint32_t>(1, particleCount), Vector4::CreateZero());

            m_positionsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.Positions", RPI::CommonBufferPoolType::ReadWrite, sizeof(Vector4), particleCount, particles.data());
            m_prevPositionsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.PrevPositions", RPI::CommonBufferPoolType::ReadWrite, sizeof(Vector4), particleCount, particles.data());
            m_velocitiesBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.Velocities", RPI::CommonBufferPoolType::ReadWrite, sizeof(Vector4), particleCount, zeroVectors.data());
            m_constraintCorrectionsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.ConstraintCorrections", RPI::CommonBufferPoolType::ReadWrite, sizeof(Vector4), constraintCount, nullptr);
            m_particleGradientsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.ParticleGradients", RPI::CommonBufferPoolType::ReadWrite, sizeof(Vector4), particleCount, nullptr);
            m_triangleGradientsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.TriangleGradients", RPI::CommonBufferPoolType::ReadWrite, sizeof(Vector4), triangleCount * 3, nullptr);

            m_bodyParamsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.BodyParams", RPI::CommonBufferPoolType::ReadOnly, sizeof(SoftBodyGpuBodyParams),
                static_cast<uint32_t>(m_bodies.size()), nullptr);
            m_constraintParticlesBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.ConstraintParticles", RPI::CommonBufferPoolType::ReadOnly, sizeof(uint32_t) * 2, constraintCount,
                constraintParticles.data());
            m_constraintParamsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.ConstraintParams", RPI::CommonBufferPoolType::ReadOnly, sizeof(float) * 2, constraintCount,
                constraintParams.data());
            m_adjacencyBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.Adjacency", RPI::CommonBufferPoolType::ReadOnly, sizeof(uint32_t),
                static_cast<uint32_t>(adjacency.size()), adjacency.data());
            m_adjacencyOffsetsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.AdjacencyOffsets", RPI::CommonBufferPoolType::ReadOnly, sizeof(uint32_t),
                static_cast<uint32_t>(adjacencyOffsets.size()), adjacencyOffsets.data());
            m_trianglesBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.Triangles", RPI::CommonBufferPoolType::ReadOnly, sizeof(uint32_t),
                static_cast<uint32_t>(triangles.size()), triangles.data());
            m_triAdjacencyBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.TriAdjacency", RPI::CommonBufferPoolType::ReadOnly, sizeof(uint32_t),
                static_cast<uint32_t>(triAdjacency.size()), triAdjacency.data());
            m_triAdjacencyOffsetsBuffer = CreateStructuredBuffer(
                "SoftBodyGpu.TriAdjacencyOffsets", RPI::CommonBufferPoolType::ReadOnly, sizeof(uint32_t),
                static_cast<uint32_t>(triAdjacencyOffsets.size()), triAdjacencyOffsets.data());

            m_packedParticleCount = particleCount;
            m_packedGeneration = m_generation;
        }

        uint32_t SoftBodyGpuSolverSystem::UpdateFrameParams()
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            if (m_bodies.empty() || !m_bodyParamsBuffer || m_packedGeneration != m_generation)
            {
                return 0;
            }

            AZStd::vector<SoftBodyGpuBodyParams> params;
            params.reserve(m_bodies.size());
            for (BodyRecord& body : m_bodies)
            {
                const SoftBodyGpuBodyDesc& desc = body.m_desc;
                SoftBodyGpuBodyParams& p = params.emplace_back();
                p.m_particleOffset = body.m_particleOffset;
                p.m_particleCount = static_cast<uint32_t>(desc.m_particles.size());
                p.m_constraintOffset = body.m_constraintOffset;
                p.m_constraintCount = static_cast<uint32_t>(desc.m_constraintParticles.size() / 2);
                p.m_triangleOffset = body.m_triangleOffset;
                p.m_triangleCount = static_cast<uint32_t>(desc.m_triangles.size() / 3);
                p.m_substeps = AZStd::max<uint32_t>(1, desc.m_substeps);
                p.m_iterations = AZStd::max<uint32_t>(1, desc.m_iterations);
                p.m_gravity[0] = desc.m_gravity.GetX();
                p.m_gravity[1] = desc.m_gravity.GetY();
                p.m_gravity[2] = desc.m_gravity.GetZ();
                p.m_dtPerSubstep = body.m_stepQueued ? (body.m_pendingDt / static_cast<float>(p.m_substeps)) : 0.0f;
                p.m_damping = desc.m_damping;
                p.m_groundHeight = desc.m_groundHeight;
                p.m_groundEnabled = desc.m_groundPlaneEnabled ? 1 : 0;
                p.m_groundFriction = desc.m_groundFriction;
                p.m_pressure = desc.m_pressure;
                p.m_pressureCompliance = desc.m_pressureCompliance;
                p.m_restVolume = desc.m_restVolume;
                p.m_stepQueued = body.m_stepQueued ? 1 : 0;
                body.m_stepQueued = false;
            }

            m_bodyParamsBuffer->UpdateData(params.data(), params.size() * sizeof(SoftBodyGpuBodyParams));
            return static_cast<uint32_t>(params.size());
        }

        AZStd::shared_ptr<RPI::AttachmentReadback> SoftBodyGpuSolverSystem::AcquireReadback()
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            if (m_readbacks.size() < ReadbackCount)
            {
                auto readback = AZStd::make_shared<RPI::AttachmentReadback>(
                    RHI::ScopeId{ AZStd::string::format("SoftBodyGpuReadback_%zu", m_readbacks.size()) });
                readback->SetCallback(
                    [this](const RPI::AttachmentReadback::ReadbackResult& result)
                    {
                        OnReadbackComplete(result);
                    });
                m_readbacks.push_back(readback);
                m_readbacks.back()->SetUserIdentifier(m_packedGeneration);
                return m_readbacks.back();
            }
            for (auto& readback : m_readbacks)
            {
                if (readback->IsReady())
                {
                    readback->SetUserIdentifier(m_packedGeneration);
                    return readback;
                }
            }
            return nullptr;
        }

        void SoftBodyGpuSolverSystem::OnReadbackComplete(const RPI::AttachmentReadback::ReadbackResult& result)
        {
            if (result.m_state != RPI::AttachmentReadback::ReadbackState::Success || !result.m_dataBuffer)
            {
                return;
            }

            AZStd::lock_guard<AZStd::mutex> lock(m_mutex);
            // Drop results issued against a previous packing (offsets no longer match).
            if (result.m_userIdentifier != m_packedGeneration ||
                result.m_dataBuffer->size() < static_cast<size_t>(m_packedParticleCount) * sizeof(Vector4))
            {
                return;
            }

            const float* data = reinterpret_cast<const float*>(result.m_dataBuffer->data());
            for (BodyRecord& body : m_bodies)
            {
                const size_t count = body.m_desc.m_particles.size();
                if (body.m_particleOffset + count > m_packedParticleCount)
                {
                    continue;
                }
                body.m_latestPositions.resize(count);
                for (size_t i = 0; i < count; ++i)
                {
                    const float* p = data + 4 * (body.m_particleOffset + i);
                    body.m_latestPositions[i].Set(p[0], p[1], p[2]);
                }
            }
        }
    } // namespace Render
} // namespace AZ
