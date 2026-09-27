/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Component/ComponentApplication.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzCore/std/string/string.h>
#include <AzTest/AzTest.h>

#include <ComponentAccess.h>

namespace CSharpScripting::Tests
{
    using namespace CSharpScripting::ComponentAccess;

    class BaseConfig
    {
    public:
        AZ_RTTI(BaseConfig, "{6B4A3F1E-0C4B-4E9A-9E7D-0F8B1A2C3D4E}");
        virtual ~BaseConfig() = default;

        float m_baseSpeed = 1.5f;

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
            {
                sc->Class<BaseConfig>()->Version(1)->Field("Base speed", &BaseConfig::m_baseSpeed);
            }
        }
    };

    class TestConfig : public BaseConfig
    {
    public:
        AZ_RTTI(TestConfig, "{2E7C9D1A-5F3B-4A8C-B1D2-9E0F7A6B5C4D}", BaseConfig);

        AZ::Vector3 m_dimensions = AZ::Vector3(1.0f, 2.0f, 3.0f);
        bool m_enabled = true;
        AZStd::string m_label = "hello";
        AZ::EntityId m_target;
        AZ::Quaternion m_orientation = AZ::Quaternion::CreateIdentity();
        AZ::u32 m_count = 7;

        static void Reflect(AZ::ReflectContext* context)
        {
            BaseConfig::Reflect(context);
            if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
            {
                sc->Class<TestConfig, BaseConfig>()
                    ->Version(1)
                    ->Field("Dimensions", &TestConfig::m_dimensions)
                    ->Field("Enabled", &TestConfig::m_enabled)
                    ->Field("Label", &TestConfig::m_label)
                    ->Field("Target", &TestConfig::m_target)
                    ->Field("Orientation", &TestConfig::m_orientation)
                    ->Field("Count", &TestConfig::m_count);
            }
        }
    };

    class TestComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(TestComponent, "{A1B2C3D4-E5F6-4718-9A0B-1C2D3E4F5A6B}");

        TestConfig m_config;
        float m_damping = 0.25f;
        int m_activations = 0;

        static void Reflect(AZ::ReflectContext* context)
        {
            TestConfig::Reflect(context);
            if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
            {
                sc->Class<TestComponent, AZ::Component>()
                    ->Version(1)
                    ->Field("Config", &TestComponent::m_config)
                    ->Field("Linear damping", &TestComponent::m_damping);
            }
        }

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
        {
            provided.push_back(AZ_CRC_CE("TestService"));
        }

        void Activate() override
        {
            ++m_activations;
        }
        void Deactivate() override
        {
        }
    };

    class OtherComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(OtherComponent, "{B2C3D4E5-F6A7-4829-8B1C-2D3E4F5A6B7C}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
            {
                sc->Class<OtherComponent, AZ::Component>()->Version(1);
            }
        }

        void Activate() override
        {
        }
        void Deactivate() override
        {
        }
    };

    // Cannot coexist with TestComponent's service: used to verify add-rollback.
    class ConflictingComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(ConflictingComponent, "{C3D4E5F6-A7B8-493A-9C2D-3E4F5A6B7C8D}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
            {
                sc->Class<ConflictingComponent, AZ::Component>()->Version(1);
            }
        }

        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
        {
            incompatible.push_back(AZ_CRC_CE("TestService"));
        }

        void Activate() override
        {
        }
        void Deactivate() override
        {
        }
    };

    // Needs TestComponent's service: used to verify remove-rollback.
    class DependentComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(DependentComponent, "{D4E5F6A7-B8C9-4A4B-8D3E-4F5A6B7C8D9E}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* sc = azrtti_cast<AZ::SerializeContext*>(context))
            {
                sc->Class<DependentComponent, AZ::Component>()->Version(1);
            }
        }

        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
        {
            required.push_back(AZ_CRC_CE("TestService"));
        }

        void Activate() override
        {
        }
        void Deactivate() override
        {
        }
    };

    class ComponentAccessFixture : public UnitTest::LeakDetectionFixture
    {
    protected:
        void SetUp() override
        {
            UnitTest::LeakDetectionFixture::SetUp();
            m_app = AZStd::make_unique<AZ::ComponentApplication>();
            AZ::ComponentApplication::Descriptor desc;
            AZ::ComponentApplication::StartupParameters startup;
            startup.m_loadSettingsRegistry = false;
            m_app->Create(desc, startup);
            m_app->RegisterComponentDescriptor(TestComponent::CreateDescriptor());
            m_app->RegisterComponentDescriptor(OtherComponent::CreateDescriptor());
            m_app->RegisterComponentDescriptor(ConflictingComponent::CreateDescriptor());
            m_app->RegisterComponentDescriptor(DependentComponent::CreateDescriptor());

            m_entity = aznew AZ::Entity("Subject");
            m_component = m_entity->CreateComponent<TestComponent>();
            m_entity->Init();
            m_entity->Activate();
        }

        void TearDown() override
        {
            delete m_entity;
            m_entity = nullptr;
            m_app->Destroy();
            m_app.reset();
            UnitTest::LeakDetectionFixture::TearDown();
        }

        static AZStd::vector<AZStd::string> Split(const AZStd::string& records)
        {
            AZStd::vector<AZStd::string> out;
            size_t start = 0;
            while (start <= records.size())
            {
                const size_t end = records.find(RecordSeparator, start);
                if (end == AZStd::string::npos)
                {
                    if (start < records.size())
                    {
                        out.push_back(records.substr(start));
                    }
                    break;
                }
                out.push_back(records.substr(start, end - start));
                start = end + 1;
            }
            return out;
        }

        static bool Contains(const AZStd::vector<AZStd::string>& list, const char* value)
        {
            for (const AZStd::string& s : list)
            {
                if (s == value)
                {
                    return true;
                }
            }
            return false;
        }

        AZStd::unique_ptr<AZ::ComponentApplication> m_app;
        AZ::Entity* m_entity = nullptr;
        TestComponent* m_component = nullptr;
    };

    TEST_F(ComponentAccessFixture, FindEntityAndComponent_NullSafe)
    {
        EXPECT_EQ(FindEntity(m_entity->GetId()), m_entity);
        EXPECT_EQ(FindEntity(AZ::EntityId()), nullptr);
        EXPECT_EQ(FindEntity(AZ::EntityId(0xDEADBEEF)), nullptr);

        EXPECT_EQ(FindComponent(nullptr, "TestComponent"), nullptr);
        EXPECT_EQ(FindComponent(m_entity, nullptr), nullptr);
        EXPECT_EQ(FindComponent(m_entity, ""), nullptr);
        EXPECT_EQ(FindComponent(m_entity, "Nope"), nullptr);

        EXPECT_EQ(FindComponent(m_entity, "TestComponent"), m_component);
        EXPECT_EQ(FindComponent(m_entity, "testcomponent"), m_component);
        EXPECT_EQ(FindComponent(m_entity, "test_component"), m_component);
        EXPECT_EQ(FindComponent(m_entity, "Test"), m_component); // partial
    }

    TEST_F(ComponentAccessFixture, ListComponents)
    {
        EXPECT_TRUE(ListComponents(nullptr).empty());
        const auto names = Split(ListComponents(m_entity));
        ASSERT_EQ(names.size(), 1u);
        EXPECT_EQ(names[0], "TestComponent");
    }

    TEST_F(ComponentAccessFixture, ListProperties_FlattensBaseClassesAndHidesComponentId)
    {
        EXPECT_TRUE(ListProperties(nullptr).empty());
        const auto props = Split(ListProperties(m_component));

        EXPECT_TRUE(Contains(props, "Linear damping|float"));
        EXPECT_TRUE(Contains(props, "Config/Dimensions|vector3"));
        EXPECT_TRUE(Contains(props, "Config/Enabled|bool"));
        EXPECT_TRUE(Contains(props, "Config/Label|string"));
        EXPECT_TRUE(Contains(props, "Config/Target|entity"));
        EXPECT_TRUE(Contains(props, "Config/Orientation|quaternion"));
        EXPECT_TRUE(Contains(props, "Config/Count|int"));
        // Inherited field appears as a direct member of Config, no "BaseClass1" segment.
        EXPECT_TRUE(Contains(props, "Config/Base speed|float"));
        for (const AZStd::string& p : props)
        {
            EXPECT_EQ(p.find("BaseClass"), AZStd::string::npos) << p.c_str();
            EXPECT_NE(p, "Id|int");
        }
        EXPECT_EQ(props.size(), 8u);
    }

    TEST_F(ComponentAccessFixture, GetProperty_NameMatchingAndPaths)
    {
        AZStd::string value;
        EXPECT_EQ(GetProperty(m_component, "Linear damping", value), "float");
        EXPECT_EQ(value, "0.25");
        EXPECT_EQ(GetProperty(m_component, "LinearDamping", value), "float");
        EXPECT_EQ(GetProperty(m_component, "linear_damping", value), "float");

        // Short name matches at any depth; explicit paths with '/' or '.' work; inherited fields resolve.
        EXPECT_EQ(GetProperty(m_component, "Dimensions", value), "vector3");
        EXPECT_EQ(value, "1 2 3");
        EXPECT_EQ(GetProperty(m_component, "Config/Dimensions", value), "vector3");
        EXPECT_EQ(GetProperty(m_component, "config.dimensions", value), "vector3");
        EXPECT_EQ(GetProperty(m_component, "BaseSpeed", value), "float");
        EXPECT_EQ(value, "1.5");
        EXPECT_EQ(GetProperty(m_component, "Config/Base speed", value), "float");
        EXPECT_EQ(GetProperty(m_component, "Enabled", value), "bool");
        EXPECT_EQ(value, "true");
        EXPECT_EQ(GetProperty(m_component, "Label", value), "string");
        EXPECT_EQ(value, "hello");
        EXPECT_EQ(GetProperty(m_component, "Count", value), "int");
        EXPECT_EQ(value, "7");
        EXPECT_EQ(GetProperty(m_component, "Orientation", value), "quaternion");
        EXPECT_EQ(value, "0 0 0 1");

        // Misses.
        EXPECT_TRUE(GetProperty(m_component, "Missing", value).empty());
        EXPECT_TRUE(GetProperty(m_component, "Wrong/Dimensions", value).empty());
        EXPECT_TRUE(GetProperty(m_component, "Id", value).empty()); // AZ::Component base hidden
        EXPECT_TRUE(GetProperty(m_component, "", value).empty());
        EXPECT_TRUE(GetProperty(m_component, nullptr, value).empty());
        EXPECT_TRUE(GetProperty(nullptr, "Dimensions", value).empty());
    }

    TEST_F(ComponentAccessFixture, SetProperty_TypedConversions)
    {
        EXPECT_TRUE(SetProperty(m_component, "Linear damping", "0.75"));
        EXPECT_FLOAT_EQ(m_component->m_damping, 0.75f);

        EXPECT_TRUE(SetProperty(m_component, "Dimensions", "4,5,6"));
        EXPECT_TRUE(m_component->m_config.m_dimensions.IsClose(AZ::Vector3(4.0f, 5.0f, 6.0f)));

        EXPECT_TRUE(SetProperty(m_component, "Enabled", "false"));
        EXPECT_FALSE(m_component->m_config.m_enabled);
        EXPECT_TRUE(SetProperty(m_component, "Enabled", "1"));
        EXPECT_TRUE(m_component->m_config.m_enabled);

        EXPECT_TRUE(SetProperty(m_component, "Label", "world"));
        EXPECT_EQ(m_component->m_config.m_label, "world");

        EXPECT_TRUE(SetProperty(m_component, "Count", "42"));
        EXPECT_EQ(m_component->m_config.m_count, 42u);

        EXPECT_TRUE(SetProperty(m_component, "Base speed", "9.5"));
        EXPECT_FLOAT_EQ(m_component->m_config.m_baseSpeed, 9.5f);

        const AZStd::string id = AZStd::string::format("%llu", static_cast<unsigned long long>(m_entity->GetId()));
        EXPECT_TRUE(SetProperty(m_component, "Target", id.c_str()));
        EXPECT_EQ(m_component->m_config.m_target, m_entity->GetId());

        EXPECT_TRUE(SetProperty(m_component, "Orientation", "0,0,0.7071068,0.7071068"));
        EXPECT_TRUE(m_component->m_config.m_orientation.IsClose(AZ::Quaternion(0.0f, 0.0f, 0.7071068f, 0.7071068f)));
    }

    TEST_F(ComponentAccessFixture, SetProperty_RejectsInvalidInput)
    {
        EXPECT_FALSE(SetProperty(m_component, "Linear damping", "not a number"));
        EXPECT_FLOAT_EQ(m_component->m_damping, 0.25f);
        EXPECT_FALSE(SetProperty(m_component, "Dimensions", "1,2"));
        EXPECT_TRUE(m_component->m_config.m_dimensions.IsClose(AZ::Vector3(1.0f, 2.0f, 3.0f)));
        EXPECT_FALSE(SetProperty(m_component, "Count", "-1"));
        EXPECT_EQ(m_component->m_config.m_count, 7u);
        EXPECT_FALSE(SetProperty(m_component, "Enabled", "maybe"));
        EXPECT_FALSE(SetProperty(m_component, "Missing", "1"));
        EXPECT_FALSE(SetProperty(m_component, "Id", "5"));
        EXPECT_FALSE(SetProperty(m_component, nullptr, "1"));
        EXPECT_FALSE(SetProperty(m_component, "Linear damping", nullptr));
        EXPECT_FALSE(SetProperty(nullptr, "Linear damping", "1"));
    }

    TEST_F(ComponentAccessFixture, AddRemoveComponent_QueuedAndReactivates)
    {
        EXPECT_FALSE(QueueAddComponent(m_entity->GetId(), "NoSuchComponentXYZ"));
        EXPECT_FALSE(QueueAddComponent(m_entity->GetId(), nullptr));

        const int activationsBefore = m_component->m_activations;
        EXPECT_TRUE(QueueAddComponent(m_entity->GetId(), "OtherComponent"));
        EXPECT_EQ(m_entity->FindComponent<OtherComponent>(), nullptr); // deferred
        AZ::TickBus::ExecuteQueuedEvents();
        EXPECT_NE(m_entity->FindComponent<OtherComponent>(), nullptr);
        EXPECT_EQ(m_entity->GetState(), AZ::Entity::State::Active);
        EXPECT_EQ(m_component->m_activations, activationsBefore + 1);

        // "Other" resolves via the "<name>Component" convention.
        EXPECT_TRUE(QueueRemoveComponent(m_entity->GetId(), "Other"));
        AZ::TickBus::ExecuteQueuedEvents();
        EXPECT_EQ(m_entity->FindComponent<OtherComponent>(), nullptr);
        EXPECT_EQ(m_entity->GetState(), AZ::Entity::State::Active);

        EXPECT_FALSE(QueueRemoveComponent(m_entity->GetId(), "OtherComponent")); // not present
        EXPECT_FALSE(QueueRemoveComponent(AZ::EntityId(), "TestComponent"));
    }

    TEST_F(ComponentAccessFixture, AddComponent_RevertsWhenEntityCannotActivate)
    {
        AZ_TEST_START_TRACE_SUPPRESSION;
        EXPECT_TRUE(QueueAddComponent(m_entity->GetId(), "ConflictingComponent"));
        AZ::TickBus::ExecuteQueuedEvents();
        AZ_TEST_STOP_TRACE_SUPPRESSION_NO_COUNT;

        EXPECT_EQ(m_entity->FindComponent<ConflictingComponent>(), nullptr);
        EXPECT_EQ(m_entity->GetState(), AZ::Entity::State::Active);
        EXPECT_NE(m_entity->FindComponent<TestComponent>(), nullptr);
    }

    TEST_F(ComponentAccessFixture, RemoveComponent_RevertsWhenDependentsWouldBreak)
    {
        EXPECT_TRUE(QueueAddComponent(m_entity->GetId(), "DependentComponent"));
        AZ::TickBus::ExecuteQueuedEvents();
        ASSERT_NE(m_entity->FindComponent<DependentComponent>(), nullptr);

        AZ_TEST_START_TRACE_SUPPRESSION;
        EXPECT_TRUE(QueueRemoveComponent(m_entity->GetId(), "TestComponent"));
        AZ::TickBus::ExecuteQueuedEvents();
        AZ_TEST_STOP_TRACE_SUPPRESSION_NO_COUNT;

        EXPECT_EQ(m_entity->FindComponent<TestComponent>(), m_component);
        EXPECT_EQ(m_entity->GetState(), AZ::Entity::State::Active);
    }

    TEST_F(ComponentAccessFixture, QueueReactivate_CyclesEntity)
    {
        const int before = m_component->m_activations;
        QueueReactivate(m_entity->GetId());
        QueueReactivate(AZ::EntityId()); // ignored
        AZ::TickBus::ExecuteQueuedEvents();
        EXPECT_EQ(m_component->m_activations, before + 1);
        EXPECT_EQ(m_entity->GetState(), AZ::Entity::State::Active);
    }
} // namespace CSharpScripting::Tests

AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
