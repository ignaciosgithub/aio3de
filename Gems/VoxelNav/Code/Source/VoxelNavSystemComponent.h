/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <VoxelNav/VoxelNavBus.h>

namespace VoxelNav
{
    //! Routes the global VoxelNavRequestBus to the Voxel Nav Volume containing the queried position.
    class VoxelNavSystemComponent
        : public AZ::Component
        , private VoxelNavRequestBus::Handler
    {
    public:
        AZ_COMPONENT(VoxelNavSystemComponent, "{0D5B7A9C-6E1F-4C2D-B3A4-5F6E7D8C9B0A}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);

        // AZ::Component
        void Activate() override;
        void Deactivate() override;

        // VoxelNavRequestBus
        AZStd::vector<AZ::Vector3> FindPath(const AZ::Vector3& start, const AZ::Vector3& goal) override;
        AZStd::vector<AZ::Vector3> FindRawPath(const AZ::Vector3& start, const AZ::Vector3& goal) override;
        bool IsNavigable(const AZ::Vector3& position) override;
        AZ::Vector3 GetNearestNavigable(const AZ::Vector3& position, float maxDistance) override;
        bool IsReady() override;
        void Rebuild() override;
        AZ::u32 GetVolumeCount() override;
        AZ::EntityId FindVolumeAt(const AZ::Vector3& position) override;

    private:
        //! Volume containing `position`, else the one containing `fallback`, else the first active volume.
        static VoxelNavVolumeRequests* SelectVolume(const AZ::Vector3& position, const AZ::Vector3& fallback);
    };
} // namespace VoxelNav
