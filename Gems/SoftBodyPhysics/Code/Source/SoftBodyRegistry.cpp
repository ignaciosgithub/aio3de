/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "SoftBodyRegistry.h"

#include <AzCore/std/containers/vector.h>

namespace SoftBodyPhysics
{
    namespace SoftBodyRegistry
    {
        namespace
        {
            struct Entry
            {
                AZ::SoftBody* m_body = nullptr;
                float m_particleRadius = 0.0f;
            };

            AZStd::vector<Entry>& GetEntries()
            {
                static AZStd::vector<Entry> entries;
                return entries;
            }
        } // namespace

        void Register(AZ::SoftBody* body, float particleRadius)
        {
            Unregister(body);
            GetEntries().push_back({ body, particleRadius });
        }

        void Unregister(AZ::SoftBody* body)
        {
            auto& entries = GetEntries();
            for (auto it = entries.begin(); it != entries.end(); ++it)
            {
                if (it->m_body == body)
                {
                    entries.erase(it);
                    return;
                }
            }
        }

        void SolveContacts(AZ::SoftBody* body, float particleRadius, float friction)
        {
            for (Entry& entry : GetEntries())
            {
                if (entry.m_body == body)
                {
                    continue;
                }
                AZ::SoftBody::SolveParticleContacts(
                    body->GetParticles(), particleRadius, entry.m_body->GetParticles(), entry.m_particleRadius, friction);
            }
        }
    } // namespace SoftBodyRegistry
} // namespace SoftBodyPhysics
