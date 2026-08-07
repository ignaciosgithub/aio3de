/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ProjectManagerWindow.h"
#include "DownloadController.h"
#include "ProjectManagerBuses.h"
#include "PythonBindingsInterface.h"
#include "ScreensCtrl.h"

#include <QCloseEvent>
#include <QMessageBox>

namespace O3DE::ProjectManager
{
    ProjectManagerWindow::ProjectManagerWindow(QWidget* parent, const AZ::IO::PathView& projectPath, ProjectManagerScreen startScreen)
        : QMainWindow(parent)
    {
        if (auto engineInfoOutcome = PythonBindingsInterface::Get()->GetEngineInfo(); engineInfoOutcome)
        {
            auto engineInfo = engineInfoOutcome.GetValue<EngineInfo>();
            auto versionToDisplay = engineInfo.m_displayVersion == "00.00" ?
                                        engineInfo.m_version : engineInfo.m_displayVersion;
            // display-only: the fork brands the stock "o3de" engine as AIO3DE in window titles
            const QString displayName =
                engineInfo.m_name.compare("o3de", Qt::CaseInsensitive) == 0 ? QStringLiteral("AIO3DE") : engineInfo.m_name.toUpper();
            setWindowTitle(QString("%1 %2 %3").arg(displayName, versionToDisplay, tr("Project Manager")));
        }
        else
        {
            setWindowTitle(QString("AIO3DE %1").arg(tr("Project Manager")));
        }

        m_downloadController = new DownloadController(this);

        ScreensCtrl* screensCtrl = new ScreensCtrl(nullptr, m_downloadController);

        // currently the tab order on the home page is based on the order of this list
        QVector<ProjectManagerScreen> screenEnums =
        {
            ProjectManagerScreen::Projects,
            ProjectManagerScreen::CreateGem,
            ProjectManagerScreen::EditGem,
            ProjectManagerScreen::GemCatalog,
            ProjectManagerScreen::Engine,
            ProjectManagerScreen::CreateProject,
            ProjectManagerScreen::UpdateProject,
            ProjectManagerScreen::GemsGemRepos
        };
        screensCtrl->BuildScreens(screenEnums);

        setCentralWidget(screensCtrl);

        // Projects is the default first screen because it is first in the above order
        if (startScreen != ProjectManagerScreen::Projects)
        {
            // always push the projects screen first so we have something to come back to
            screensCtrl->ForceChangeToScreen(ProjectManagerScreen::Projects);
            screensCtrl->ForceChangeToScreen(startScreen);
        }

        if (!projectPath.empty())
        {
            const QString path = QString::fromUtf8(projectPath.Native().data(), aznumeric_cast<int>(projectPath.Native().size()));
            emit screensCtrl->NotifyCurrentProject(path);
        }
    }

    void ProjectManagerWindow::closeEvent(QCloseEvent* event)
    {
        bool canClose = true;
        ProjectManagerUtilityRequestsBus::Broadcast(&ProjectManagerUtilityRequestsBus::Events::CanCloseProjectManager, canClose);

        if (!canClose)
        {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("Project action ongoing"),
                tr("A project action is currently going on. Are you sure you want to exit?"),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::No)
            {
                event->ignore();
                return;
            }
        }

        QMainWindow::closeEvent(event);
    }
} // namespace O3DE::ProjectManager
