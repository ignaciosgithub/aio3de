/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#include <RayTracing/PortableGiUpdatePass.h>

#include <RayTracing/RayTracedShadowsFeatureProcessor.h>

#include <Atom/RPI.Public/Buffer/BufferSystemInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/ProbeGi.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/vector.h>
#include <cmath>

namespace AZ::Render
{
    AZ_CVAR_EXTERNED(bool, r_portableGi);
    AZ_CVAR(
        float,
        r_portableGiProbeSpacing,
        2.0f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Spacing in meters between portable GI probes.");
    AZ_CVAR(
        uint32_t,
        r_portableGiGridSize,
        24u,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Portable GI grid dimensions are GridSize x GridSize/2 x GridSize.");
    AZ_CVAR(
        uint32_t,
        r_portableGiRaysPerProbe,
        32u,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Spherical-Fibonacci rays traced for each portable GI probe update.");
    AZ_CVAR(
        uint32_t,
        r_portableGiProbesPerFrame,
        1024u,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Maximum number of portable GI probes updated per frame.");
    AZ_CVAR(
        float,
        r_portableGiHysteresis,
        0.97f,
        nullptr,
        AZ::ConsoleFunctorFlags::Null,
        "Portable GI probe history weight.");

    RPI::Ptr<PortableGiUpdatePass> PortableGiUpdatePass::Create(const RPI::PassDescriptor& descriptor)
    {
        return aznew PortableGiUpdatePass(descriptor);
    }

    PortableGiUpdatePass::PortableGiUpdatePass(const RPI::PassDescriptor& descriptor)
        : RPI::ComputePass(descriptor)
    {
    }

    void PortableGiUpdatePass::EnsureProbeBuffers(uint32_t gridSize)
    {
        gridSize = AZStd::clamp(gridSize, 2u, 128u);
        if (gridSize == m_gridSize && m_probeBuffers[0] && m_probeBuffers[1])
        {
            return;
        }

        m_gridSize = gridSize;
        m_gridDims = { gridSize, AZStd::max(2u, gridSize / 2u), gridSize };
        m_gridCount = m_gridDims[0] * m_gridDims[1] * m_gridDims[2];
        m_readIndex = 0;
        AZStd::vector<AZ::Vector4> initialData(static_cast<size_t>(m_gridCount) * 4, AZ::Vector4::CreateZero());
        for (uint32_t bufferIndex = 0; bufferIndex < 2; ++bufferIndex)
        {
            RPI::CommonBufferDescriptor descriptor;
            descriptor.m_poolType = RPI::CommonBufferPoolType::ReadWrite;
            descriptor.m_bufferName = bufferIndex == 0 ? "PortableGi.Probes0" : "PortableGi.Probes1";
            descriptor.m_elementSize = sizeof(AZ::Vector4);
            descriptor.m_byteCount = static_cast<uint64_t>(initialData.size()) * sizeof(AZ::Vector4);
            descriptor.m_bufferData = initialData.data();
            m_probeBuffers[bufferIndex] = RPI::BufferSystemInterface::Get()->CreateBufferFromCommonPool(descriptor);
        }
    }

    void PortableGiUpdatePass::BuildInternal()
    {
        EnsureProbeBuffers(24u);
        ComputePass::BuildInternal();
    }

    void PortableGiUpdatePass::FrameBeginInternal(FramePrepareParams params)
    {
        EnsureProbeBuffers(static_cast<uint32_t>(r_portableGiGridSize));
        m_probeSpacing = AZStd::max(0.1f, static_cast<float>(r_portableGiProbeSpacing));
        m_hysteresis = AZStd::clamp(static_cast<float>(r_portableGiHysteresis), 0.0f, 1.0f);

        auto* featureProcessor = GetScene() ? GetScene()->GetFeatureProcessor<RayTracedShadowsFeatureProcessor>() : nullptr;
        if (featureProcessor)
        {
            featureProcessor->PrepareSharedBvhBuffers();
            const auto& srg = m_shaderResourceGroup;
            const auto& nodes = featureProcessor->GetSharedBvhNodesBuffer();
            const auto& vertices = featureProcessor->GetSharedBvhTriangleVerticesBuffer();
            const auto& primitiveIndices = featureProcessor->GetSharedBvhPrimitiveIndicesBuffer();
            const auto& albedo = featureProcessor->GetSharedBvhAlbedoBuffer();
            if (srg && nodes && vertices && primitiveIndices && albedo)
            {
                srg->SetBufferView(m_nodesIndex, nodes->GetBufferView());
                srg->SetBufferView(m_verticesIndex, vertices->GetBufferView());
                srg->SetBufferView(m_primitiveIndicesIndex, primitiveIndices->GetBufferView());
                srg->SetBufferView(m_albedoIndex, albedo->GetBufferView());
            }

            m_gridOrigin = AZ::SnapProbeGridOrigin(featureProcessor->GetCameraPosition(), m_probeSpacing, m_gridDims.data());
        }

        const uint32_t writeIndex = 1u - m_readIndex;
        if (auto* srg = m_shaderResourceGroup.get())
        {
            if (m_probeBuffers[m_readIndex] && m_probeBuffers[writeIndex])
            {
                srg->SetBufferView(m_probeReadIndex, m_probeBuffers[m_readIndex]->GetBufferView());
                srg->SetBufferView(m_probeWriteIndex, m_probeBuffers[writeIndex]->GetBufferView());
            }
            srg->SetConstant(m_nodeCountIndex, featureProcessor ? featureProcessor->GetSharedBvhNodeCount() : 0u);
            srg->SetConstant(m_gridCountIndex, m_gridCount);
            srg->SetConstant(
                m_probesPerFrameIndex,
                AZStd::clamp<uint32_t>(static_cast<uint32_t>(r_portableGiProbesPerFrame), 1u, m_gridCount));
            srg->SetConstant(
                m_raysPerProbeIndex, AZStd::clamp<uint32_t>(static_cast<uint32_t>(r_portableGiRaysPerProbe), 8u, 256u));
            srg->SetConstant(m_frameIndexIndex, m_frameIndex);
            srg->SetConstant(m_probeSpacingIndex, m_probeSpacing);
            srg->SetConstant(m_hysteresisIndex, m_hysteresis);
            srg->SetConstant(m_gridOriginIndex, m_gridOrigin);
            srg->SetConstant(
                m_gridDimsIndex,
                AZ::Vector3(static_cast<float>(m_gridDims[0]), static_cast<float>(m_gridDims[1]), static_cast<float>(m_gridDims[2])));
            srg->SetConstant(m_sunDirectionIndex, featureProcessor ? featureProcessor->GetSunDirection() : AZ::Vector3::CreateZero());
            srg->SetConstant(
                m_sunIlluminanceOverPiIndex,
                featureProcessor ? featureProcessor->GetSunIlluminanceOverPi() : AZ::Vector3::CreateZero());
        }
        SetTargetThreadCounts(AZStd::max(1u, m_gridCount), 1, 1);
        if (featureProcessor)
        {
            m_readIndex = writeIndex;
        }
        ++m_frameIndex;
        ComputePass::FrameBeginInternal(params);
    }
} // namespace AZ::Render
