/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ScriptHost.h"

#include <AzCore/PlatformDef.h>

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/EBus/Results.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Transform.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/StringFunc/StringFunc.h>
#include <AzCore/Time/ITime.h>
#include <AzCore/Utils/Utils.h>

#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Entity/GameEntityContextBus.h>
#include <AzFramework/Input/Buses/Requests/InputChannelRequestBus.h>
#include <AzFramework/Input/Buses/Requests/InputSystemCursorRequestBus.h>
#include <AzFramework/Input/Channels/InputChannel.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <AzFramework/Physics/Common/PhysicsSceneQueries.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/RigidBodyBus.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

#include <LmbrCentral/Scripting/TagComponentBus.h>

#include <cstdio>

namespace CSharpScripting
{
    namespace
    {
        void ApiLog(int level, const char* message)
        {
            switch (level)
            {
            case 2:
                AZ_Error("C#", false, "%s", message);
                break;
            case 1:
                AZ_Warning("C#", false, "%s", message);
                break;
            default:
                AZ_Printf("C#", "%s", message);
                break;
            }
        }

        void ApiGetWorldPosition(AZ::u64 entityId, float* xyz)
        {
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(position, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldTranslation);
            xyz[0] = position.GetX();
            xyz[1] = position.GetY();
            xyz[2] = position.GetZ();
        }

        void ApiSetWorldPosition(AZ::u64 entityId, float x, float y, float z)
        {
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetWorldTranslation, AZ::Vector3(x, y, z));
        }

        void ApiGetWorldRotationEuler(AZ::u64 entityId, float* xyzDegrees)
        {
            AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(rotation, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            const AZ::Vector3 euler = rotation.GetEulerDegrees();
            xyzDegrees[0] = euler.GetX();
            xyzDegrees[1] = euler.GetY();
            xyzDegrees[2] = euler.GetZ();
        }

        void ApiSetWorldRotationEuler(AZ::u64 entityId, float xDegrees, float yDegrees, float zDegrees)
        {
            const AZ::Quaternion rotation = AZ::Quaternion::CreateFromEulerDegreesXYZ(AZ::Vector3(xDegrees, yDegrees, zDegrees));
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(transform, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldTM);
            transform.SetRotation(rotation);
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetWorldTM, transform);
        }

        float ApiGetUniformScale(AZ::u64 entityId)
        {
            float scale = 1.0f;
            AZ::TransformBus::EventResult(scale, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldUniformScale);
            return scale;
        }

        void ApiSetUniformScale(AZ::u64 entityId, float scale)
        {
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetLocalUniformScale, scale);
        }

        AZ::u64 ApiFindEntityByName(const char* name)
        {
            AZ::u64 found = 0;
            AZ::ComponentApplicationBus::Broadcast(
                &AZ::ComponentApplicationBus::Events::EnumerateEntities,
                [&found, name](AZ::Entity* entity)
                {
                    if (found == 0 && entity->GetName() == name)
                    {
                        found = static_cast<AZ::u64>(entity->GetId());
                    }
                });
            return found;
        }

        void ApiGetEntityName(AZ::u64 entityId, char* buffer, int bufferSize)
        {
            AZStd::string name;
            AZ::ComponentApplicationBus::BroadcastResult(
                name, &AZ::ComponentApplicationBus::Events::GetEntityName, AZ::EntityId(entityId));
            azstrncpy(buffer, bufferSize, name.c_str(), bufferSize - 1);
            buffer[bufferSize - 1] = '\0';
        }

        void ApiGetLocalPosition(AZ::u64 entityId, float* xyz)
        {
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(position, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetLocalTranslation);
            xyz[0] = position.GetX();
            xyz[1] = position.GetY();
            xyz[2] = position.GetZ();
        }

        void ApiSetLocalPosition(AZ::u64 entityId, float x, float y, float z)
        {
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(x, y, z));
        }

        void ApiGetWorldRotationQuaternion(AZ::u64 entityId, float* xyzw)
        {
            AZ::Quaternion rotation = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(rotation, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            xyzw[0] = rotation.GetX();
            xyzw[1] = rotation.GetY();
            xyzw[2] = rotation.GetZ();
            xyzw[3] = rotation.GetW();
        }

        void ApiSetWorldRotationQuaternion(AZ::u64 entityId, float x, float y, float z, float w)
        {
            AZ::Quaternion rotation(x, y, z, w);
            rotation.Normalize();
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(transform, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldTM);
            transform.SetRotation(rotation);
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetWorldTM, transform);
        }

        void ApiGetWorldBasis(AZ::u64 entityId, float* rightForwardUp9)
        {
            AZ::Transform transform = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(transform, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetWorldTM);
            const AZ::Vector3 right = transform.GetBasisX();
            const AZ::Vector3 forward = transform.GetBasisY();
            const AZ::Vector3 up = transform.GetBasisZ();
            rightForwardUp9[0] = right.GetX();
            rightForwardUp9[1] = right.GetY();
            rightForwardUp9[2] = right.GetZ();
            rightForwardUp9[3] = forward.GetX();
            rightForwardUp9[4] = forward.GetY();
            rightForwardUp9[5] = forward.GetZ();
            rightForwardUp9[6] = up.GetX();
            rightForwardUp9[7] = up.GetY();
            rightForwardUp9[8] = up.GetZ();
        }

        void ApiSetParent(AZ::u64 entityId, AZ::u64 parentId)
        {
            AZ::TransformBus::Event(AZ::EntityId(entityId), &AZ::TransformBus::Events::SetParent, AZ::EntityId(parentId));
        }

        AZ::u64 ApiGetParent(AZ::u64 entityId)
        {
            AZ::EntityId parentId;
            AZ::TransformBus::EventResult(parentId, AZ::EntityId(entityId), &AZ::TransformBus::Events::GetParentId);
            return static_cast<AZ::u64>(parentId);
        }

        AZ::u64 ApiCreateEntity(const char* name)
        {
            AZ::Entity* entity = nullptr;
            AzFramework::GameEntityContextRequestBus::BroadcastResult(
                entity, &AzFramework::GameEntityContextRequestBus::Events::CreateGameEntity, name);
            if (!entity)
            {
                return 0;
            }
            entity->CreateComponent<AzFramework::TransformComponent>();
            AzFramework::GameEntityContextRequestBus::Broadcast(
                &AzFramework::GameEntityContextRequestBus::Events::ActivateGameEntity, entity->GetId());
            return static_cast<AZ::u64>(entity->GetId());
        }

        void ApiDestroyEntity(AZ::u64 entityId)
        {
            AzFramework::GameEntityContextRequestBus::Broadcast(
                &AzFramework::GameEntityContextRequestBus::Events::DestroyGameEntity, AZ::EntityId(entityId));
        }

        void ApiSetEntityActive(AZ::u64 entityId, int active)
        {
            if (active)
            {
                AzFramework::GameEntityContextRequestBus::Broadcast(
                    &AzFramework::GameEntityContextRequestBus::Events::ActivateGameEntity, AZ::EntityId(entityId));
            }
            else
            {
                AzFramework::GameEntityContextRequestBus::Broadcast(
                    &AzFramework::GameEntityContextRequestBus::Events::DeactivateGameEntity, AZ::EntityId(entityId));
            }
        }

        int ApiIsEntityActive(AZ::u64 entityId)
        {
            AZ::Entity* entity = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(
                entity, &AZ::ComponentApplicationBus::Events::FindEntity, AZ::EntityId(entityId));
            return (entity && entity->GetState() == AZ::Entity::State::Active) ? 1 : 0;
        }

        const AzFramework::InputChannel* FindChannel(const char* channelName)
        {
            const AzFramework::InputChannel* channel = nullptr;
            const AzFramework::InputChannelRequests::BusIdType requestId{ AzFramework::InputChannelId(channelName) };
            AzFramework::InputChannelRequestBus::EventResult(
                channel, requestId, &AzFramework::InputChannelRequests::GetInputChannel);
            return channel;
        }

        int ApiIsChannelActive(const char* channelName)
        {
            const AzFramework::InputChannel* channel = FindChannel(channelName);
            return (channel && channel->IsActive()) ? 1 : 0;
        }

        float ApiGetChannelValue(const char* channelName)
        {
            const AzFramework::InputChannel* channel = FindChannel(channelName);
            return channel ? channel->GetValue() : 0.0f;
        }

        void ApiGetCursorPositionNormalized(float* xy)
        {
            AZ::Vector2 position = AZ::Vector2::CreateZero();
            AzFramework::InputSystemCursorRequestBus::EventResult(
                position,
                AzFramework::InputDeviceMouse::Id,
                &AzFramework::InputSystemCursorRequests::GetSystemCursorPositionNormalized);
            xy[0] = position.GetX();
            xy[1] = position.GetY();
        }

        double ApiGetTimeSeconds()
        {
            if (const AZ::ITime* time = AZ::Interface<AZ::ITime>::Get())
            {
                return static_cast<double>(static_cast<AZ::s64>(time->GetElapsedTimeUs())) / 1e6;
            }
            return 0.0;
        }

        int ApiRayCast(
            float originX, float originY, float originZ,
            float directionX, float directionY, float directionZ,
            float maxDistance, float* hitPositionNormal6, float* hitDistance, AZ::u64* hitEntityId)
        {
            auto* sceneInterface = AZ::Interface<AzPhysics::SceneInterface>::Get();
            if (!sceneInterface)
            {
                return 0;
            }
            const AzPhysics::SceneHandle sceneHandle = sceneInterface->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName);
            if (sceneHandle == AzPhysics::InvalidSceneHandle)
            {
                return 0;
            }

            AzPhysics::RayCastRequest request;
            request.m_start = AZ::Vector3(originX, originY, originZ);
            request.m_direction = AZ::Vector3(directionX, directionY, directionZ).GetNormalizedSafe();
            request.m_distance = maxDistance;
            const AzPhysics::SceneQueryHits hits = sceneInterface->QueryScene(sceneHandle, &request);
            if (hits.m_hits.empty())
            {
                return 0;
            }

            const AzPhysics::SceneQueryHit& hit = hits.m_hits.front();
            hitPositionNormal6[0] = hit.m_position.GetX();
            hitPositionNormal6[1] = hit.m_position.GetY();
            hitPositionNormal6[2] = hit.m_position.GetZ();
            hitPositionNormal6[3] = hit.m_normal.GetX();
            hitPositionNormal6[4] = hit.m_normal.GetY();
            hitPositionNormal6[5] = hit.m_normal.GetZ();
            *hitDistance = hit.m_distance;
            *hitEntityId = static_cast<AZ::u64>(hit.m_entityId);
            return 1;
        }

        void ApiGetLinearVelocity(AZ::u64 entityId, float* xyz)
        {
            AZ::Vector3 velocity = AZ::Vector3::CreateZero();
            Physics::RigidBodyRequestBus::EventResult(
                velocity, AZ::EntityId(entityId), &Physics::RigidBodyRequests::GetLinearVelocity);
            xyz[0] = velocity.GetX();
            xyz[1] = velocity.GetY();
            xyz[2] = velocity.GetZ();
        }

        void ApiSetLinearVelocity(AZ::u64 entityId, float x, float y, float z)
        {
            Physics::RigidBodyRequestBus::Event(
                AZ::EntityId(entityId), &Physics::RigidBodyRequests::SetLinearVelocity, AZ::Vector3(x, y, z));
        }

        void ApiGetAngularVelocity(AZ::u64 entityId, float* xyz)
        {
            AZ::Vector3 velocity = AZ::Vector3::CreateZero();
            Physics::RigidBodyRequestBus::EventResult(
                velocity, AZ::EntityId(entityId), &Physics::RigidBodyRequests::GetAngularVelocity);
            xyz[0] = velocity.GetX();
            xyz[1] = velocity.GetY();
            xyz[2] = velocity.GetZ();
        }

        void ApiSetAngularVelocity(AZ::u64 entityId, float x, float y, float z)
        {
            Physics::RigidBodyRequestBus::Event(
                AZ::EntityId(entityId), &Physics::RigidBodyRequests::SetAngularVelocity, AZ::Vector3(x, y, z));
        }

        void ApiApplyLinearImpulse(AZ::u64 entityId, float x, float y, float z)
        {
            Physics::RigidBodyRequestBus::Event(
                AZ::EntityId(entityId), &Physics::RigidBodyRequests::ApplyLinearImpulse, AZ::Vector3(x, y, z));
        }

        void ApiApplyAngularImpulse(AZ::u64 entityId, float x, float y, float z)
        {
            Physics::RigidBodyRequestBus::Event(
                AZ::EntityId(entityId), &Physics::RigidBodyRequests::ApplyAngularImpulse, AZ::Vector3(x, y, z));
        }

        float ApiGetMass(AZ::u64 entityId)
        {
            float mass = 0.0f;
            Physics::RigidBodyRequestBus::EventResult(mass, AZ::EntityId(entityId), &Physics::RigidBodyRequests::GetMass);
            return mass;
        }

        void ApiSetGravityEnabled(AZ::u64 entityId, int enabled)
        {
            Physics::RigidBodyRequestBus::Event(
                AZ::EntityId(entityId), &Physics::RigidBodyRequests::SetGravityEnabled, enabled != 0);
        }

        void ApiSetKinematic(AZ::u64 entityId, int kinematic)
        {
            Physics::RigidBodyRequestBus::Event(
                AZ::EntityId(entityId), &Physics::RigidBodyRequests::SetKinematic, kinematic != 0);
        }

        int ApiHasTag(AZ::u64 entityId, const char* tag)
        {
            bool hasTag = false;
            LmbrCentral::TagComponentRequestBus::EventResult(
                hasTag, AZ::EntityId(entityId), &LmbrCentral::TagComponentRequests::HasTag, LmbrCentral::Tag(tag));
            return hasTag ? 1 : 0;
        }

        void ApiAddTag(AZ::u64 entityId, const char* tag)
        {
            LmbrCentral::TagComponentRequestBus::Event(
                AZ::EntityId(entityId), &LmbrCentral::TagComponentRequests::AddTag, LmbrCentral::Tag(tag));
        }

        void ApiRemoveTag(AZ::u64 entityId, const char* tag)
        {
            LmbrCentral::TagComponentRequestBus::Event(
                AZ::EntityId(entityId), &LmbrCentral::TagComponentRequests::RemoveTag, LmbrCentral::Tag(tag));
        }

        AZ::u64 ApiFindEntityByTag(const char* tag)
        {
            AZ::EntityId entityId;
            LmbrCentral::TagGlobalRequestBus::EventResult(
                entityId, LmbrCentral::Tag(tag), &LmbrCentral::TagGlobalRequests::RequestTaggedEntities);
            return static_cast<AZ::u64>(entityId);
        }

        int ApiFindEntitiesByTag(const char* tag, AZ::u64* buffer, int bufferSize)
        {
            AZ::EBusAggregateResults<AZ::EntityId> results;
            LmbrCentral::TagGlobalRequestBus::EventResult(
                results, LmbrCentral::Tag(tag), &LmbrCentral::TagGlobalRequests::RequestTaggedEntities);
            int count = 0;
            for (const AZ::EntityId& entityId : results.values)
            {
                if (count >= bufferSize)
                {
                    break;
                }
                buffer[count++] = static_cast<AZ::u64>(entityId);
            }
            return count;
        }

        struct SpawnRecord
        {
            AzFramework::EntitySpawnTicket m_ticket;
            AZ::EntityId m_rootEntityId;
        };
        AZStd::unordered_map<AZ::u64, SpawnRecord> s_spawnRecords;
        AZ::u64 s_nextSpawnId = 1;

        AZ::u64 ApiSpawnPrefab(const char* spawnablePath, float x, float y, float z)
        {
            AZ::Data::AssetId assetId;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(
                assetId, &AZ::Data::AssetCatalogRequestBus::Events::GetAssetIdByPath,
                spawnablePath, azrtti_typeid<AzFramework::Spawnable>(), false);
            if (!assetId.IsValid())
            {
                AZ_Error("CSharpScripting", false, "Spawnable '%s' not found in the asset catalog.", spawnablePath);
                return 0;
            }

            auto spawnableAsset = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(
                assetId, AZ::Data::AssetLoadBehavior::PreLoad);
            spawnableAsset.BlockUntilLoadComplete();
            if (!spawnableAsset.IsReady())
            {
                AZ_Error("CSharpScripting", false, "Spawnable '%s' failed to load.", spawnablePath);
                return 0;
            }

            auto* spawner = AzFramework::SpawnableEntitiesInterface::Get();
            if (!spawner)
            {
                return 0;
            }

            const AZ::u64 spawnId = s_nextSpawnId++;
            SpawnRecord& record = s_spawnRecords[spawnId];
            record.m_ticket = AzFramework::EntitySpawnTicket(spawnableAsset);

            AzFramework::SpawnAllEntitiesOptionalArgs optionalArgs;
            const AZ::Vector3 translation(x, y, z);
            optionalArgs.m_preInsertionCallback =
                [translation]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableEntityContainerView view)
            {
                for (AZ::Entity* entity : view)
                {
                    if (auto* transformComponent = entity->FindComponent<AzFramework::TransformComponent>())
                    {
                        AZ::Transform worldTm = transformComponent->GetWorldTM();
                        worldTm.SetTranslation(worldTm.GetTranslation() + translation);
                        transformComponent->SetWorldTM(worldTm);
                    }
                }
            };
            optionalArgs.m_completionCallback =
                [spawnId]([[maybe_unused]] AzFramework::EntitySpawnTicket::Id ticketId, AzFramework::SpawnableConstEntityContainerView view)
            {
                auto it = s_spawnRecords.find(spawnId);
                if (it != s_spawnRecords.end() && !view.empty())
                {
                    it->second.m_rootEntityId = (*view.begin())->GetId();
                }
            };
            spawner->SpawnAllEntities(record.m_ticket, AZStd::move(optionalArgs));
            return spawnId;
        }

        AZ::u64 ApiGetSpawnedRoot(AZ::u64 ticketId)
        {
            auto it = s_spawnRecords.find(ticketId);
            return it != s_spawnRecords.end() ? static_cast<AZ::u64>(it->second.m_rootEntityId) : 0;
        }

        void ApiDespawn(AZ::u64 ticketId)
        {
            // Destroying the ticket despawns its entities.
            s_spawnRecords.erase(ticketId);
        }

        bool RunCommand(const AZStd::string& command, AZStd::string& output)
        {
#if defined(AZ_PLATFORM_WINDOWS)
            FILE* pipe = _popen((command + " 2>&1").c_str(), "r");
#else
            FILE* pipe = popen((command + " 2>&1").c_str(), "r");
#endif
            if (!pipe)
            {
                return false;
            }
            char buffer[512];
            while (fgets(buffer, sizeof(buffer), pipe))
            {
                output += buffer;
            }
#if defined(AZ_PLATFORM_WINDOWS)
            const int exitCode = _pclose(pipe);
#else
            const int exitCode = pclose(pipe);
#endif
            return exitCode == 0;
        }

        AZ::u64 NewestSourceStamp(const AZStd::string& directory)
        {
            AZ::u64 newest = 0;
            AZ::IO::SystemFile::FindFiles(
                (directory + "/*").c_str(),
                [&](const char* item, bool isFile)
                {
                    if (item[0] == '.')
                    {
                        return true;
                    }
                    const AZStd::string path = directory + "/" + item;
                    if (isFile)
                    {
                        if (AZ::StringFunc::EndsWith(path, ".cs"))
                        {
                            newest = AZStd::max(newest, AZ::IO::SystemFile::ModificationTime(path.c_str()));
                        }
                    }
                    else
                    {
                        newest = AZStd::max(newest, NewestSourceStamp(path));
                    }
                    return true;
                });
            return newest;
        }
    } // namespace

    ScriptHost& ScriptHost::Instance()
    {
        static ScriptHost instance;
        return instance;
    }

    AZStd::string ScriptHost::GemManagedSourceDir() const
    {
        return AZStd::string(AZ::Utils::GetEnginePath().c_str()) + "/Gems/CSharpScripting/Managed";
    }

    AZStd::string ScriptHost::ProjectScriptsSourceDir() const
    {
        return AZStd::string(AZ::Utils::GetProjectPath().c_str()) + "/Scripts";
    }

    AZStd::string ScriptHost::ManagedWorkDir() const
    {
        return AZStd::string(AZ::Utils::GetProjectPath().c_str()) + "/user/csharp";
    }

    bool ScriptHost::BuildManagedCore()
    {
        const AZStd::string coreDll = ManagedWorkDir() + "/core/AIO3DE.Core.dll";
        const AZStd::string coreSourceDir = GemManagedSourceDir() + "/AIO3DE.Core";
        if (AZ::IO::SystemFile::Exists(coreDll.c_str()) &&
            AZ::IO::SystemFile::ModificationTime(coreDll.c_str()) >= NewestSourceStamp(coreSourceDir))
        {
            return true;
        }

        const AZStd::string dotnet = DotNetHost::FindDotNetCli();
        if (dotnet.empty())
        {
            AZ_Error("CSharpScripting", false,
                "The 'dotnet' CLI was not found. Install the .NET 8 SDK to use C# scripting.");
            return false;
        }

        AZStd::string output;
        const AZStd::string command =
            "\"" + dotnet + "\" build \"" + coreSourceDir + "/AIO3DE.Core.csproj\" -c Release -o \"" + ManagedWorkDir() + "/core\"";
        AZ_Printf("CSharpScripting", "Building managed core: %s", command.c_str());
        if (!RunCommand(command, output))
        {
            AZ_Error("CSharpScripting", false, "AIO3DE.Core build failed:\n%s", output.c_str());
            return false;
        }
        return true;
    }

    bool ScriptHost::InitializeRuntime()
    {
        if (m_bootstrapReady)
        {
            return true;
        }
        if (!BuildManagedCore())
        {
            return false;
        }

        const AZStd::string coreDir = ManagedWorkDir() + "/core";
        const AZStd::string runtimeConfig = coreDir + "/AIO3DE.Core.runtimeconfig.json";
        if (!m_host.Initialize(runtimeConfig))
        {
            return false;
        }

        const AZStd::string coreDll = coreDir + "/AIO3DE.Core.dll";
        const AZStd::string bootstrapType = "AIO3DE.Interop.Bootstrap, AIO3DE.Core";
        m_managedInitialize = reinterpret_cast<int (*)(const NativeApi*)>(m_host.GetFunction(coreDll, bootstrapType, "Initialize"));
        m_managedLoadScripts = reinterpret_cast<int (*)(const char*)>(m_host.GetFunction(coreDll, bootstrapType, "LoadScripts"));
        m_managedCreateScript =
            reinterpret_cast<AZ::s64 (*)(const char*, AZ::u64)>(m_host.GetFunction(coreDll, bootstrapType, "CreateScript"));
        m_managedScriptOnActivate = reinterpret_cast<void (*)(AZ::s64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnActivate"));
        m_managedScriptOnUpdate =
            reinterpret_cast<void (*)(AZ::s64, float)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnUpdate"));
        m_managedScriptOnDeactivate =
            reinterpret_cast<void (*)(AZ::s64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnDeactivate"));
        m_managedDestroyScript = reinterpret_cast<void (*)(AZ::s64)>(m_host.GetFunction(coreDll, bootstrapType, "DestroyScript"));
        m_managedScriptOnCollisionEnter = reinterpret_cast<void (*)(AZ::s64, AZ::u64, float, float, float, float, float, float, float)>(
            m_host.GetFunction(coreDll, bootstrapType, "ScriptOnCollisionEnter"));
        m_managedScriptOnCollisionExit =
            reinterpret_cast<void (*)(AZ::s64, AZ::u64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnCollisionExit"));
        m_managedScriptOnTriggerEnter =
            reinterpret_cast<void (*)(AZ::s64, AZ::u64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnTriggerEnter"));
        m_managedScriptOnTriggerExit =
            reinterpret_cast<void (*)(AZ::s64, AZ::u64)>(m_host.GetFunction(coreDll, bootstrapType, "ScriptOnTriggerExit"));

        if (!m_managedInitialize || !m_managedLoadScripts || !m_managedCreateScript || !m_managedScriptOnActivate ||
            !m_managedScriptOnUpdate || !m_managedScriptOnDeactivate || !m_managedDestroyScript ||
            !m_managedScriptOnCollisionEnter || !m_managedScriptOnCollisionExit ||
            !m_managedScriptOnTriggerEnter || !m_managedScriptOnTriggerExit)
        {
            return false;
        }

        static const NativeApi api = {
            &ApiLog,
            &ApiGetWorldPosition,
            &ApiSetWorldPosition,
            &ApiGetWorldRotationEuler,
            &ApiSetWorldRotationEuler,
            &ApiGetUniformScale,
            &ApiSetUniformScale,
            &ApiFindEntityByName,
            &ApiGetEntityName,
            &ApiGetLocalPosition,
            &ApiSetLocalPosition,
            &ApiGetWorldRotationQuaternion,
            &ApiSetWorldRotationQuaternion,
            &ApiGetWorldBasis,
            &ApiSetParent,
            &ApiGetParent,
            &ApiCreateEntity,
            &ApiDestroyEntity,
            &ApiSetEntityActive,
            &ApiIsEntityActive,
            &ApiIsChannelActive,
            &ApiGetChannelValue,
            &ApiGetCursorPositionNormalized,
            &ApiGetTimeSeconds,
            &ApiRayCast,
            &ApiGetLinearVelocity,
            &ApiSetLinearVelocity,
            &ApiGetAngularVelocity,
            &ApiSetAngularVelocity,
            &ApiApplyLinearImpulse,
            &ApiApplyAngularImpulse,
            &ApiGetMass,
            &ApiSetGravityEnabled,
            &ApiSetKinematic,
            &ApiHasTag,
            &ApiAddTag,
            &ApiRemoveTag,
            &ApiFindEntityByTag,
            &ApiFindEntitiesByTag,
            &ApiSpawnPrefab,
            &ApiGetSpawnedRoot,
            &ApiDespawn,
        };
        if (m_managedInitialize(&api) == 0)
        {
            AZ_Error("CSharpScripting", false, "Managed bootstrap initialization failed.");
            return false;
        }

        m_bootstrapReady = true;
        return true;
    }

    bool ScriptHost::BuildProjectScripts(bool force)
    {
        const AZStd::string scriptsDir = ProjectScriptsSourceDir();
        if (!AZ::IO::SystemFile::Exists(scriptsDir.c_str()))
        {
            AZ_Warning("CSharpScripting", false,
                "No C# scripts folder found at '%s'. Create it and add .cs files deriving from AIO3DE.ScriptComponent.",
                scriptsDir.c_str());
            return false;
        }

        const AZ::u64 stamp = NewestSourceStamp(scriptsDir);
        const AZStd::string scriptsDll = ManagedWorkDir() + "/scripts/bin/ProjectScripts.dll";
        if (!force && m_scriptsLoaded && stamp == m_scriptsSourceStamp && AZ::IO::SystemFile::Exists(scriptsDll.c_str()))
        {
            return true;
        }

        const AZStd::string dotnet = DotNetHost::FindDotNetCli();
        if (dotnet.empty())
        {
            AZ_Error("CSharpScripting", false, "The 'dotnet' CLI was not found. Install the .NET 8 SDK to use C# scripting.");
            return false;
        }

        // Generated project: compiles every .cs under <project>/Scripts against the managed core.
        const AZStd::string projectDir = ManagedWorkDir() + "/scripts";
        AZ::IO::SystemFile::CreateDir(projectDir.c_str());
        const AZStd::string csproj = projectDir + "/ProjectScripts.csproj";
        const AZStd::string csprojContent =
            "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
            "  <PropertyGroup>\n"
            "    <TargetFramework>net8.0</TargetFramework>\n"
            "    <AssemblyName>ProjectScripts</AssemblyName>\n"
            "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
            "    <Nullable>enable</Nullable>\n"
            "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n"
            "  </PropertyGroup>\n"
            "  <ItemGroup>\n"
            "    <Compile Include=\"" + scriptsDir + "/**/*.cs\" />\n"
            "    <Reference Include=\"AIO3DE.Core\">\n"
            "      <HintPath>" + ManagedWorkDir() + "/core/AIO3DE.Core.dll</HintPath>\n"
            "    </Reference>\n"
            "  </ItemGroup>\n"
            "</Project>\n";
        AZ::Utils::WriteFile(csprojContent, csproj);

        AZStd::string output;
        const AZStd::string command = "\"" + dotnet + "\" build \"" + csproj + "\" -c Release -o \"" + projectDir + "/bin\"";
        AZ_Printf("CSharpScripting", "Building project scripts: %s", command.c_str());
        if (!RunCommand(command, output))
        {
            AZ_Error("CSharpScripting", false, "C# script build failed:\n%s", output.c_str());
            return false;
        }

        m_scriptsSourceStamp = stamp;
        m_scriptsLoaded = false;
        return true;
    }

    bool ScriptHost::LoadScriptsAssembly()
    {
        if (m_scriptsLoaded)
        {
            return true;
        }
        const AZStd::string scriptsDll = ManagedWorkDir() + "/scripts/bin/ProjectScripts.dll";
        if (m_managedLoadScripts(scriptsDll.c_str()) == 0)
        {
            AZ_Error("CSharpScripting", false, "Failed to load compiled scripts assembly '%s'.", scriptsDll.c_str());
            return false;
        }
        m_scriptsLoaded = true;
        return true;
    }

    bool ScriptHost::EnsureReady()
    {
        return InitializeRuntime() && BuildProjectScripts(false) && LoadScriptsAssembly();
    }

    bool ScriptHost::RebuildScripts()
    {
        return InitializeRuntime() && BuildProjectScripts(true) && LoadScriptsAssembly();
    }

    AZ::s64 ScriptHost::CreateScript(const AZStd::string& className, AZ::EntityId entityId)
    {
        if (!EnsureReady())
        {
            return 0;
        }
        const AZ::s64 handle = m_managedCreateScript(className.c_str(), static_cast<AZ::u64>(entityId));
        AZ_Error("CSharpScripting", handle != 0,
            "Could not create C# script instance '%s'. Check the class name (namespace-qualified if in a namespace) "
            "and that it derives from AIO3DE.ScriptComponent.",
            className.c_str());
        return handle;
    }

    void ScriptHost::ScriptOnActivate(AZ::s64 handle)
    {
        if (handle != 0 && m_managedScriptOnActivate)
        {
            m_managedScriptOnActivate(handle);
        }
    }

    void ScriptHost::ScriptOnUpdate(AZ::s64 handle, float deltaTime)
    {
        if (handle != 0 && m_managedScriptOnUpdate)
        {
            m_managedScriptOnUpdate(handle, deltaTime);
        }
    }

    void ScriptHost::ScriptOnDeactivate(AZ::s64 handle)
    {
        if (handle != 0 && m_managedScriptOnDeactivate)
        {
            m_managedScriptOnDeactivate(handle);
        }
    }

    void ScriptHost::DestroyScript(AZ::s64 handle)
    {
        if (handle != 0 && m_managedDestroyScript)
        {
            m_managedDestroyScript(handle);
        }
    }

    void ScriptHost::ScriptOnCollisionEnter(
        AZ::s64 handle, AZ::u64 otherEntityId,
        float positionX, float positionY, float positionZ,
        float normalX, float normalY, float normalZ, float impulse)
    {
        if (handle != 0 && m_managedScriptOnCollisionEnter)
        {
            m_managedScriptOnCollisionEnter(handle, otherEntityId, positionX, positionY, positionZ, normalX, normalY, normalZ, impulse);
        }
    }

    void ScriptHost::ScriptOnCollisionExit(AZ::s64 handle, AZ::u64 otherEntityId)
    {
        if (handle != 0 && m_managedScriptOnCollisionExit)
        {
            m_managedScriptOnCollisionExit(handle, otherEntityId);
        }
    }

    void ScriptHost::ScriptOnTriggerEnter(AZ::s64 handle, AZ::u64 otherEntityId)
    {
        if (handle != 0 && m_managedScriptOnTriggerEnter)
        {
            m_managedScriptOnTriggerEnter(handle, otherEntityId);
        }
    }

    void ScriptHost::ScriptOnTriggerExit(AZ::s64 handle, AZ::u64 otherEntityId)
    {
        if (handle != 0 && m_managedScriptOnTriggerExit)
        {
            m_managedScriptOnTriggerExit(handle, otherEntityId);
        }
    }
} // namespace CSharpScripting
