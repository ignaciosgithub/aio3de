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

    assert commands[0] == [
        "/engine/python/python.sh", "-m", "pip", "install", "--no-deps",
        "--index-url", deps.TORCH_BUILDS[build], "--only-binary=:all:", "torch",
    ]
    assert "pypi.org" not in " ".join(commands[0])
    assert commands[1] == [
        "/engine/python/python.sh", "-m", "pip", "install",
        "--index-url", deps.TORCH_BUILDS[build], "--extra-index-url",
        "https://pypi.org/simple", "--only-binary=:all:", "torch",
    ]
    assert commands[0][-1] == commands[1][-1]
    assert commands[2][4:] == ["numpy", "onnx", "onnxscript", "onnxruntime"]


@pytest.mark.parametrize(("only_missing", "force_reinstall"), [(True, False), (False, True)])
def test_force_reinstall_is_only_on_torch_selection_phase(
        only_missing, force_reinstall, monkeypatch, all_missing):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")

    commands = deps.build_commands(
        "cpu", pinned=False, only_missing=only_missing)

    assert ("--force-reinstall" in commands[0]) is force_reinstall
    assert all("--force-reinstall" not in command for command in commands[1:])


def test_build_commands_uses_pinned_versions(monkeypatch, all_missing):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")

    commands = deps.build_commands("cpu", pinned=True)

    assert commands[0][-1] == "torch==2.8.0"
    assert commands[1][-1] == "torch==2.8.0"
    assert commands[2][4:] == [
        "numpy==1.24.4", "onnx==1.17.0", "onnxscript==0.2.2", "onnxruntime==1.20.1",
    ]


def test_pinned_commands_include_numpy_when_already_installed(monkeypatch):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")
    monkeypatch.setattr(importlib.util, "find_spec", lambda name: object())

    commands = deps.build_commands("cpu", pinned=True)

    assert commands == [[
        "python", "-m", "pip", "install", "numpy==1.24.4",
    ]]


def test_unpinned_only_missing_preserves_installed_numpy(monkeypatch):
    monkeypatch.setattr(deps, "engine_python", lambda: "python")
    monkeypatch.setattr(
        importlib.util, "find_spec",
        lambda name: object() if name == "numpy" else None)
    monkeypatch.setattr(deps.importlib.metadata, "version", lambda name: "1.24.4")

    commands = deps.build_commands("cpu", pinned=False, only_missing=True)

    assert commands[2][4:] == [
        "numpy==1.24.4", "onnx", "onnxscript", "onnxruntime",
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


def test_cuda_alias_parses_with_check(monkeypatch, capsys):
    monkeypatch.setattr(deps, "missing_libs", lambda: ())

    assert deps.main(["--cuda", "--check"]) == 0
    assert "All required AI libraries are available." in capsys.readouterr().out


def test_cuda_alias_selects_cu128_for_install(monkeypatch):
    install_args = {}

    def fake_install(torch_build, pinned, **kwargs):
        install_args.update(torch_build=torch_build, pinned=pinned, **kwargs)
        return True, ()

    monkeypatch.setattr(deps, "install", fake_install)

    assert deps.main(["--cuda"]) == 0
    assert install_args["torch_build"] == "cu128"


def test_unavailable_torch_version_logs_message_in_selection_phase(monkeypatch):
    class FailedProcess:
        stdout = ["ERROR: No matching distribution found for torch==2.8.0\n"]

        def wait(self):
            return 1

    commands = []
    monkeypatch.setattr(deps, "engine_python", lambda: "python")
    monkeypatch.setattr(deps, "missing_libs", lambda: ("torch",))
    monkeypatch.setattr(deps.importlib.metadata, "version", lambda name: "1.24.4")
    monkeypatch.setattr(
        deps.subprocess, "Popen",
        lambda command, **kwargs: (commands.append(command) or FailedProcess()))
    messages = []

    ok, missing = deps.install(
        "cu118", False, log=messages.append, torch_version="2.8.0")

    assert not ok
    assert missing == ("torch",)
    assert len(commands) == 1
    assert "--no-deps" in commands[0]
    assert any("PyTorch 2.8.0 is not available for build cu118" in line
               for line in messages)
