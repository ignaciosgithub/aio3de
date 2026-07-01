"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
# PyTorch backend: turns a model spec (model_spec.py) into a real nn.Module,
# trains it on recorded datasets (recorder.py), and exports to ONNX.
#
# Training is checkpoint-first: a checkpoint (weights + optimizer state + epoch)
# is written after every epoch and on request, so a run can be stopped at any
# point and resumed later exactly where it left off.
#
# torch/numpy are imported lazily so the Editor tool can open (and explain what
# to install) even before install_ai_libs has been run.

import json
import os
import threading

from . import model_spec


def _torch():
    import torch  # noqa: F401  (lazy import; see module docstring)
    return torch


def build_model(spec):
    """Builds an nn.Sequential from a validated model spec."""
    torch = _torch()
    import torch.nn as nn

    model_spec.validate_spec(spec)
    in_width = model_spec.vector_width(spec["inputs"])
    out_width = model_spec.vector_width(spec["outputs"])

    activations = {
        "relu": nn.ReLU,
        "tanh": nn.Tanh,
        "sigmoid": nn.Sigmoid,
        "leaky_relu": nn.LeakyReLU,
    }

    layers = []
    width = in_width
    for layer in spec.get("layers", []):
        units = int(layer["units"])
        layers.append(nn.Linear(width, units))
        if layer["activation"] != "none":
            layers.append(activations[layer["activation"]]())
        width = units
    layers.append(nn.Linear(width, out_width))

    out_act = spec.get("output_activation", "none")
    if out_act == "sigmoid":
        layers.append(nn.Sigmoid())
    elif out_act == "tanh":
        layers.append(nn.Tanh())
    elif out_act == "softmax":
        layers.append(nn.Softmax(dim=-1))

    return nn.Sequential(*layers)


def _flatten_sample(variables, sample):
    """Flattens one recorded sample dict into a float list ordered by the spec's variables."""
    values = []
    for var in variables:
        name = var["name"]
        if name not in sample:
            raise KeyError(f"Dataset sample is missing field '{name}'")
        value = sample[name]
        width = model_spec.VARIABLE_TYPES[var["type"]]
        if width == 1:
            values.append(float(value))
        else:
            seq = list(value)
            if len(seq) != width:
                raise ValueError(f"Field '{name}' should have {width} components, got {len(seq)}")
            values.extend(float(v) for v in seq)
    return values


def load_dataset(spec, dataset_path):
    """Loads a recorder JSONL dataset into (inputs, targets) tensors matching the spec."""
    torch = _torch()
    xs, ys = [], []
    with open(dataset_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            sample = json.loads(line)
            xs.append(_flatten_sample(spec["inputs"], sample.get("inputs", sample)))
            ys.append(_flatten_sample(spec["outputs"], sample.get("outputs", sample)))
    if not xs:
        raise ValueError(f"Dataset '{dataset_path}' contains no samples")
    return torch.tensor(xs, dtype=torch.float32), torch.tensor(ys, dtype=torch.float32)


def checkpoint_path(spec_path):
    return os.path.splitext(spec_path)[0] + ".checkpoint.pt"


def weights_path(spec_path):
    return os.path.splitext(spec_path)[0] + ".pt"


def onnx_path(spec_path):
    return os.path.splitext(spec_path)[0] + ".onnx"


class Trainer:
    """Background-thread trainer with stop-and-save and resume-from-checkpoint.

    Usage:
        trainer = Trainer(spec, spec_path, dataset_path)
        trainer.start(resume=True)      # resumes from <spec>.checkpoint.pt if present
        trainer.request_stop()          # stops after the current epoch, saving a checkpoint
        trainer.progress()              # -> (epoch, total_epochs, last_loss, status)

    A checkpoint (weights + optimizer state + epoch) is written after every epoch,
    so even a hard kill loses at most one epoch of work.
    """

    def __init__(self, spec, spec_path, dataset_path, on_progress=None):
        self.spec = model_spec.validate_spec(spec)
        self.spec_path = spec_path
        self.dataset_path = dataset_path
        self.on_progress = on_progress  # optional callback(epoch, total, loss, status)
        self._stop_requested = threading.Event()
        self._lock = threading.Lock()
        self._thread = None
        self._epoch = 0
        self._loss = None
        self._status = "idle"
        self._error = None

    def start(self, resume=True):
        if self._thread and self._thread.is_alive():
            raise RuntimeError("Training already running")
        self._stop_requested.clear()
        self._thread = threading.Thread(target=self._run, args=(resume,), daemon=True)
        self._thread.start()

    def request_stop(self):
        """Asks the trainer to stop after the current epoch; a checkpoint is saved."""
        self._stop_requested.set()

    def is_running(self):
        return bool(self._thread and self._thread.is_alive())

    def progress(self):
        with self._lock:
            return self._epoch, int(self.spec["training"]["epochs"]), self._loss, self._status

    def error(self):
        with self._lock:
            return self._error

    def _report(self, epoch, total, loss, status):
        with self._lock:
            self._epoch, self._loss, self._status = epoch, loss, status
        if self.on_progress:
            try:
                self.on_progress(epoch, total, loss, status)
            except Exception:
                pass

    def _run(self, resume):
        try:
            self._train(resume)
        except Exception as e:  # surfaced through error() / status, never crashes the Editor
            with self._lock:
                self._error = str(e)
                self._status = "error"

    def _train(self, resume):
        torch = _torch()

        model = build_model(self.spec)
        training = self.spec["training"]
        optimizer = torch.optim.Adam(model.parameters(), lr=float(training["learning_rate"]))
        loss_fn = torch.nn.MSELoss()
        total_epochs = int(training["epochs"])
        start_epoch = 0

        ckpt_file = checkpoint_path(self.spec_path)
        if resume and os.path.exists(ckpt_file):
            checkpoint = torch.load(ckpt_file, weights_only=False)
            model.load_state_dict(checkpoint["model_state"])
            optimizer.load_state_dict(checkpoint["optimizer_state"])
            start_epoch = int(checkpoint.get("epoch", 0))

        if start_epoch >= total_epochs:
            self._report(start_epoch, total_epochs, None, "finished (checkpoint already complete)")
            self._save_final(model)
            return

        self._report(start_epoch, total_epochs, None, "loading dataset")
        inputs, targets = load_dataset(self.spec, self.dataset_path)
        batch_size = max(1, int(training["batch_size"]))

        for epoch in range(start_epoch, total_epochs):
            permutation = torch.randperm(inputs.shape[0])
            epoch_loss = 0.0
            batches = 0
            for begin in range(0, inputs.shape[0], batch_size):
                idx = permutation[begin:begin + batch_size]
                optimizer.zero_grad()
                loss = loss_fn(model(inputs[idx]), targets[idx])
                loss.backward()
                optimizer.step()
                epoch_loss += float(loss.item())
                batches += 1

            completed = epoch + 1
            self._save_checkpoint(model, optimizer, completed)
            self._report(completed, total_epochs, epoch_loss / max(1, batches), "training")

            if self._stop_requested.is_set():
                self._save_final(model)
                self._report(completed, total_epochs, epoch_loss / max(1, batches),
                             f"stopped and saved at epoch {completed}/{total_epochs} (resume to continue)")
                return

        self._save_final(model)
        self._report(total_epochs, total_epochs, self._loss, "finished")

    def _save_checkpoint(self, model, optimizer, epoch):
        torch = _torch()
        torch.save(
            {
                "spec": self.spec,
                "epoch": epoch,
                "model_state": model.state_dict(),
                "optimizer_state": optimizer.state_dict(),
            },
            checkpoint_path(self.spec_path))

    def _save_final(self, model):
        torch = _torch()
        torch.save(model.state_dict(), weights_path(self.spec_path))
        try:
            export_onnx(self.spec, model, onnx_path(self.spec_path))
        except Exception:
            pass  # ONNX export is best-effort; weights are already saved


def load_trained_model(spec, spec_path):
    """Loads a model with the final trained weights (falls back to the latest checkpoint)."""
    torch = _torch()
    model = build_model(spec)
    final = weights_path(spec_path)
    ckpt = checkpoint_path(spec_path)
    if os.path.exists(final):
        model.load_state_dict(torch.load(final, weights_only=True))
    elif os.path.exists(ckpt):
        model.load_state_dict(torch.load(ckpt, weights_only=False)["model_state"])
    else:
        raise FileNotFoundError(f"No trained weights found for {spec_path}")
    model.eval()
    return model


def export_onnx(spec, model, path):
    torch = _torch()
    example = torch.zeros(1, model_spec.vector_width(spec["inputs"]), dtype=torch.float32)
    kwargs = dict(
        input_names=["inputs"], output_names=["outputs"],
        dynamic_axes={"inputs": {0: "batch"}, "outputs": {0: "batch"}})
    try:
        torch.onnx.export(model, example, path, **kwargs)
    except (ImportError, ModuleNotFoundError):
        # Newer torch defaults to the dynamo exporter (needs onnxscript);
        # fall back to the classic TorchScript exporter.
        torch.onnx.export(model, example, path, dynamo=False, **kwargs)


def predict(spec, model, sample):
    """Runs one sample (dict of input fields) through the model, returning a dict of output fields."""
    torch = _torch()
    x = torch.tensor([_flatten_sample(spec["inputs"], sample)], dtype=torch.float32)
    with torch.no_grad():
        y = model(x)[0].tolist()
    result = {}
    cursor = 0
    for var in spec["outputs"]:
        width = model_spec.VARIABLE_TYPES[var["type"]]
        result[var["name"]] = y[cursor] if width == 1 else y[cursor:cursor + width]
        cursor += width
    return result
