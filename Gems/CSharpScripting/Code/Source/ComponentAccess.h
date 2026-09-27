/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/std/string/string.h>

namespace AZ
{
    class Component;
    class Entity;
}

namespace CSharpScripting
{
    //! Reflection-driven access to any component on an entity: find components by (partial) type name,
    //! list/read/write their serialized properties, and add/remove components by type name.
    //! Values are exchanged as strings in the same format ScriptField uses
    //! (floats as text, "x y z" for vectors, "true"/"false", entity ids as decimal).
    namespace ComponentAccess
    {
        //! Records separated by this character in list results.
        constexpr char RecordSeparator = '\x1f';

        AZ::Entity* FindEntity(AZ::EntityId entityId);

        //! Case-insensitive substring match on the component's type name; exact (case-insensitive) matches win.
        AZ::Component* FindComponent(AZ::Entity* entity, const char* typeName);

        //! Type names of all components on the entity, RecordSeparator-separated.
        AZStd::string ListComponents(AZ::Entity* entity);

        //! "path|type" records for every supported leaf property of the component, RecordSeparator-separated.
        //! Paths use '/' between nested elements and the serialized field names (e.g. "RigidBodyConfiguration/Mass").
        AZStd::string ListProperties(AZ::Component* component);

        //! Reads a property. Path is either a single name matched at any depth ("Mass") or a '/'-separated path.
        //! Returns the type name ("float", "int", "bool", "string", "vector2", "vector3", "vector4",
        //! "quaternion", "color", "entity") or empty when not found.
        AZStd::string GetProperty(AZ::Component* component, const char* propertyPath, AZStd::string& outValue);

        //! Writes a property from its string form. Returns false when not found or the value can't be parsed.
        bool SetProperty(AZ::Component* component, const char* propertyPath, const char* value);

        //! Queues adding a component (by exact or partial type name) for the next tick; the entity is
        //! deactivated and reactivated to apply it. Returns false if no such component type is reflected.
        bool QueueAddComponent(AZ::EntityId entityId, const char* typeName);

        //! Queues removal of the first component matching typeName for the next tick.
        bool QueueRemoveComponent(AZ::EntityId entityId, const char* typeName);

        //! Queues a deactivate/activate cycle of the entity for the next tick so components re-read their configuration.
        void QueueReactivate(AZ::EntityId entityId);
    } // namespace ComponentAccess
} // namespace CSharpScripting
