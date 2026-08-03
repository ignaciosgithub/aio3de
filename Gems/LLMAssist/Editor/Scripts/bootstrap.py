"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# Registers the LLM Assist view panes with the Editor:
#   Tools > AI Assistant  (chat with OpenAI/Anthropic/Kimi, docs-aware, file edits)
#   Tools > Gem Manager   (enable/disable gems without the command line)
# Executed automatically by EditorPythonBindings when this gem is active.

try:
    import az_qt_helpers
    from llmassist.ui import AIAssistantDialog, GemManagerDialog

    az_qt_helpers.register_view_pane('AI Assistant', AIAssistantDialog)
    az_qt_helpers.register_view_pane('Gem Manager', GemManagerDialog)
    print('LLMAssist: registered AI Assistant and Gem Manager view panes (Tools menu).')
except Exception as e:
    print(f'LLMAssist: skipping view pane registration ({e}). '
          'Ensure the QtForPython and EditorPythonBindings gems are enabled.')
