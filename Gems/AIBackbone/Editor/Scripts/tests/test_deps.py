"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""

import importlib.util
from pathlib import Path
import subprocess
import sys

import pytest

SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from aibackbone import deps  # noqa: E402


@pytest.fixture
def all_missing(monkeypatch):
    monkeypatch.setattr(importlib.util, "find_spec", lambda name: None)


def test_missing_libs_uses_find_spec_and_invalidates_caches(monkeypatch):
    invalidated = []
    monkeypatch.setattr(deps.importlib, "invalidate_caches", lambda: invalidated.append(True))
    monkeypatch.setattr(importlib.util, "find_spec",
                        lambda name: object() if name in ("numpy", "onnx") else None)

    assert deps.missing_libs() == ("torch", "onnxscript", "onnxruntime")
    assert invalidated == [True]


@pytest.mark.parametrize("build", ["cpu", "cu128"])
def test_build_commands_selects_torch_index(build, monkeypatch, all_missing):
    monkeypatch.setattr(deps, "engine_python", lambda: "/engine/python/python.sh")

    commands = deps.build_commands(build, pinned=False)

    assert commands[0][:7] == [
        "/engine/python/python.sh", "-m", "pip", "install",
        "--index-url", deps.TORCH_BUILDS[build], "--extra-index-url",
    ]
    assert commands[0][7] == "https://pypi.org/simple"
    assert commands[0][8:10] == ["--only-binary=:all:", "torch"]
    assert commands[1][4:] == ["numpy", "onnx", "onnxscript", "onnxruntime"]


def test_build_commands_uses_pinned_versions(monkeypatch, all_missing):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")

    commands = deps.build_commands("cpu", pinned=True)

    assert commands[0][-1] == "torch==2.8.0"
    assert commands[1][4:] == [
        "numpy==1.24.4", "onnx==1.17.0", "onnxscript==0.2.2", "onnxruntime==1.20.1",
    ]


def test_torch_version_is_used_with_selected_build_index(monkeypatch, all_missing):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")

    command = deps.build_commands(
        "cu126", pinned=False, torch_version="2.6.0")[0]

    assert command[command.index("--index-url") + 1] == deps.TORCH_BUILDS["cu126"]
    assert command[-1] == "torch==2.6.0"


def test_torch_version_overrides_pinned_torch(monkeypatch, all_missing):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")

    command = deps.build_commands(
        "cpu", pinned=True, torch_version="2.7.1")[0]

    assert command[-1] == "torch==2.7.1"


def test_invalid_torch_version_is_rejected():
    with pytest.raises(ValueError, match="PyTorch version must match"):
        deps.build_commands("cpu", pinned=False, torch_version="2.7.1+cpu")


def test_only_missing_skips_torch_when_present(monkeypatch):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")
    monkeypatch.setattr(
        importlib.util, "find_spec",
        lambda name: object() if name == "torch" else None)

    commands = deps.build_commands("cpu", pinned=False, only_missing=True)

    assert len(commands) == 1
    assert commands[0][4:] == ["numpy", "onnx", "onnxscript", "onnxruntime"]
    assert all("torch" not in command[-1:] for command in commands)


def test_auto_selects_cuda_when_nvidia_smi_succeeds(monkeypatch):
    run_calls = []
    monkeypatch.setattr(deps.shutil, "which", lambda name: "/usr/bin/nvidia-smi")
    monkeypatch.setattr(deps.subprocess, "run",
                        lambda *args, **kwargs: run_calls.append(args[0]))

    assert deps.resolve_torch_build("auto") == "cu128"
    assert run_calls == [["/usr/bin/nvidia-smi"]]


def test_auto_selects_cpu_without_working_nvidia_smi(monkeypatch):
    monkeypatch.setattr(deps.shutil, "which", lambda name: None)
    assert deps.resolve_torch_build("auto") == "cpu"

    monkeypatch.setattr(deps.shutil, "which", lambda name: "/usr/bin/nvidia-smi")

    def failed_run(*args, **kwargs):
        raise subprocess.CalledProcessError(1, args[0])

    monkeypatch.setattr(deps.subprocess, "run", failed_run)
    assert deps.resolve_torch_build("auto") == "cpu"


def test_engine_python_fallback_walk(tmp_path, monkeypatch):
    engine_root = tmp_path / "engine"
    module = engine_root / "Gems/AIBackbone/Editor/Scripts/aibackbone/deps.py"
    module.parent.mkdir(parents=True)
    launcher = engine_root / "python/python.sh"
    launcher.parent.mkdir()
    launcher.touch()
    monkeypatch.setattr(deps, "__file__", str(module))

    assert deps.engine_python() == str(launcher)


@pytest.mark.parametrize(("missing", "expected"), [
    (("torch",), 1),
    ((), 0),
])
def test_check_exit_status(monkeypatch, capsys, missing, expected):
    monkeypatch.setattr(importlib.util, "find_spec",
                        lambda name: None if name in missing else object())

    assert deps.main(["--check"]) == expected
    assert capsys.readouterr().out
