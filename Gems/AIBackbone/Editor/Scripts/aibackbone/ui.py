"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# The in-Editor "AI Model Builder" view pane (Tools > AI Model Builder):
#  - Models tab: create models from templates, edit their input variables (name +
#    type), hidden layers (units + activation), output fields and training
#    hyperparameters; specs are saved as JSON under <project>/AIModels.
#  - Train tab: train a model on a recorded dataset in a background thread with
#    live progress, Stop & Save (checkpoint) at any time and resume-from-checkpoint.
#  - Import ONNX tab: import any ONNX model and auto-detect its required inputs
#    and outputs (names, dtypes, shapes).

import os
import queue
import sys
import threading

from PySide6 import QtCore, QtWidgets

from . import deps
from . import ml_available
from . import model_spec

try:
    import azlmbr.paths
    PROJECT_ROOT = azlmbr.paths.projectroot
except Exception:
    PROJECT_ROOT = os.getcwd()

MODELS_DIR = os.path.join(PROJECT_ROOT, "AIModels")


def _install_prompt(parent, status_widget):
    missing = deps.missing_libs()
    if not missing:
        return True
    answer = QtWidgets.QMessageBox.question(
        parent, "AIBackbone libraries",
        "Missing libraries: " + ", ".join(missing) + "\n\nInstall now?",
        QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No,
        QtWidgets.QMessageBox.Yes)
    if answer == QtWidgets.QMessageBox.Yes:
        widget = parent
        while widget is not None and not hasattr(widget, "start_install"):
            widget = widget.parentWidget()
        if widget is not None:
            widget.start_install()
    elif status_widget is not None:
        message = "Missing libraries: " + ", ".join(missing)
        if hasattr(status_widget, "appendPlainText"):
            status_widget.appendPlainText(message)
        else:
            status_widget.setText(message)
    return False


def _spec_files():
    if not os.path.isdir(MODELS_DIR):
        return []
    return sorted(
        os.path.join(MODELS_DIR, f) for f in os.listdir(MODELS_DIR) if f.endswith(".model.json"))


class VariableTable(QtWidgets.QTableWidget):
    """Editable table of (name, type) variables."""

    def __init__(self, parent=None):
        super().__init__(0, 2, parent)
        self.setHorizontalHeaderLabels(["Name", "Type"])
        self.horizontalHeader().setSectionResizeMode(0, QtWidgets.QHeaderView.Stretch)

    def set_variables(self, variables):
        self.setRowCount(0)
        for var in variables:
            self.add_variable(var["name"], var["type"])

    def add_variable(self, name="var", var_type="float"):
        row = self.rowCount()
        self.insertRow(row)
        self.setItem(row, 0, QtWidgets.QTableWidgetItem(name))
        combo = QtWidgets.QComboBox()
        combo.addItems(list(model_spec.VARIABLE_TYPES))
        combo.setCurrentText(var_type)
        self.setCellWidget(row, 1, combo)

    def variables(self):
        result = []
        for row in range(self.rowCount()):
            name = self.item(row, 0).text().strip()
            var_type = self.cellWidget(row, 1).currentText()
            result.append(model_spec.make_variable(name, var_type))
        return result

    def remove_selected(self):
        for index in sorted({i.row() for i in self.selectedIndexes()}, reverse=True):
            self.removeRow(index)


class LayerTable(QtWidgets.QTableWidget):
    """Editable table of (units, activation) hidden layers."""

    def __init__(self, parent=None):
        super().__init__(0, 2, parent)
        self.setHorizontalHeaderLabels(["Units", "Activation"])
        self.horizontalHeader().setSectionResizeMode(0, QtWidgets.QHeaderView.Stretch)

    def set_layers(self, layers):
        self.setRowCount(0)
        for layer in layers:
            self.add_layer(layer["units"], layer["activation"])

    def add_layer(self, units=16, activation="relu"):
        row = self.rowCount()
        self.insertRow(row)
        spin = QtWidgets.QSpinBox()
        spin.setRange(1, 65536)
        spin.setValue(int(units))
        self.setCellWidget(row, 0, spin)
        combo = QtWidgets.QComboBox()
        combo.addItems(model_spec.ACTIVATIONS)
        combo.setCurrentText(activation)
        self.setCellWidget(row, 1, combo)

    def layers(self):
        result = []
        for row in range(self.rowCount()):
            units = self.cellWidget(row, 0).value()
            activation = self.cellWidget(row, 1).currentText()
            result.append(model_spec.make_layer(units, activation))
        return result

    def remove_selected(self):
        for index in sorted({i.row() for i in self.selectedIndexes()}, reverse=True):
            self.removeRow(index)


class ModelsTab(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QtWidgets.QHBoxLayout(self)

        # Left: model list + new-from-template
        left = QtWidgets.QVBoxLayout()
        self.model_list = QtWidgets.QListWidget()
        self.model_list.currentTextChanged.connect(self._load_selected)
        left.addWidget(QtWidgets.QLabel("Models (AIModels/*.model.json)"))
        left.addWidget(self.model_list)
        self.template_combo = QtWidgets.QComboBox()
        self.template_combo.addItems(list(model_spec.builtin_templates()))
        left.addWidget(QtWidgets.QLabel("Template"))
        left.addWidget(self.template_combo)
        new_button = QtWidgets.QPushButton("New model from template")
        new_button.clicked.connect(self._new_model)
        left.addWidget(new_button)
        refresh_button = QtWidgets.QPushButton("Refresh list")
        refresh_button.clicked.connect(self.refresh)
        left.addWidget(refresh_button)
        layout.addLayout(left, 1)

        # Right: spec editor
        right = QtWidgets.QVBoxLayout()
        form = QtWidgets.QFormLayout()
        self.name_edit = QtWidgets.QLineEdit()
        form.addRow("Name", self.name_edit)
        right.addLayout(form)

        right.addWidget(QtWidgets.QLabel("Input variables"))
        self.inputs_table = VariableTable()
        right.addWidget(self.inputs_table)
        right.addLayout(self._table_buttons(self.inputs_table, lambda: self.inputs_table.add_variable()))

        right.addWidget(QtWidgets.QLabel("Hidden layers"))
        self.layers_table = LayerTable()
        right.addWidget(self.layers_table)
        right.addLayout(self._table_buttons(self.layers_table, lambda: self.layers_table.add_layer()))

        right.addWidget(QtWidgets.QLabel("Output fields"))
        self.outputs_table = VariableTable()
        right.addWidget(self.outputs_table)
        right.addLayout(self._table_buttons(self.outputs_table, lambda: self.outputs_table.add_variable()))

        hyper = QtWidgets.QFormLayout()
        self.output_activation = QtWidgets.QComboBox()
        self.output_activation.addItems(model_spec.OUTPUT_ACTIVATIONS)
        hyper.addRow("Output activation", self.output_activation)
        self.epochs = QtWidgets.QSpinBox()
        self.epochs.setRange(1, 1000000)
        self.epochs.setValue(100)
        hyper.addRow("Epochs", self.epochs)
        self.learning_rate = QtWidgets.QDoubleSpinBox()
        self.learning_rate.setDecimals(6)
        self.learning_rate.setRange(1e-6, 10.0)
        self.learning_rate.setValue(1e-3)
        hyper.addRow("Learning rate", self.learning_rate)
        self.batch_size = QtWidgets.QSpinBox()
        self.batch_size.setRange(1, 65536)
        self.batch_size.setValue(32)
        hyper.addRow("Batch size", self.batch_size)
        right.addLayout(hyper)

        save_button = QtWidgets.QPushButton("Save model spec")
        save_button.clicked.connect(self._save)
        right.addWidget(save_button)
        self.status = QtWidgets.QLabel("")
        right.addWidget(self.status)
        layout.addLayout(right, 2)

        self.refresh()

    def _table_buttons(self, table, add_callback):
        buttons = QtWidgets.QHBoxLayout()
        add_button = QtWidgets.QPushButton("Add")
        add_button.clicked.connect(add_callback)
        remove_button = QtWidgets.QPushButton("Remove selected")
        remove_button.clicked.connect(table.remove_selected)
        buttons.addWidget(add_button)
        buttons.addWidget(remove_button)
        buttons.addStretch()
        return buttons

    def refresh(self):
        self.model_list.clear()
        for path in _spec_files():
            self.model_list.addItem(os.path.basename(path))

    def _new_model(self):
        spec = model_spec.builtin_templates()[self.template_combo.currentText()]
        self._populate(spec)
        self.status.setText("New model (unsaved) - rename and Save")

    def _load_selected(self, filename):
        if not filename:
            return
        try:
            spec = model_spec.load_spec(os.path.join(MODELS_DIR, filename))
            self._populate(spec)
            self.status.setText(f"Loaded {filename}")
        except Exception as e:
            self.status.setText(f"Load failed: {e}")

    def _populate(self, spec):
        self.name_edit.setText(spec["name"])
        self.inputs_table.set_variables(spec["inputs"])
        self.layers_table.set_layers(spec.get("layers", []))
        self.outputs_table.set_variables(spec["outputs"])
        self.output_activation.setCurrentText(spec.get("output_activation", "none"))
        self.epochs.setValue(int(spec["training"]["epochs"]))
        self.learning_rate.setValue(float(spec["training"]["learning_rate"]))
        self.batch_size.setValue(int(spec["training"]["batch_size"]))

    def current_spec(self):
        return model_spec.make_spec(
            self.name_edit.text().strip(),
            inputs=self.inputs_table.variables(),
            layers=self.layers_table.layers(),
            outputs=self.outputs_table.variables(),
            output_activation=self.output_activation.currentText(),
            epochs=self.epochs.value(),
            learning_rate=self.learning_rate.value(),
            batch_size=self.batch_size.value())

    def _save(self):
        try:
            spec = self.current_spec()
            path = os.path.join(MODELS_DIR, f"{spec['name']}.model.json")
            model_spec.save_spec(spec, path)
            self.refresh()
            self.status.setText(f"Saved {path}")
        except Exception as e:
            self.status.setText(f"Save failed: {e}")


class TrainTab(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.trainer = None
        layout = QtWidgets.QVBoxLayout(self)

        form = QtWidgets.QFormLayout()
        self.model_combo = QtWidgets.QComboBox()
        form.addRow("Model", self.model_combo)
        dataset_row = QtWidgets.QHBoxLayout()
        self.dataset_edit = QtWidgets.QLineEdit()
        browse = QtWidgets.QPushButton("Browse...")
        browse.clicked.connect(self._browse_dataset)
        dataset_row.addWidget(self.dataset_edit)
        dataset_row.addWidget(browse)
        form.addRow("Dataset (.jsonl)", dataset_row)
        layout.addLayout(form)

        buttons = QtWidgets.QHBoxLayout()
        self.start_button = QtWidgets.QPushButton("Start / Resume training")
        self.start_button.clicked.connect(self._start)
        self.stop_button = QtWidgets.QPushButton("Stop && Save")
        self.stop_button.clicked.connect(self._stop)
        self.stop_button.setEnabled(False)
        refresh = QtWidgets.QPushButton("Refresh models")
        refresh.clicked.connect(self.refresh)
        buttons.addWidget(self.start_button)
        buttons.addWidget(self.stop_button)
        buttons.addWidget(refresh)
        buttons.addStretch()
        layout.addLayout(buttons)

        self.progress = QtWidgets.QProgressBar()
        layout.addWidget(self.progress)
        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        layout.addWidget(self.log)

        hint = QtWidgets.QLabel(
            "A checkpoint is saved every epoch and on Stop && Save, so training can be\n"
            "interrupted at any time and resumed later from exactly where it stopped.\n"
            "Record datasets with aibackbone.recorder.DatasetRecorder (see gem README).")
        layout.addWidget(hint)

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._poll)
        self.refresh()

    def refresh(self):
        self.model_combo.clear()
        for path in _spec_files():
            self.model_combo.addItem(os.path.basename(path))

    def _browse_dataset(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, "Select dataset", MODELS_DIR, "Datasets (*.jsonl);;All files (*)")
        if path:
            self.dataset_edit.setText(path)

    def _start(self):
        available, _ = ml_available()
        if not available:
            _install_prompt(self, self.log)
            return
        model_file = self.model_combo.currentText()
        dataset = self.dataset_edit.text().strip()
        if not model_file or not dataset:
            self._log("Select a model and a dataset first.")
            return
        try:
            from . import torch_backend
            spec_path = os.path.join(MODELS_DIR, model_file)
            spec = model_spec.load_spec(spec_path)
            self.trainer = torch_backend.Trainer(spec, spec_path, dataset)
            self.trainer.start(resume=True)
            self.progress.setMaximum(int(spec["training"]["epochs"]))
            self.start_button.setEnabled(False)
            self.stop_button.setEnabled(True)
            self._log(f"Training {model_file} on {dataset} (resumes from checkpoint if present)...")
            self._timer.start(250)
        except Exception as e:
            self._log(f"Failed to start training: {e}")

    def _stop(self):
        if self.trainer:
            self.trainer.request_stop()
            self._log("Stop requested - saving checkpoint after the current epoch...")

    def _poll(self):
        if not self.trainer:
            return
        epoch, total, loss, status = self.trainer.progress()
        self.progress.setValue(epoch)
        if loss is not None:
            self._log(f"epoch {epoch}/{total}  loss {loss:.6f}  [{status}]", replace_last=True)
        if not self.trainer.is_running():
            self._timer.stop()
            error = self.trainer.error()
            if error:
                self._log(f"Training error: {error}")
            else:
                self._log(f"Done: {status}")
            self.start_button.setEnabled(True)
            self.stop_button.setEnabled(False)

    def _log(self, text, replace_last=False):
        if replace_last:
            cursor = self.log.textCursor()
            cursor.movePosition(cursor.MoveOperation.End)
            cursor.select(cursor.SelectionType.LineUnderCursor)
            cursor.removeSelectedText()
            cursor.insertText(text)
        else:
            self.log.appendPlainText(text)


class ImportOnnxTab(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QtWidgets.QVBoxLayout(self)
        row = QtWidgets.QHBoxLayout()
        self.path_edit = QtWidgets.QLineEdit()
        browse = QtWidgets.QPushButton("Browse...")
        browse.clicked.connect(self._browse)
        inspect_button = QtWidgets.QPushButton("Inspect model")
        inspect_button.clicked.connect(self._inspect)
        row.addWidget(self.path_edit)
        row.addWidget(browse)
        row.addWidget(inspect_button)
        layout.addLayout(row)

        layout.addWidget(QtWidgets.QLabel("Detected required inputs / outputs:"))
        self.io_table = QtWidgets.QTableWidget(0, 4)
        self.io_table.setHorizontalHeaderLabels(["Direction", "Name", "Dtype", "Shape"])
        self.io_table.horizontalHeader().setSectionResizeMode(1, QtWidgets.QHeaderView.Stretch)
        layout.addWidget(self.io_table)
        self.status = QtWidgets.QLabel(
            "Import any ONNX model (including open-source ones); its inputs/outputs are\n"
            "auto-detected. Run it in-engine with aibackbone.onnx_inspect.OnnxModel.")
        layout.addWidget(self.status)

    def _browse(self):
        path, _ = QtWidgets.QFileDialog.getOpenFileName(
            self, "Select ONNX model", MODELS_DIR, "ONNX models (*.onnx);;All files (*)")
        if path:
            self.path_edit.setText(path)
            self._inspect()

    def _inspect(self):
        available, _ = ml_available()
        if not available:
            _install_prompt(self, self.status)
            return
        path = self.path_edit.text().strip()
        if not path:
            return
        try:
            from . import onnx_inspect
            io = onnx_inspect.inspect_model(path)
            rows = [("input", d) for d in io["inputs"]] + [("output", d) for d in io["outputs"]]
            self.io_table.setRowCount(0)
            for direction, desc in rows:
                row = self.io_table.rowCount()
                self.io_table.insertRow(row)
                self.io_table.setItem(row, 0, QtWidgets.QTableWidgetItem(direction))
                self.io_table.setItem(row, 1, QtWidgets.QTableWidgetItem(desc["name"]))
                self.io_table.setItem(row, 2, QtWidgets.QTableWidgetItem(desc["dtype"]))
                self.io_table.setItem(row, 3, QtWidgets.QTableWidgetItem(str(desc["shape"])))
            self.status.setText(f"Inspected {os.path.basename(path)}")
        except Exception as e:
            self.status.setText(f"Inspect failed: {e}")


class AIModelBuilderDialog(QtWidgets.QDialog):
    """The 'AI Model Builder' Editor view pane."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("AI Model Builder")
        layout = QtWidgets.QVBoxLayout(self)

        self._settings = QtCore.QSettings("O3DE", "AIBackbone")
        self._install_queue = None
        self._install_thread = None
        self._restart_required = False
        self.install_banner = QtWidgets.QGroupBox("AI libraries")
        install_layout = QtWidgets.QVBoxLayout(self.install_banner)
        self.missing_label = QtWidgets.QLabel()
        self.missing_label.setWordWrap(True)
        install_layout.addWidget(self.missing_label)

        options = QtWidgets.QHBoxLayout()
        self.torch_build_combo = QtWidgets.QComboBox()
        choices = [
            ("Auto (detect GPU)", "auto"),
            ("CPU", "cpu"),
            ("CUDA 11.8", "cu118"),
            ("CUDA 12.6", "cu126"),
            ("CUDA 12.8", "cu128"),
        ]
        if "rocm6.3" in deps.TORCH_BUILDS:
            choices.append(("ROCm 6.3", "rocm6.3"))
        for label, value in choices:
            self.torch_build_combo.addItem(label, value)
        selected_build = self._settings.value("torch_build", "auto")
        selected_index = self.torch_build_combo.findData(selected_build)
        self.torch_build_combo.setCurrentIndex(max(0, selected_index))
        options.addWidget(QtWidgets.QLabel("PyTorch build:"))
        options.addWidget(self.torch_build_combo)
        options.addWidget(QtWidgets.QLabel("Torch version:"))
        self.torch_version_combo = QtWidgets.QComboBox()
        self.torch_version_combo.setEditable(True)
        self.torch_version_combo.addItem("")
        self.torch_version_combo.addItems(("2.8.0", "2.7.1", "2.7.0"))
        self.torch_version_combo.setCurrentText(
            self._settings.value("torch_version", ""))
        options.addWidget(self.torch_version_combo)
        self.pinned_checkbox = QtWidgets.QCheckBox("Use tested versions")
        self.pinned_checkbox.setChecked(self._settings.value("pinned", False, type=bool))
        options.addWidget(self.pinned_checkbox)
        self.install_button = QtWidgets.QPushButton("Install missing libraries")
        self.install_button.clicked.connect(self.start_install)
        options.addWidget(self.install_button)
        install_layout.addLayout(options)

        self.install_log = QtWidgets.QPlainTextEdit()
        self.install_log.setReadOnly(True)
        self.install_log.setMaximumHeight(140)
        install_layout.addWidget(self.install_log)
        self._install_timer = QtCore.QTimer(self)
        self._install_timer.timeout.connect(self._poll_install)
        self.torch_build_combo.currentIndexChanged.connect(self._save_settings)
        self.torch_version_combo.currentTextChanged.connect(self._save_settings)
        self.pinned_checkbox.toggled.connect(self._save_settings)

        available, _ = ml_available()
        self.install_banner.setVisible(not available)
        self._update_missing_label()
        layout.addWidget(self.install_banner)

        tabs = QtWidgets.QTabWidget()
        tabs.addTab(ModelsTab(), "Models")
        tabs.addTab(TrainTab(), "Train")
        tabs.addTab(ImportOnnxTab(), "Import ONNX")
        layout.addWidget(tabs)

    def _save_settings(self, *unused):
        self._settings.setValue("torch_build", self.torch_build_combo.currentData())
        self._settings.setValue("torch_version", self.torch_version_combo.currentText().strip())
        self._settings.setValue("pinned", self.pinned_checkbox.isChecked())

    def _update_missing_label(self):
        missing = deps.missing_libs()
        if missing:
            self.missing_label.setText("Missing Python libraries: " + ", ".join(missing))
        else:
            self.missing_label.setText("All required AI libraries are installed.")

    def start_install(self):
        if self._install_thread and self._install_thread.is_alive():
            return
        self._save_settings()
        self._restart_required = any(name in sys.modules for name in ("torch", "numpy"))
        self.install_log.clear()
        self.install_button.setEnabled(False)
        self.install_banner.setVisible(True)
        self._install_queue = queue.Queue()
        torch_build = self.torch_build_combo.currentData()
        pinned = self.pinned_checkbox.isChecked()
        torch_version = self.torch_version_combo.currentText().strip() or None

        def run_install():
            try:
                result = deps.install(
                    torch_build, pinned, torch_version=torch_version,
                    log=lambda line: self._install_queue.put(("log", line)),
                    only_missing=True)
            except Exception as exc:
                self._install_queue.put(("log", f"Install failed: {exc}"))
                result = False, deps.missing_libs()
            self._install_queue.put(("done", result))

        self._install_thread = threading.Thread(target=run_install, daemon=True)
        self._install_thread.start()
        self._install_timer.start(100)

    def _poll_install(self):
        while True:
            try:
                kind, value = self._install_queue.get_nowait()
            except queue.Empty:
                return
            if kind == "log":
                self.install_log.appendPlainText(value)
                continue

            self._install_timer.stop()
            self.install_button.setEnabled(True)
            ok, missing_after = value
            self._update_missing_label()
            self.install_banner.setVisible(bool(missing_after))
            if ok:
                self.install_log.appendPlainText("All required AI libraries are installed.")
                if self._restart_required:
                    QtWidgets.QMessageBox.information(
                        self, "Restart Editor",
                        "torch or numpy was already imported in this Editor process. "
                        "Restart the Editor before using the newly installed libraries.")
            elif missing_after:
                self.install_log.appendPlainText(
                    "Still missing: " + ", ".join(missing_after))
            return
