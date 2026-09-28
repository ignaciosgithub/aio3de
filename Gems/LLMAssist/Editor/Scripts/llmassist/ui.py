"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""

"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# The in-Editor view panes:
#  - AIAssistantDialog (Tools > AI Assistant): Chat tab (provider/model picker,
#    docs-aware toggle, scene-aware toggle, apply-file-edit flow with user-save
#    priority, apply-scene-actions flow with confirmation + undo batch) and a
#    Settings tab (API keys, stored per-user, never in the project).
#  - GemManagerDialog (Tools > Gem Manager): list/search all engine gems,
#    enable/disable with one click, rebuild guidance for code gems.

import os
import re

from PySide6 import QtCore, QtWidgets

from . import docs_context
from . import file_guard
from . import gem_manager
from . import keystore
from . import memory
from . import providers
from . import scene_actions

_FILE_BLOCK = re.compile(
    r"FILE:\s*(?P<path>[^\n]+)\n+```[a-zA-Z0-9_+-]*\n(?P<body>.*?)```", re.DOTALL)


class _ChatWorker(QtCore.QThread):
    """Runs the network call off the UI thread."""
    finished_ok = QtCore.Signal(str)
    finished_err = QtCore.Signal(str)

    def __init__(self, provider, messages, model, parent=None):
        super().__init__(parent)
        self._provider = provider
        self._messages = messages
        self._model = model

    def run(self):
        try:
            reply = providers.chat(self._provider, self._messages, model=self._model)
            self.finished_ok.emit(reply)
        except Exception as e:  # surfaced to the user, never crashes the Editor
            self.finished_err.emit(str(e))


class AIAssistantDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("AI Assistant")
        self.resize(760, 640)
        self._history = []
        self._worker = None
        self._last_reply = ""
        self._backend = None

        tabs = QtWidgets.QTabWidget()
        tabs.addTab(self._build_chat_tab(), "Chat")
        tabs.addTab(self._build_memory_tab(), "Memory")
        tabs.addTab(self._build_settings_tab(), "Settings")
        layout = QtWidgets.QVBoxLayout(self)
        layout.addWidget(tabs)

    # ---- Chat tab ----

    def _build_chat_tab(self):
        widget = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(widget)

        top = QtWidgets.QHBoxLayout()
        top.addWidget(QtWidgets.QLabel("Provider:"))
        self._provider = QtWidgets.QComboBox()
        self._provider.addItems(list(providers.PROVIDERS))
        self._provider.currentTextChanged.connect(self._on_provider_changed)
        top.addWidget(self._provider)
        top.addWidget(QtWidgets.QLabel("Model:"))
        self._model = QtWidgets.QComboBox()
        self._model.setEditable(True)  # any model id can be typed directly
        self._populate_models(self._provider.currentText())
        top.addWidget(self._model, 1)
        add_model = QtWidgets.QPushButton("+")
        add_model.setFixedWidth(28)
        add_model.setToolTip("Save the typed model id to this provider's list "
                             "(kept in ~/.o3de/llmassist_models.json)")
        add_model.clicked.connect(self._on_add_model)
        top.addWidget(add_model)
        layout.addLayout(top)

        options = QtWidgets.QHBoxLayout()
        self._docs_aware = QtWidgets.QCheckBox("Docs-aware (engine docs + updates)")
        self._docs_aware.setChecked(True)
        options.addWidget(self._docs_aware)
        self._scene_aware = QtWidgets.QCheckBox("Scene-aware (open level + selection)")
        self._scene_aware.setChecked(True)
        self._scene_aware.setToolTip(
            "Give the assistant the entities of the open level and the components/properties "
            "of the selected entities, and let it propose scene actions (create/select/modify "
            "entities and components, rebuild C#) that you apply with one click.")
        options.addWidget(self._scene_aware)
        options.addStretch(1)
        layout.addLayout(options)

        self._transcript = QtWidgets.QPlainTextEdit()
        self._transcript.setReadOnly(True)
        layout.addWidget(self._transcript, 1)

        self._input = QtWidgets.QPlainTextEdit()
        self._input.setPlaceholderText(
            "Ask about the engine, the docs, request a file edit, or ask for changes to the "
            "open level (e.g. 'add a crate with a rigid body above the player')...")
        self._input.setFixedHeight(90)
        layout.addWidget(self._input)

        buttons = QtWidgets.QHBoxLayout()
        self._send = QtWidgets.QPushButton("Send")
        self._send.clicked.connect(self._on_send)
        buttons.addWidget(self._send)
        self._apply = QtWidgets.QPushButton("Apply file edits from last reply")
        self._apply.setEnabled(False)
        self._apply.clicked.connect(self._on_apply_edits)
        buttons.addWidget(self._apply)
        self._apply_actions = QtWidgets.QPushButton("Apply scene actions from last reply")
        self._apply_actions.setEnabled(False)
        self._apply_actions.clicked.connect(self._on_apply_actions)
        buttons.addWidget(self._apply_actions)
        clear = QtWidgets.QPushButton("Clear")
        clear.clicked.connect(self._on_clear)
        buttons.addWidget(clear)
        buttons.addStretch(1)
        layout.addLayout(buttons)
        return widget

    def _populate_models(self, provider):
        self._model.clear()
        self._model.addItems(providers.models_for(provider))

    def _on_provider_changed(self, provider):
        self._populate_models(provider)

    def _on_add_model(self):
        model = self._model.currentText().strip()
        provider = self._provider.currentText()
        if model:
            providers.add_user_model(provider, model)
            self._populate_models(provider)
            self._model.setCurrentText(model)

    def _append(self, who, text):
        self._transcript.appendPlainText(f"[{who}]\n{text}\n")

    def _on_clear(self):
        self._history = []
        self._transcript.clear()
        self._apply.setEnabled(False)
        self._apply_actions.setEnabled(False)

    def _on_send(self):
        question = self._input.toPlainText().strip()
        if not question or self._worker is not None:
            return
        self._input.clear()
        self._append("you", question)

        # "remember: ..." stores a durable per-project fact without an API call.
        if question.lower().startswith("remember:"):
            fact = question[len("remember:"):].strip()
            memory.add_fact(fact)
            self._append("system", f"Remembered for this project: {fact}")
            self._refresh_memory_tab()
            return

        system = ""
        if self._docs_aware.isChecked():
            system = docs_context.system_prompt(question)
        if self._scene_aware.isChecked():
            system = (system + "\n\n" + self._scene_prompt()).strip()
        memory_block = memory.context_block()
        if memory_block:
            system = (system + "\n\n--- PROJECT MEMORY ---\n" + memory_block).strip()
        messages = []
        if system:
            messages.append({"role": "system", "content": system})
        messages.extend(self._history)
        messages.append({"role": "user", "content": question})

        self._send.setEnabled(False)
        self._send.setText("Waiting...")
        self._worker = _ChatWorker(
            self._provider.currentText(), messages,
            self._model.currentText().strip() or None, self)
        self._worker.finished_ok.connect(lambda reply: self._on_reply(question, reply))
        self._worker.finished_err.connect(self._on_error)
        self._worker.start()

    def _finish_request(self):
        self._send.setEnabled(True)
        self._send.setText("Send")
        self._worker = None

    def _on_reply(self, question, reply):
        self._finish_request()
        self._history.append({"role": "user", "content": question})
        self._history.append({"role": "assistant", "content": reply})
        memory.record_exchange(question, reply)
        self._refresh_memory_tab()
        self._append("assistant", reply)
        self._last_reply = reply
        self._apply.setEnabled(bool(_FILE_BLOCK.search(reply)))
        self._apply_actions.setEnabled(scene_actions.has_actions(reply))

    def _on_error(self, message):
        self._finish_request()
        self._append("error", message)

    # ---- applying AI file edits (user-save priority) ----

    def _on_apply_edits(self):
        reply = self._last_reply
        edits = [(m.group("path").strip(), m.group("body")) for m in _FILE_BLOCK.finditer(reply)]
        if not edits:
            return
        root = docs_context.engine_root()
        try:
            import azlmbr.paths
            project_root = azlmbr.paths.projectroot
        except Exception:
            project_root = os.getcwd()

        for rel_path, body in edits:
            path = rel_path
            if not os.path.isabs(path):
                candidate = os.path.join(project_root, rel_path)
                path = candidate if (
                    os.path.isfile(candidate) or not os.path.isfile(os.path.join(root, rel_path))
                ) else os.path.join(root, rel_path)

            # User-save priority: ask the user to save & close the file first.
            answer = QtWidgets.QMessageBox.question(
                self, "Apply AI edit",
                f"The assistant wants to write:\n\n{path}\n\n"
                "If this file is open anywhere (script editor, external editor), "
                "SAVE and CLOSE it first - your save always takes priority.\n\n"
                "A .bak backup will be created. Apply the edit?",
                QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
            if answer != QtWidgets.QMessageBox.Yes:
                self._append("system", f"Skipped {path} (user declined).")
                continue

            edit = file_guard.ProposedEdit(path, body)
            try:
                backup = edit.apply()
            except file_guard.StaleFileError as e:
                QtWidgets.QMessageBox.warning(self, "File changed on disk", str(e))
                self._append("system", f"NOT applied (user save wins): {path}")
                continue
            except OSError as e:
                self._append("error", f"Failed writing {path}: {e}")
                continue
            note = f"Applied edit to {path}"
            if backup:
                note += f" (backup: {os.path.basename(backup)})"
            self._append("system", note)

    # ---- scene actions (act on the open level, confirmed, one undo batch) ----

    def _get_backend(self):
        if self._backend is None:
            self._backend = scene_actions.EditorBackend()
        return self._backend

    def _scene_prompt(self):
        try:
            context = scene_actions.scene_context(self._get_backend())
        except Exception as e:  # e.g. running outside the Editor
            context = f"(scene unavailable: {e})"
        if not context:
            context = "(no level is open, or it has no entities)"
        return ("--- OPEN LEVEL ---\n" + context + "\n\n--- SCENE ACTIONS ---\n"
                + scene_actions.ACTION_DOCS)

    def _on_apply_actions(self):
        try:
            actions = scene_actions.parse_actions(self._last_reply)
        except scene_actions.ActionError as e:
            QtWidgets.QMessageBox.warning(self, "Scene actions", str(e))
            self._append("error", str(e))
            return
        if not actions:
            return
        try:
            backend = self._get_backend()
        except Exception as e:
            self._append("error", f"Scene actions need the Editor Python bindings: {e}")
            return

        preview = "\n".join(
            f"{i}. {'[confirm] ' if scene_actions.is_destructive(a) else ''}{scene_actions.describe(a)}"
            for i, a in enumerate(actions, start=1))
        answer = QtWidgets.QMessageBox.question(
            self, "Apply scene actions",
            f"The assistant wants to change the open level:\n\n{preview}\n\n"
            "Actions marked [confirm] are destructive and will be confirmed one by one. "
            "Everything is applied as one undo step (Ctrl+Z reverts it). Apply?",
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
        if answer != QtWidgets.QMessageBox.Yes:
            self._append("system", "Scene actions skipped (user declined).")
            return

        def confirm(action, text):
            reply = QtWidgets.QMessageBox.question(
                self, "Confirm destructive action",
                f"{text}\n\nProceed?",
                QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
            return reply == QtWidgets.QMessageBox.Yes

        executor = scene_actions.Executor(backend, confirm=confirm)
        applied, messages = executor.run(actions)
        self._append("system", f"Scene actions: {applied}/{len(actions)} applied\n" + "\n".join(messages))
        self._apply_actions.setEnabled(False)

    # ---- Memory tab (per-project, persists across Editor restarts) ----

    def _build_memory_tab(self):
        widget = QtWidgets.QWidget()
        layout = QtWidgets.QVBoxLayout(widget)
        note = QtWidgets.QLabel(
            "Per-project memory, stored in <project>/user/llmassist_memory.json "
            "(git-ignored, survives Editor restarts). Facts are always given to the "
            "assistant; type 'remember: <fact>' in the chat to add one quickly.")
        note.setWordWrap(True)
        layout.addWidget(note)

        layout.addWidget(QtWidgets.QLabel("Facts:"))
        self._facts_list = QtWidgets.QListWidget()
        layout.addWidget(self._facts_list, 1)

        fact_row = QtWidgets.QHBoxLayout()
        self._fact_input = QtWidgets.QLineEdit()
        self._fact_input.setPlaceholderText("Add a fact to remember for this project...")
        fact_row.addWidget(self._fact_input, 1)
        add_fact = QtWidgets.QPushButton("Add")
        add_fact.clicked.connect(self._on_add_fact)
        fact_row.addWidget(add_fact)
        remove_fact = QtWidgets.QPushButton("Remove selected")
        remove_fact.clicked.connect(self._on_remove_fact)
        fact_row.addWidget(remove_fact)
        layout.addLayout(fact_row)

        layout.addWidget(QtWidgets.QLabel("Recent exchanges:"))
        self._exchanges_view = QtWidgets.QPlainTextEdit()
        self._exchanges_view.setReadOnly(True)
        layout.addWidget(self._exchanges_view, 1)

        clear = QtWidgets.QPushButton("Clear all project memory")
        clear.clicked.connect(self._on_clear_memory)
        layout.addWidget(clear)

        self._refresh_memory_tab()
        return widget

    def _refresh_memory_tab(self):
        if not hasattr(self, "_facts_list"):
            return
        data = memory.load()
        self._facts_list.clear()
        for fact in data["facts"]:
            self._facts_list.addItem(fact)
        lines = []
        for exchange in data["exchanges"]:
            lines.append(f"[{exchange['time']}] you: {exchange['q']}")
            lines.append(f"assistant: {exchange['a']}")
            lines.append("")
        self._exchanges_view.setPlainText("\n".join(lines))

    def _on_add_fact(self):
        memory.add_fact(self._fact_input.text())
        self._fact_input.clear()
        self._refresh_memory_tab()

    def _on_remove_fact(self):
        row = self._facts_list.currentRow()
        if row >= 0:
            memory.remove_fact(row)
            self._refresh_memory_tab()

    def _on_clear_memory(self):
        answer = QtWidgets.QMessageBox.question(
            self, "Clear memory",
            "Delete all remembered facts and exchange history for this project?",
            QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No)
        if answer == QtWidgets.QMessageBox.Yes:
            memory.clear()
            self._refresh_memory_tab()

    # ---- Settings tab ----

    def _build_settings_tab(self):
        widget = QtWidgets.QWidget()
        form = QtWidgets.QFormLayout(widget)
        note = QtWidgets.QLabel(
            "Keys are stored per-user in ~/.o3de/llmassist_keys.json (chmod 600) - "
            "outside the project and never committed. Environment variables "
            "(OPENAI_API_KEY, ANTHROPIC_API_KEY, MOONSHOT_API_KEY) take priority.")
        note.setWordWrap(True)
        form.addRow(note)
        self._key_edits = {}
        for provider in providers.PROVIDERS:
            edit = QtWidgets.QLineEdit()
            edit.setEchoMode(QtWidgets.QLineEdit.Password)
            source = keystore.key_source(provider)
            edit.setPlaceholderText(
                {"env": "set from environment variable", "file": "saved",
                 "": "not configured"}[source])
            self._key_edits[provider] = edit
            form.addRow(f"{provider} API key:", edit)
        save = QtWidgets.QPushButton("Save keys")
        save.clicked.connect(self._on_save_keys)
        form.addRow(save)
        return widget

    def _on_save_keys(self):
        for provider, edit in self._key_edits.items():
            value = edit.text().strip()
            if value:
                keystore.set_key(provider, value)
                edit.clear()
                edit.setPlaceholderText("saved")
        QtWidgets.QMessageBox.information(self, "AI Assistant", "Keys saved.")


class GemManagerDialog(QtWidgets.QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Gem Manager")
        self.resize(820, 600)
        layout = QtWidgets.QVBoxLayout(self)

        self._filter = QtWidgets.QLineEdit()
        self._filter.setPlaceholderText("Filter gems...")
        self._filter.textChanged.connect(self._refresh)
        layout.addWidget(self._filter)

        self._table = QtWidgets.QTableWidget(0, 4)
        self._table.setHorizontalHeaderLabels(["Enabled", "Gem", "Type", "Summary"])
        self._table.horizontalHeader().setSectionResizeMode(3, QtWidgets.QHeaderView.Stretch)
        self._table.setEditTriggers(QtWidgets.QAbstractItemView.NoEditTriggers)
        layout.addWidget(self._table, 1)

        self._status = QtWidgets.QLabel(
            "Toggle a checkbox to enable/disable a gem for the current project. "
            "Code gems need: re-run CMake configure -> rebuild Editor -> relaunch. "
            "Asset/Tool gems need: re-run CMake configure (no compile) -> Editor restart.")
        self._status.setWordWrap(True)
        layout.addWidget(self._status)

        self._refresh()

    def _refresh(self):
        needle = self._filter.text().strip().lower()
        gems = [g for g in gem_manager.list_gems()
                if not needle or needle in g["name"].lower() or needle in g["summary"].lower()]
        self._table.blockSignals(True)
        self._table.setRowCount(0)
        for gem in gems:
            row = self._table.rowCount()
            self._table.insertRow(row)
            box = QtWidgets.QCheckBox()
            box.setChecked(gem["enabled"])
            box.toggled.connect(
                lambda checked, name=gem["name"], rebuild=gem["needs_rebuild"]:
                self._on_toggle(name, checked, rebuild))
            cell = QtWidgets.QWidget()
            cell_layout = QtWidgets.QHBoxLayout(cell)
            cell_layout.setContentsMargins(8, 0, 0, 0)
            cell_layout.addWidget(box)
            self._table.setCellWidget(row, 0, cell)
            self._table.setItem(row, 1, QtWidgets.QTableWidgetItem(gem["name"]))
            self._table.setItem(row, 2, QtWidgets.QTableWidgetItem(gem["type"]))
            self._table.setItem(row, 3, QtWidgets.QTableWidgetItem(gem["summary"]))
        self._table.blockSignals(False)

    def _on_toggle(self, name, enable, needs_rebuild):
        ok, message = gem_manager.set_gem_enabled(name, enable)
        if not ok:
            QtWidgets.QMessageBox.warning(self, "Gem Manager", message)
            self._refresh()
            return
        if needs_rebuild:
            message += (" This is a CODE gem: re-run the CMake configure, rebuild the "
                        "Editor, then relaunch for the change to take effect.")
        else:
            message += (" Re-run the CMake configure (no compile needed) so the gem is "
                        "registered as active, then restart the Editor / Asset Processor.")
        self._status.setText(message)
