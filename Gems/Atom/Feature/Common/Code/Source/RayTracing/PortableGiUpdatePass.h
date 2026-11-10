/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI.Reflect/ShaderInputNameIndex.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/Pass/ComputePass.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/array.h>

namespace AZ::Render
{
    class PortableGiUpdatePass final
        : public RPI::ComputePass
    {
        AZ_RPI_PASS(PortableGiUpdatePass);

    public:
        AZ_RTTI(PortableGiUpdatePass, "{B6A8DF79-E2A1-4BE0-9303-672042137710}", RPI::ComputePass);
        AZ_CLASS_ALLOCATOR(PortableGiUpdatePass, SystemAllocator);

        static RPI::Ptr<PortableGiUpdatePass> Create(const RPI::PassDescriptor& descriptor);

        const Data::Instance<RPI::Buffer>& GetCurrentProbeBuffer() const { return m_probeBuffers[m_readIndex]; }
        AZ::Vector3 GetGridOrigin() const { return m_gridOrigin; }
        float GetProbeSpacing() const { return m_probeSpacing; }
        AZStd::array<uint32_t, 3> GetGridDimensions() const { return m_gridDims; }

    protected:
        PortableGiUpdatePass(const RPI::PassDescriptor& descriptor);
        void BuildInternal() override;
        void FrameBeginInternal(FramePrepareParams params) override;

    private:
        void EnsureProbeBuffers(uint32_t gridSize);

        Data::Instance<RPI::Buffer> m_probeBuffers[2];
        AZStd::array<uint32_t, 3> m_gridDims{ 0, 0, 0 };
        uint32_t m_gridCount = 0;
        uint32_t m_gridSize = 0;
        uint32_t m_readIndex = 0;
        uint32_t m_frameIndex = 0;
        float m_probeSpacing = 2.0f;
        float m_hysteresis = 0.97f;
        AZ::Vector3 m_gridOrigin = AZ::Vector3::CreateZero();

        AZ::RHI::ShaderInputNameIndex m_nodesIndex = "m_nodes";
        AZ::RHI::ShaderInputNameIndex m_verticesIndex = "m_triangleVertices";
        AZ::RHI::ShaderInputNameIndex m_primitiveIndicesIndex = "m_primitiveIndices";
        AZ::RHI::ShaderInputNameIndex m_albedoIndex = "m_triangleAlbedo";
        AZ::RHI::ShaderInputNameIndex m_probeReadIndex = "m_probeRead";
        AZ::RHI::ShaderInputNameIndex m_probeWriteIndex = "m_probeWrite";
        AZ::RHI::ShaderInputNameIndex m_nodeCountIndex = "m_nodeCount";
        AZ::RHI::ShaderInputNameIndex m_gridCountIndex = "m_gridCount";
        AZ::RHI::ShaderInputNameIndex m_probesPerFrameIndex = "m_probesPerFrame";
        AZ::RHI::ShaderInputNameIndex m_raysPerProbeIndex = "m_raysPerProbe";
        AZ::RHI::ShaderInputNameIndex m_frameIndexIndex = "m_frameIndex";
        AZ::RHI::ShaderInputNameIndex m_probeSpacingIndex = "m_probeSpacing";
        AZ::RHI::ShaderInputNameIndex m_hysteresisIndex = "m_hysteresis";
        AZ::RHI::ShaderInputNameIndex m_gridOriginIndex = "m_gridOrigin";
        AZ::RHI::ShaderInputNameIndex m_gridDimsIndex = "m_gridDims";
        AZ::RHI::ShaderInputNameIndex m_sunDirectionIndex = "m_sunDirection";
        AZ::RHI::ShaderInputNameIndex m_sunIlluminanceOverPiIndex = "m_sunIlluminanceOverPi";
    };
} // namespace AZ::Render
