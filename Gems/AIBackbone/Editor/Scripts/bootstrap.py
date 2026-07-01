"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Registers the AI Backbone's "AI Model Builder" view pane with the Editor
# (Tools > AI Model Builder). Executed automatically by EditorPythonBindings
# when this gem is active.

try:
    import az_qt_helpers
    from aibackbone.ui import AIModelBuilderDialog

    az_qt_helpers.register_view_pane('AI Model Builder', AIModelBuilderDialog)
    print('AIBackbone: registered AI Model Builder view pane (Tools > AI Model Builder).')
except Exception as e:
    print(f'AIBackbone: skipping AI Model Builder registration ({e}). '
          'Ensure the QtForPython and EditorPythonBindings gems are enabled.')
