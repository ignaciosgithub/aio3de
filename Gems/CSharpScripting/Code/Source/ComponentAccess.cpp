/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ComponentAccess.h"

#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/functional.h>
#include <AzCore/std/string/conversions.h>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace CSharpScripting::ComponentAccess
{
    namespace
    {
        enum class Kind
        {
            None,
            Float,
            Double,
            Bool,
            S8,
            U8,
            S16,
            U16,
            S32,
            U32,
            S64,
            U64,
            String,
            Vector2,
            Vector3,
            Vector4,
            Quaternion,
            Color,
            EntityRef,
        };

        Kind KindOf(const AZ::Uuid& typeId)
        {
            if (typeId == azrtti_typeid<float>()) return Kind::Float;
            if (typeId == azrtti_typeid<double>()) return Kind::Double;
            if (typeId == azrtti_typeid<bool>()) return Kind::Bool;
            if (typeId == azrtti_typeid<AZ::s8>()) return Kind::S8;
            if (typeId == azrtti_typeid<AZ::u8>()) return Kind::U8;
            if (typeId == azrtti_typeid<AZ::s16>()) return Kind::S16;
            if (typeId == azrtti_typeid<AZ::u16>()) return Kind::U16;
            if (typeId == azrtti_typeid<AZ::s32>()) return Kind::S32;
            if (typeId == azrtti_typeid<AZ::u32>()) return Kind::U32;
            if (typeId == azrtti_typeid<AZ::s64>()) return Kind::S64;
            if (typeId == azrtti_typeid<AZ::u64>()) return Kind::U64;
            if (typeId == azrtti_typeid<AZStd::string>()) return Kind::String;
            if (typeId == azrtti_typeid<AZ::Vector2>()) return Kind::Vector2;
            if (typeId == azrtti_typeid<AZ::Vector3>()) return Kind::Vector3;
            if (typeId == azrtti_typeid<AZ::Vector4>()) return Kind::Vector4;
            if (typeId == azrtti_typeid<AZ::Quaternion>()) return Kind::Quaternion;
            if (typeId == azrtti_typeid<AZ::Color>()) return Kind::Color;
            if (typeId == azrtti_typeid<AZ::EntityId>()) return Kind::EntityRef;
            return Kind::None;
        }

        const char* KindName(Kind kind)
        {
            switch (kind)
            {
            case Kind::Float:
            case Kind::Double:
                return "float";
            case Kind::Bool:
                return "bool";
            case Kind::S8:
            case Kind::U8:
            case Kind::S16:
            case Kind::U16:
            case Kind::S32:
            case Kind::U32:
            case Kind::S64:
            case Kind::U64:
                return "int";
            case Kind::String:
                return "string";
            case Kind::Vector2:
                return "vector2";
            case Kind::Vector3:
                return "vector3";
            case Kind::Vector4:
                return "vector4";
            case Kind::Quaternion:
                return "quaternion";
            case Kind::Color:
                return "color";
            case Kind::EntityRef:
                return "entity";
            default:
                return "";
            }
        }

        // Case-insensitive comparison ignoring spaces and underscores, so "LinearDamping",
        // "linear_damping" and the reflected "Linear damping" all match.
        bool NamesMatch(const char* a, const char* b)
        {
            if (!a || !b)
            {
                return false;
            }
            while (true)
            {
                while (*a == ' ' || *a == '_') ++a;
                while (*b == ' ' || *b == '_') ++b;
                if (*a == '\0' || *b == '\0')
                {
                    return *a == *b;
                }
                if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b)))
                {
                    return false;
                }
                ++a;
                ++b;
            }
        }

        bool ContainsNoCase(const char* haystack, const char* needle)
        {
            if (!haystack || !needle)
            {
                return false;
            }
            const size_t n = strlen(needle);
            for (const char* p = haystack; *p; ++p)
            {
                size_t i = 0;
                while (i < n && p[i] && tolower(static_cast<unsigned char>(p[i])) == tolower(static_cast<unsigned char>(needle[i])))
                {
                    ++i;
                }
                if (i == n)
                {
                    return true;
                }
            }
            return false;
        }

        bool ElementMatches(const AZ::SerializeContext::ClassElement* element, const char* name)
        {
            if (!element)
            {
                return false;
            }
            if (NamesMatch(element->m_name, name))
            {
                return true;
            }
            return element->m_editData && NamesMatch(element->m_editData->m_name, name);
        }

        AZStd::vector<AZStd::string> SplitPath(const char* path)
        {
            AZStd::vector<AZStd::string> segments;
            AZStd::string current;
            for (const char* p = path; p && *p; ++p)
            {
                if (*p == '/' || *p == '.')
                {
                    if (!current.empty())
                    {
                        segments.push_back(current);
                        current.clear();
                    }
                }
                else
                {
                    current += *p;
                }
            }
            if (!current.empty())
            {
                segments.push_back(current);
            }
            return segments;
        }

        void* ResolveInstance(void* ptr, const AZ::SerializeContext::ClassElement* element)
        {
            if (element && (element->m_flags & AZ::SerializeContext::ClassElement::FLG_POINTER))
            {
                return *reinterpret_cast<void**>(ptr);
            }
            return ptr;
        }

        AZ::SerializeContext* GetSerializeContext()
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
            return serializeContext;
        }

        bool IsBaseClassElement(const AZ::SerializeContext::ClassElement* element)
        {
            return element && (element->m_flags & AZ::SerializeContext::ClassElement::FLG_BASE_CLASS);
        }

        // AZ::Component's own reflected data (the component id) must never be exposed to scripts.
        bool IsComponentBase(const AZ::SerializeContext::ClassData* classData)
        {
            return classData && classData->m_typeId == azrtti_typeid<AZ::Component>();
        }

        struct Found
        {
            void* m_instance = nullptr;
            Kind m_kind = Kind::None;
        };

        // Walks the reflected leaves of a component. Base classes are transparent (their fields
        // appear as direct members of the derived class). `onLeaf(path segments, ptr, element, kind)`
        // returns false to stop the walk.
        template<typename OnLeaf>
        void WalkLeaves(AZ::Component* component, OnLeaf&& onLeaf)
        {
            AZ::SerializeContext* serializeContext = GetSerializeContext();
            if (!serializeContext || !component)
            {
                return;
            }

            struct Frame
            {
                bool m_countsAsSegment;
            };
            AZStd::vector<Frame> frames;
            AZStd::vector<AZStd::string> path;
            bool keepGoing = true;

            auto begin = [&](void* ptr, const AZ::SerializeContext::ClassData* classData, const AZ::SerializeContext::ClassElement* element) -> bool
            {
                if (!keepGoing)
                {
                    frames.push_back({ false });
                    return false;
                }
                if (!element)
                {
                    frames.push_back({ false }); // component root
                    return true;
                }
                if (IsBaseClassElement(element))
                {
                    frames.push_back({ false });
                    return !IsComponentBase(classData);
                }
                const Kind kind = classData ? KindOf(classData->m_typeId) : Kind::None;
                path.push_back(element->m_name ? element->m_name : "");
                frames.push_back({ true });
                if (kind == Kind::None)
                {
                    return true; // composite: descend
                }
                keepGoing = onLeaf(path, ResolveInstance(ptr, element), element, kind);
                return false;
            };
            auto end = [&]() -> bool
            {
                if (!frames.empty())
                {
                    if (frames.back().m_countsAsSegment && !path.empty())
                    {
                        path.pop_back();
                    }
                    frames.pop_back();
                }
                return keepGoing;
            };

            serializeContext->EnumerateInstance(
                component,
                component->RTTI_GetType(),
                begin,
                end,
                AZ::SerializeContext::ENUM_ACCESS_FOR_READ,
                nullptr,
                nullptr);
        }

        // Matches a requested path against a leaf path. A single segment matches the leaf name at
        // any depth; several segments must match the trailing part of the leaf path in order.
        bool PathMatches(
            const AZStd::vector<AZStd::string>& wanted,
            const AZStd::vector<AZStd::string>& leafPath,
            const AZ::SerializeContext::ClassElement* leafElement)
        {
            if (wanted.empty() || leafPath.size() < wanted.size())
            {
                return false;
            }
            if (!ElementMatches(leafElement, wanted.back().c_str()))
            {
                return false;
            }
            for (size_t i = 1; i < wanted.size(); ++i)
            {
                const AZStd::string& want = wanted[wanted.size() - 1 - i];
                const AZStd::string& have = leafPath[leafPath.size() - 1 - i];
                if (!NamesMatch(have.c_str(), want.c_str()))
                {
                    return false;
                }
            }
            return true;
        }

        Found FindProperty(AZ::Component* component, const char* propertyPath)
        {
            Found found;
            if (!propertyPath || propertyPath[0] == '\0')
            {
                return found;
            }
            const AZStd::vector<AZStd::string> segments = SplitPath(propertyPath);
            if (segments.empty())
            {
                return found;
            }
            WalkLeaves(
                component,
                [&](const AZStd::vector<AZStd::string>& path, void* instance, const AZ::SerializeContext::ClassElement* element, Kind kind)
                {
                    if (!instance || !PathMatches(segments, path, element))
                    {
                        return true;
                    }
                    found.m_instance = instance;
                    found.m_kind = kind;
                    return false;
                });
            return found;
        }

        AZStd::string FormatFloats(const float* values, int count)
        {
            AZStd::string result;
            for (int i = 0; i < count; ++i)
            {
                if (i > 0)
                {
                    result += ' ';
                }
                result += AZStd::string::format("%.9g", values[i]);
            }
            return result;
        }

        int ParseFloats(const char* text, float* values, int count)
        {
            int parsed = 0;
            const char* p = text;
            while (parsed < count && p && *p)
            {
                char* endPtr = nullptr;
                const float value = strtof(p, &endPtr);
                if (endPtr == p)
                {
                    break;
                }
                values[parsed++] = value;
                p = endPtr;
                while (*p == ' ' || *p == ',')
                {
                    ++p;
                }
            }
            return parsed;
        }

        AZStd::string ReadValue(const Found& found)
        {
            switch (found.m_kind)
            {
            case Kind::Float:
                return AZStd::string::format("%.9g", *static_cast<float*>(found.m_instance));
            case Kind::Double:
                return AZStd::string::format("%.17g", *static_cast<double*>(found.m_instance));
            case Kind::Bool:
                return *static_cast<bool*>(found.m_instance) ? "true" : "false";
            case Kind::S8:
                return AZStd::string::format("%d", *static_cast<AZ::s8*>(found.m_instance));
            case Kind::U8:
                return AZStd::string::format("%u", *static_cast<AZ::u8*>(found.m_instance));
            case Kind::S16:
                return AZStd::string::format("%d", *static_cast<AZ::s16*>(found.m_instance));
            case Kind::U16:
                return AZStd::string::format("%u", *static_cast<AZ::u16*>(found.m_instance));
            case Kind::S32:
                return AZStd::string::format("%d", *static_cast<AZ::s32*>(found.m_instance));
            case Kind::U32:
                return AZStd::string::format("%u", *static_cast<AZ::u32*>(found.m_instance));
            case Kind::S64:
                return AZStd::string::format("%lld", static_cast<long long>(*static_cast<AZ::s64*>(found.m_instance)));
            case Kind::U64:
                return AZStd::string::format("%llu", static_cast<unsigned long long>(*static_cast<AZ::u64*>(found.m_instance)));
            case Kind::String:
                return *static_cast<AZStd::string*>(found.m_instance);
            case Kind::Vector2:
            {
                const AZ::Vector2& v = *static_cast<AZ::Vector2*>(found.m_instance);
                const float values[2] = { v.GetX(), v.GetY() };
                return FormatFloats(values, 2);
            }
            case Kind::Vector3:
            {
                const AZ::Vector3& v = *static_cast<AZ::Vector3*>(found.m_instance);
                const float values[3] = { v.GetX(), v.GetY(), v.GetZ() };
                return FormatFloats(values, 3);
            }
            case Kind::Vector4:
            {
                const AZ::Vector4& v = *static_cast<AZ::Vector4*>(found.m_instance);
                const float values[4] = { v.GetX(), v.GetY(), v.GetZ(), v.GetW() };
                return FormatFloats(values, 4);
            }
            case Kind::Quaternion:
            {
                const AZ::Quaternion& q = *static_cast<AZ::Quaternion*>(found.m_instance);
                const float values[4] = { q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
                return FormatFloats(values, 4);
            }
            case Kind::Color:
            {
                const AZ::Color& c = *static_cast<AZ::Color*>(found.m_instance);
                const float values[4] = { c.GetR(), c.GetG(), c.GetB(), c.GetA() };
                return FormatFloats(values, 4);
            }
            case Kind::EntityRef:
                return AZStd::string::format(
                    "%llu", static_cast<unsigned long long>(static_cast<AZ::u64>(*static_cast<AZ::EntityId*>(found.m_instance))));
            default:
                return {};
            }
        }

        bool IsBlank(const char* p)
        {
            while (p && *p)
            {
                if (!isspace(static_cast<unsigned char>(*p)))
                {
                    return false;
                }
                ++p;
            }
            return true;
        }

        bool ParseDouble(const char* text, double& out)
        {
            if (IsBlank(text))
            {
                return false;
            }
            char* end = nullptr;
            out = strtod(text, &end);
            return end != text && IsBlank(end);
        }

        bool ParseSigned(const char* text, long long minValue, long long maxValue, long long& out)
        {
            if (IsBlank(text))
            {
                return false;
            }
            char* end = nullptr;
            errno = 0;
            out = strtoll(text, &end, 10);
            return end != text && IsBlank(end) && errno == 0 && out >= minValue && out <= maxValue;
        }

        bool ParseUnsigned(const char* text, unsigned long long maxValue, unsigned long long& out)
        {
            if (IsBlank(text))
            {
                return false;
            }
            const char* p = text;
            while (isspace(static_cast<unsigned char>(*p)))
            {
                ++p;
            }
            if (*p == '-')
            {
                return false;
            }
            char* end = nullptr;
            errno = 0;
            out = strtoull(p, &end, 10);
            return end != p && IsBlank(end) && errno == 0 && out <= maxValue;
        }

        template<typename T>
        bool WriteSigned(void* instance, const char* value)
        {
            long long parsed = 0;
            if (!ParseSigned(value, std::numeric_limits<T>::min(), std::numeric_limits<T>::max(), parsed))
            {
                return false;
            }
            *static_cast<T*>(instance) = static_cast<T>(parsed);
            return true;
        }

        template<typename T>
        bool WriteUnsigned(void* instance, const char* value)
        {
            unsigned long long parsed = 0;
            if (!ParseUnsigned(value, std::numeric_limits<T>::max(), parsed))
            {
                return false;
            }
            *static_cast<T*>(instance) = static_cast<T>(parsed);
            return true;
        }

        bool WriteValue(const Found& found, const char* value)
        {
            if (!value)
            {
                return false;
            }
            float floats[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            double number = 0.0;
            switch (found.m_kind)
            {
            case Kind::Float:
                if (!ParseDouble(value, number)) return false;
                *static_cast<float*>(found.m_instance) = static_cast<float>(number);
                return true;
            case Kind::Double:
                if (!ParseDouble(value, number)) return false;
                *static_cast<double*>(found.m_instance) = number;
                return true;
            case Kind::Bool:
                if (NamesMatch(value, "true") || strcmp(value, "1") == 0 || NamesMatch(value, "yes"))
                {
                    *static_cast<bool*>(found.m_instance) = true;
                    return true;
                }
                if (NamesMatch(value, "false") || strcmp(value, "0") == 0 || NamesMatch(value, "no"))
                {
                    *static_cast<bool*>(found.m_instance) = false;
                    return true;
                }
                return false;
            case Kind::S8:
                return WriteSigned<AZ::s8>(found.m_instance, value);
            case Kind::U8:
                return WriteUnsigned<AZ::u8>(found.m_instance, value);
            case Kind::S16:
                return WriteSigned<AZ::s16>(found.m_instance, value);
            case Kind::U16:
                return WriteUnsigned<AZ::u16>(found.m_instance, value);
            case Kind::S32:
                return WriteSigned<AZ::s32>(found.m_instance, value);
            case Kind::U32:
                return WriteUnsigned<AZ::u32>(found.m_instance, value);
            case Kind::S64:
                return WriteSigned<AZ::s64>(found.m_instance, value);
            case Kind::U64:
                return WriteUnsigned<AZ::u64>(found.m_instance, value);
            case Kind::String:
                *static_cast<AZStd::string*>(found.m_instance) = value;
                return true;
            case Kind::Vector2:
                if (ParseFloats(value, floats, 2) != 2) return false;
                static_cast<AZ::Vector2*>(found.m_instance)->Set(floats[0], floats[1]);
                return true;
            case Kind::Vector3:
                if (ParseFloats(value, floats, 3) != 3) return false;
                static_cast<AZ::Vector3*>(found.m_instance)->Set(floats[0], floats[1], floats[2]);
                return true;
            case Kind::Vector4:
                if (ParseFloats(value, floats, 4) != 4) return false;
                static_cast<AZ::Vector4*>(found.m_instance)->Set(floats[0], floats[1], floats[2], floats[3]);
                return true;
            case Kind::Quaternion:
                if (ParseFloats(value, floats, 4) != 4) return false;
                static_cast<AZ::Quaternion*>(found.m_instance)->Set(floats[0], floats[1], floats[2], floats[3]);
                return true;
            case Kind::Color:
            {
                const int parsed = ParseFloats(value, floats, 4);
                if (parsed < 3) return false;
                static_cast<AZ::Color*>(found.m_instance)->Set(floats[0], floats[1], floats[2], parsed == 4 ? floats[3] : 1.0f);
                return true;
            }
            case Kind::EntityRef:
            {
                unsigned long long id = 0;
                if (!ParseUnsigned(value, std::numeric_limits<AZ::u64>::max(), id)) return false;
                *static_cast<AZ::EntityId*>(found.m_instance) = AZ::EntityId(static_cast<AZ::u64>(id));
                return true;
            }
            default:
                return false;
            }
        }

        const AZ::SerializeContext::ClassData* FindComponentClass(const char* typeName)
        {
            AZ::SerializeContext* serializeContext = GetSerializeContext();
            if (!serializeContext || !typeName || typeName[0] == '\0')
            {
                return nullptr;
            }
            const AZStd::string withSuffix = AZStd::string(typeName) + "Component";
            const AZ::SerializeContext::ClassData* exact = nullptr;
            const AZ::SerializeContext::ClassData* suffixed = nullptr;
            const AZ::SerializeContext::ClassData* partial = nullptr;
            serializeContext->EnumerateAll(
                [&](const AZ::SerializeContext::ClassData* classData, const AZ::Uuid&) -> bool
                {
                    if (!classData->m_azRtti || !classData->m_azRtti->IsTypeOf(azrtti_typeid<AZ::Component>()) ||
                        classData->m_azRtti->IsAbstract())
                    {
                        return true;
                    }
                    // Editor components are not usable on runtime entities.
                    if (ContainsNoCase(classData->m_name, "Editor"))
                    {
                        return true;
                    }
                    if (NamesMatch(classData->m_name, typeName))
                    {
                        exact = classData;
                        return false;
                    }
                    if (!suffixed && NamesMatch(classData->m_name, withSuffix.c_str()))
                    {
                        suffixed = classData;
                    }
                    else if (!partial && ContainsNoCase(classData->m_name, typeName))
                    {
                        partial = classData;
                    }
                    return true;
                });
            return exact ? exact : (suffixed ? suffixed : partial);
        }

        // Runs `fn` with the entity deactivated, then reactivates it. `fn` returns a rollback
        // that is invoked if the modified entity could no longer activate (unsatisfied or
        // incompatible component services), so a bad add/remove never leaves the entity dead.
        // Returns true when the change was kept.
        template<typename Fn>
        bool WithEntityInactive(AZ::EntityId entityId, Fn&& fn)
        {
            AZ::Entity* entity = FindEntity(entityId);
            if (!entity)
            {
                return false;
            }
            const bool wasActive = entity->GetState() == AZ::Entity::State::Active;
            if (!wasActive && entity->GetState() != AZ::Entity::State::Init)
            {
                return false;
            }
            if (wasActive)
            {
                entity->Deactivate();
            }
            AZStd::function<void()> rollback = fn(entity);
            bool kept = true;
            const AZ::Entity::DependencySortOutcome outcome = entity->EvaluateDependenciesGetDetails();
            if (!outcome.IsSuccess())
            {
                AZ_Warning(
                    "CSharpScripting", false, "Component change on entity '%s' reverted: %s", entity->GetName().c_str(),
                    outcome.GetError().m_message.c_str());
                if (rollback)
                {
                    rollback();
                }
                kept = false;
            }
            if (wasActive)
            {
                entity->Activate();
            }
            return kept;
        }
    } // namespace

    AZ::Entity* FindEntity(AZ::EntityId entityId)
    {
        AZ::Entity* entity = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationBus::Events::FindEntity, entityId);
        return entity;
    }

    AZ::Component* FindComponent(AZ::Entity* entity, const char* typeName)
    {
        if (!entity || !typeName || typeName[0] == '\0')
        {
            return nullptr;
        }
        AZ::Component* partial = nullptr;
        for (AZ::Component* component : entity->GetComponents())
        {
            const char* name = component->RTTI_GetTypeName();
            if (NamesMatch(name, typeName))
            {
                return component;
            }
            if (!partial && ContainsNoCase(name, typeName))
            {
                partial = component;
            }
        }
        return partial;
    }

    AZStd::string ListComponents(AZ::Entity* entity)
    {
        AZStd::string result;
        if (!entity)
        {
            return result;
        }
        for (const AZ::Component* component : entity->GetComponents())
        {
            if (!result.empty())
            {
                result += RecordSeparator;
            }
            result += component->RTTI_GetTypeName();
        }
        return result;
    }

    AZStd::string ListProperties(AZ::Component* component)
    {
        AZStd::string result;
        WalkLeaves(
            component,
            [&](const AZStd::vector<AZStd::string>& path, void*, const AZ::SerializeContext::ClassElement*, Kind kind)
            {
                if (!result.empty())
                {
                    result += RecordSeparator;
                }
                for (size_t i = 0; i < path.size(); ++i)
                {
                    if (i > 0)
                    {
                        result += '/';
                    }
                    result += path[i];
                }
                result += '|';
                result += KindName(kind);
                return true;
            });
        return result;
    }

    AZStd::string GetProperty(AZ::Component* component, const char* propertyPath, AZStd::string& outValue)
    {
        const Found found = FindProperty(component, propertyPath);
        if (found.m_kind == Kind::None)
        {
            outValue.clear();
            return {};
        }
        outValue = ReadValue(found);
        return KindName(found.m_kind);
    }

    bool SetProperty(AZ::Component* component, const char* propertyPath, const char* value)
    {
        const Found found = FindProperty(component, propertyPath);
        if (found.m_kind == Kind::None)
        {
            return false;
        }
        return WriteValue(found, value);
    }

    bool QueueAddComponent(AZ::EntityId entityId, const char* typeName)
    {
        const AZ::SerializeContext::ClassData* classData = FindComponentClass(typeName);
        if (!classData)
        {
            AZ_Warning("CSharpScripting", false, "AddComponent: no component type matches '%s'.", typeName ? typeName : "");
            return false;
        }
        const AZ::Uuid typeId = classData->m_typeId;
        AZ::TickBus::QueueFunction(
            [entityId, typeId]()
            {
                WithEntityInactive(
                    entityId,
                    [&](AZ::Entity* entity) -> AZStd::function<void()>
                    {
                        AZ::Component* component = entity->CreateComponent(typeId);
                        if (!component)
                        {
                            AZ_Warning("CSharpScripting", false, "AddComponent failed on entity '%s'.", entity->GetName().c_str());
                            return {};
                        }
                        return [entity, component]()
                        {
                            if (entity->RemoveComponent(component))
                            {
                                delete component;
                            }
                        };
                    });
            });
        return true;
    }

    bool QueueRemoveComponent(AZ::EntityId entityId, const char* typeName)
    {
        AZ::Entity* entity = FindEntity(entityId);
        AZ::Component* component = FindComponent(entity, typeName);
        if (!component)
        {
            return false;
        }
        const AZ::ComponentId componentId = component->GetId();
        AZ::TickBus::QueueFunction(
            [entityId, componentId]()
            {
                AZ::Component* removed = nullptr;
                const bool kept = WithEntityInactive(
                    entityId,
                    [&](AZ::Entity* liveEntity) -> AZStd::function<void()>
                    {
                        AZ::Component* liveComponent = liveEntity->FindComponent(componentId);
                        if (!liveComponent || !liveEntity->RemoveComponent(liveComponent))
                        {
                            return {};
                        }
                        removed = liveComponent;
                        return [liveEntity, liveComponent]()
                        {
                            liveEntity->AddComponent(liveComponent);
                        };
                    });
                if (kept && removed)
                {
                    delete removed;
                }
            });
        return true;
    }

    void QueueReactivate(AZ::EntityId entityId)
    {
        AZ::TickBus::QueueFunction(
            [entityId]()
            {
                WithEntityInactive(entityId, [](AZ::Entity*) -> AZStd::function<void()> { return {}; });
            });
    }
} // namespace CSharpScripting::ComponentAccess
