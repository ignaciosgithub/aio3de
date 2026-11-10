/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzToolsFramework/HotReload/GameCodeHotReloadSystemComponent.h>

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/IO/ByteContainerStream.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Module/ModuleManagerBus.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/Settings/SettingsRegistryVisitorUtils.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/string/string_view.h>

#include <AzToolsFramework/ActionManager/Action/ActionManagerInterface.h>
#include <AzToolsFramework/ActionManager/HotKey/HotKeyManagerInterface.h>
#include <AzToolsFramework/ActionManager/Menu/MenuManagerInterface.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Editor/ActionManagerIdentifiers/EditorContextIdentifiers.h>
#include <AzToolsFramework/Editor/ActionManagerIdentifiers/EditorMenuIdentifiers.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <AzToolsFramework/Entity/PrefabEditorEntityOwnershipInterface.h>
#include <AzToolsFramework/Prefab/PrefabSystemComponentInterface.h>

#include <chrono>
#include <cctype>

namespace AzToolsFramework
{
    namespace
    {
        constexpr AZStd::string_view HotReloadModulesPath = "/O3DE/HotReload/Modules";
        constexpr AZStd::string_view ReloadActionIdentifier = "o3de.action.gamecode.reload";

        GameCodeHotReloadSystemComponent* s_gameCodeHotReloadSystemComponent = nullptr;

        AZStd::string NormalizeModuleStem(AZStd::string_view path)
        {
            AZStd::string stem = AZ::IO::Path(path).Filename().Native();
            const size_t extensionSeparator = stem.find_last_of('.');
            if (extensionSeparator != AZStd::string::npos)
            {
                AZStd::string extension = stem.substr(extensionSeparator);
                AZStd::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });
                if (extension == ".dll" || extension == ".so" || extension == ".dylib")
                {
                    stem.resize(extensionSeparator);
                }
            }
            if (stem.size() >= 3 && std::tolower(static_cast<unsigned char>(stem[0])) == 'l' &&
                std::tolower(static_cast<unsigned char>(stem[1])) == 'i' &&
                std::tolower(static_cast<unsigned char>(stem[2])) == 'b')
            {
                stem.erase(0, 3);
            }
            AZStd::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
            return stem;
        }

        void ReloadGameCodeConsoleFunc([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
        {
            AZ::SystemTickBus::QueueFunction(
                []()
                {
                    if (s_gameCodeHotReloadSystemComponent)
                    {
                        s_gameCodeHotReloadSystemComponent->ReloadGameCode();
                    }
                });
        }

        AZ_CONSOLEFREEFUNC_4(
            "ed_reloadGameCode",
            ReloadGameCodeConsoleFunc,
            AZ::ConsoleFunctorFlags::Null,
            "Reload configured C++ game modules in the Editor");
        AZ_CVAR(bool, ed_hotReloadWatch, false, nullptr, AZ::ConsoleFunctorFlags::Null,
            "Watch configured game modules for changes and reload after they settle");
    } // namespace

    void GameCodeHotReloadSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<GameCodeHotReloadSystemComponent, AZ::Component>();
        }
    }

    void GameCodeHotReloadSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("GameCodeHotReloadService"));
    }

    void GameCodeHotReloadSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("GameCodeHotReloadService"));
    }

    void GameCodeHotReloadSystemComponent::Activate()
    {
        s_gameCodeHotReloadSystemComponent = this;
        AZ::TickBus::Handler::BusConnect();
        ActionManagerRegistrationNotificationBus::Handler::BusConnect();
    }

    void GameCodeHotReloadSystemComponent::Deactivate()
    {
        ActionManagerRegistrationNotificationBus::Handler::BusDisconnect();
        AZ::TickBus::Handler::BusDisconnect();
        m_watchedModules.clear();
        s_gameCodeHotReloadSystemComponent = nullptr;
    }

    void GameCodeHotReloadSystemComponent::ReloadGameCode()
    {
        AZStd::vector<AZStd::string> configuredModuleNames = GetConfiguredModuleNames();
        if (configuredModuleNames.empty())
        {
            AZ_Error("GameCodeHotReload", false, "No game modules are configured in /O3DE/HotReload/Modules.");
            return;
        }

        AZStd::vector<AZStd::string> modulePaths = GetLoadedConfiguredModulePaths();
        if (modulePaths.empty())
        {
            AZ_Warning("GameCodeHotReload", false, "No configured game modules are currently loaded.");
            return;
        }

        QueueReload(AZStd::move(modulePaths));
    }

    void GameCodeHotReloadSystemComponent::QueueReload(AZStd::vector<AZStd::string> modulePaths)
    {
        if (m_reloadRequested)
        {
            AZ_Warning("GameCodeHotReload", false, "A game-code reload is already in progress.");
            return;
        }
        m_requestedModulePaths = AZStd::move(modulePaths);
        m_reloadRequested = true;
    }

    AZStd::vector<AZStd::string> GameCodeHotReloadSystemComponent::GetConfiguredModuleNames() const
    {
        AZStd::vector<AZStd::string> moduleNames;
        AZ::SettingsRegistryInterface* settingsRegistry = AZ::SettingsRegistry::Get();
        if (!settingsRegistry)
        {
            return moduleNames;
        }

        AZ::SettingsRegistryVisitorUtils::VisitArray(
            *settingsRegistry,
            [&moduleNames, settingsRegistry](const AZ::SettingsRegistryInterface::VisitArgs& visitArgs)
            {
                AZStd::string moduleName;
                if (settingsRegistry->Get(moduleName, visitArgs.m_jsonKeyPath) && !moduleName.empty())
                {
                    moduleNames.emplace_back(AZStd::move(moduleName));
                }
                return AZ::SettingsRegistryInterface::VisitResponse::Continue;
            },
            HotReloadModulesPath);
        return moduleNames;
    }

    AZStd::vector<AZStd::string> GameCodeHotReloadSystemComponent::GetLoadedConfiguredModulePaths() const
    {
        const AZStd::vector<AZStd::string> configuredModuleNames = GetConfiguredModuleNames();
        AZStd::vector<AZStd::string> modulePaths;
        AZ::ModuleManagerRequestBus::Broadcast(
            &AZ::ModuleManagerRequests::EnumerateModules,
            [&configuredModuleNames, &modulePaths](const AZ::ModuleData& moduleData)
            {
                if (!moduleData.GetDynamicModuleHandle())
                {
                    return true;
                }
                const char* originalModulePath = moduleData.GetOriginalModulePath();
                if (originalModulePath)
                {
                    AZStd::string_view modulePath = originalModulePath;
                    AZStd::string moduleStem = NormalizeModuleStem(modulePath);
                    for (const AZStd::string& configuredName : configuredModuleNames)
                    {
                        if (moduleStem == NormalizeModuleStem(configuredName))
                        {
                            modulePaths.emplace_back(modulePath);
                            break;
                        }
                    }
                }
                return true;
            });
        return modulePaths;
    }

    void GameCodeHotReloadSystemComponent::OnTick(float deltaTime, AZ::ScriptTimePoint)
    {
        if (m_reloadRequested)
        {
            bool isRunningGame = false;
            EditorEntityContextRequestBus::BroadcastResult(
                isRunningGame, &EditorEntityContextRequests::IsEditorRunningGame);
            if (isRunningGame)
            {
                if (!m_waitingForPlayModeStop)
                {
                    EditorEntityContextRequestBus::Broadcast(&EditorEntityContextRequests::StopPlayInEditor);
                    m_waitingForPlayModeStop = true;
                }
                return;
            }
            m_waitingForPlayModeStop = false;

            AZStd::vector<AZStd::string> modulePaths = AZStd::move(m_requestedModulePaths);
            m_reloadRequested = false;
            PerformReload(modulePaths);
            return;
        }

        if (ed_hotReloadWatch)
        {
            PollModuleFiles(deltaTime);
        }
        else
        {
            m_watchPollElapsed = 0.0f;
            m_watchedModules.clear();
        }
    }

    void GameCodeHotReloadSystemComponent::PollModuleFiles(float deltaTime)
    {
        m_watchPollElapsed += deltaTime;
        if (m_watchPollElapsed < 1.0f)
        {
            return;
        }
        m_watchPollElapsed = 0.0f;

        AZStd::vector<AZStd::string> changedModulePaths;
        for (const AZStd::string& modulePath : GetLoadedConfiguredModulePaths())
        {
            const AZ::u64 modificationTime = AZ::IO::SystemFile::ModificationTime(modulePath.c_str());
            WatchedModule& watchedModule = m_watchedModules[modulePath];
            if (!watchedModule.m_initialized)
            {
                watchedModule.m_modificationTime = modificationTime;
                watchedModule.m_initialized = true;
                continue;
            }
            if (modificationTime != watchedModule.m_modificationTime)
            {
                watchedModule.m_modificationTime = modificationTime;
                watchedModule.m_secondsWithoutChanges = 0.0f;
                watchedModule.m_changed = true;
            }
            else if (watchedModule.m_changed)
            {
                watchedModule.m_secondsWithoutChanges += 1.0f;
                if (watchedModule.m_secondsWithoutChanges >= 2.0f)
                {
                    changedModulePaths.emplace_back(modulePath);
                    watchedModule.m_changed = false;
                }
            }
        }
        if (!changedModulePaths.empty())
        {
            QueueReload(AZStd::move(changedModulePaths));
        }
    }

    void GameCodeHotReloadSystemComponent::PerformReload(const AZStd::vector<AZStd::string>& modulePaths)
    {
        const auto startTime = std::chrono::steady_clock::now();
        PrefabEditorEntityOwnershipInterface* ownershipInterface =
            AZ::Interface<PrefabEditorEntityOwnershipInterface>::Get();
        Prefab::PrefabSystemComponentInterface* prefabSystemInterface =
            AZ::Interface<Prefab::PrefabSystemComponentInterface>::Get();
        if (!ownershipInterface || !prefabSystemInterface || !ownershipInterface->IsRootPrefabAssigned())
        {
            AZ_Error("GameCodeHotReload", false, "Cannot reload game code without an assigned Editor level prefab.");
            return;
        }

        auto rootInstance = ownershipInterface->GetRootPrefabInstance();
        if (!rootInstance)
        {
            AZ_Error("GameCodeHotReload", false, "Cannot access the Editor level prefab instance.");
            return;
        }

        const AZ::IO::Path levelPath = rootInstance->get().GetTemplateSourcePath();
        const Prefab::TemplateId templateId = ownershipInterface->GetRootPrefabTemplateId();
        const bool wasDirty = prefabSystemInterface->IsTemplateDirty(templateId);
        AZStd::vector<char> levelBuffer;
        AZ::IO::ByteContainerStream<AZStd::vector<char>> levelStream(&levelBuffer);
        if (!ownershipInterface->SaveToStream(levelStream, levelPath.Native()))
        {
            AZ_Error("GameCodeHotReload", false, "Failed to serialize the current level before reloading game code.");
            return;
        }

        EditorEntityContextRequestBus::Broadcast(&EditorEntityContextRequests::ResetEditorContext);
        ToolsApplicationRequestBus::Broadcast(&ToolsApplicationRequestBus::Events::FlushUndo);

        AZ::u32 reloadedModuleCount = 0;
        AZStd::string reloadErrors;
        for (const AZStd::string& modulePath : modulePaths)
        {
            AZ::ModuleManagerRequests::ReloadModuleOutcome reloadOutcome =
                AZ::Failure(AZStd::string("Module reload was not attempted."));
            AZ::ModuleManagerRequestBus::BroadcastResult(
                reloadOutcome, &AZ::ModuleManagerRequests::ReloadDynamicModule, modulePath.c_str());
            if (reloadOutcome.IsSuccess())
            {
                ++reloadedModuleCount;
                reloadOutcome.GetValue().reset();
            }
            else
            {
                reloadErrors += AZStd::string::format("%s%s", reloadErrors.empty() ? "" : "\n", reloadOutcome.GetError().c_str());
            }
        }

        levelStream.Seek(0, AZ::IO::GenericStream::ST_SEEK_BEGIN);
        const bool levelRestored = ownershipInterface->LoadFromStream(levelStream, levelPath.Native());
        if (levelRestored && ownershipInterface->IsRootPrefabAssigned())
        {
            prefabSystemInterface->SetTemplateDirtyFlag(ownershipInterface->GetRootPrefabTemplateId(), wasDirty);
        }
        if (!levelRestored)
        {
            AZ_Error("GameCodeHotReload", false, "Failed to restore the Editor level after reloading game code.");
        }

        const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (reloadErrors.empty() && levelRestored)
        {
            AZ_Printf("GameCodeHotReload", "Hot reload: reloaded %u module(s) in %lld ms",
                reloadedModuleCount, static_cast<long long>(elapsedMilliseconds));
        }
        else if (!reloadErrors.empty())
        {
            AZ_Error("GameCodeHotReload", false, "Hot reload failed: %s", reloadErrors.c_str());
        }
    }

    void GameCodeHotReloadSystemComponent::OnActionRegistrationHook()
    {
        ActionManagerInterface* actionManager = AZ::Interface<ActionManagerInterface>::Get();
        if (!actionManager || actionManager->IsActionRegistered(AZStd::string(ReloadActionIdentifier)))
        {
            return;
        }

        ActionProperties actionProperties;
        actionProperties.m_name = "Reload Game Code";
        actionProperties.m_description = "Reload configured C++ game modules";
        actionProperties.m_category = "Tools";
        actionManager->RegisterAction(
            AZStd::string(EditorIdentifiers::MainWindowActionContextIdentifier),
            AZStd::string(ReloadActionIdentifier),
            actionProperties,
            [this]()
            {
                ReloadGameCode();
            });

        if (HotKeyManagerInterface* hotKeyManager = AZ::Interface<HotKeyManagerInterface>::Get())
        {
            hotKeyManager->SetActionHotKey(AZStd::string(ReloadActionIdentifier), "Ctrl+Alt+F11");
        }
    }

    void GameCodeHotReloadSystemComponent::OnMenuBindingHook()
    {
        if (MenuManagerInterface* menuManager = AZ::Interface<MenuManagerInterface>::Get())
        {
            menuManager->AddActionToMenu(
                AZStd::string(EditorIdentifiers::ToolsMenuIdentifier), AZStd::string(ReloadActionIdentifier), 9000);
        }
    }
} // namespace AzToolsFramework
