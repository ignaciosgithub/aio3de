"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""
"""Dependency detection and installation for AIBackbone."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

REQUIRED = ("numpy", "torch", "onnx", "onnxscript", "onnxruntime")

TORCH_BUILDS = {
    "cpu": "https://download.pytorch.org/whl/cpu",
    "cu118": "https://download.pytorch.org/whl/cu118",
    "cu126": "https://download.pytorch.org/whl/cu126",
    "cu128": "https://download.pytorch.org/whl/cu128",
}
if sys.platform.startswith("linux"):
    TORCH_BUILDS["rocm6.3"] = "https://download.pytorch.org/whl/rocm6.3"

_PINNED_FILE = Path(__file__).resolve().parents[3] / "requirements-ai-pinned.txt"
_TORCH_VERSION_PATTERN = re.compile(r"^\d+\.\d+(\.\d+)?$")


def missing_libs():
    """Return required import names which are not available in this interpreter."""
    importlib.invalidate_caches()
    return tuple(name for name in REQUIRED if importlib.util.find_spec(name) is None)


def resolve_torch_build(torch_build):
    if torch_build == "auto":
        nvidia_smi = shutil.which("nvidia-smi")
        if nvidia_smi:
            try:
                subprocess.run(
                    [nvidia_smi], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    check=True, timeout=5)
                return "cu128"
            except (OSError, subprocess.SubprocessError):
                pass
        return "cpu"
    if torch_build not in TORCH_BUILDS:
        raise ValueError(f"Unsupported PyTorch build: {torch_build}")
    return torch_build


def engine_python():
    """Return the engine's Python launcher, preferring the Editor's engine root."""
    launcher = "python.cmd" if os.name == "nt" else "python.sh"
    try:
        import azlmbr.paths
        candidate = Path(azlmbr.paths.engroot) / "python" / launcher
        if candidate.is_file():
            return str(candidate)
    except (ImportError, AttributeError, TypeError):
        pass

    for parent in (Path(__file__).resolve().parent, *Path(__file__).resolve().parents):
        candidate = parent / "python" / launcher
        if candidate.is_file():
            return str(candidate)
    raise FileNotFoundError(f"Could not locate engine Python ({launcher})")


def _pinned_versions():
    versions = {}
    with _PINNED_FILE.open("r", encoding="utf-8") as requirements:
        for line in requirements:
            line = line.partition("#")[0].strip()
            if not line:
                continue
            name, separator, version = line.partition("==")
            if not separator or not name.strip() or not version.strip():
                raise ValueError(f"Expected an exact package pin, got: {line}")
            versions[name.strip()] = version.strip()
    return versions


def _validate_torch_version(torch_version):
    if torch_version is None:
        return None
    if not isinstance(torch_version, str) or not _TORCH_VERSION_PATTERN.fullmatch(torch_version):
        raise ValueError("PyTorch version must match X.Y or X.Y.Z (for example, 2.6.0)")
    return torch_version


def build_commands(torch_build, pinned, only_missing=True, torch_version=None):
    """Build pip argv lists, installing torch separately from PyPI packages."""
    selected = resolve_torch_build(torch_build)
    torch_version = _validate_torch_version(torch_version)
    missing = set(missing_libs()) if only_missing else set(REQUIRED)
    if pinned:
        versions = _pinned_versions()
    else:
        versions = {}

    commands = []
    python = engine_python()
    if "torch" in missing:
        version = torch_version or (versions["torch"] if pinned else None)
        torch_spec = "torch==" + version if version else "torch"
        commands.append([
            python, "-m", "pip", "install", "--index-url", TORCH_BUILDS[selected],
            "--extra-index-url", "https://pypi.org/simple", "--only-binary=:all:", torch_spec,
        ])

    packages = []
    for name in REQUIRED:
        if name != "torch" and name in missing:
            packages.append(f"{name}=={versions[name]}" if pinned else name)
    if packages:
        commands.append([python, "-m", "pip", "install", *packages])
    return commands


def install(torch_build, pinned, log=print, only_missing=True, torch_version=None):
    """Install missing libraries, streaming pip output to ``log``."""
    ok = True
    requested_version = _validate_torch_version(torch_version)
    if requested_version is None and pinned:
        requested_version = _pinned_versions().get("torch")
    for command in build_commands(
            torch_build, pinned, only_missing, torch_version=torch_version):
        log("[AIBackbone] Running: " + " ".join(command))
        try:
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                text=True, bufsize=1)
            torch_command = command[-1].split("==", 1)[0] == "torch"
            availability_reported = False
            for line in process.stdout:
                log(line.rstrip())
                line_lower = line.lower()
                if (torch_command and not availability_reported and
                        ("no matching distribution" in line_lower or
                         "could not find a version that satisfies" in line_lower) and
                        "torch" in line_lower):
                    version_text = requested_version or "the selected latest release"
                    log(
                        f"[AIBackbone] PyTorch {version_text} is not available for "
                        f"build {resolve_torch_build(torch_build)} or engine Python 3.10.")
                    availability_reported = True
            return_code = process.wait()
        except OSError as exc:
            log(f"[AIBackbone] Install failed: {exc}")
            ok = False
            break
        if return_code:
            log(f"[AIBackbone] pip exited with code {return_code}")
            ok = False
            break

    missing_after = missing_libs()
    return ok and not missing_after, missing_after


def ensure_ml_libs(
        auto_install=False, torch_build="auto", pinned=False, log=print, torch_version=None):
    """Check required libraries, optionally installing missing packages."""
    _validate_torch_version(torch_version)
    missing = missing_libs()
    if not missing or not auto_install:
        return not missing, missing
    return install(
        torch_build, pinned, log=log, only_missing=True, torch_version=torch_version)


def _arg_torch_version(value):
    try:
        return _validate_torch_version(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def main(argv=None):
    builds = ["auto", *TORCH_BUILDS]
    parser = argparse.ArgumentParser(description="Install AIBackbone Python libraries.")
    parser.add_argument("--torch", choices=builds, default="auto", metavar="BUILD")
    parser.add_argument("--torch-version", type=_arg_torch_version, metavar="X.Y.Z")
    parser.add_argument("--pinned", action="store_true", help="use tested exact package versions")
    parser.add_argument("--all", action="store_true", help="reinstall libraries already present")
    parser.add_argument("--check", action="store_true", help="only report missing libraries")
    args = parser.parse_args(argv)

    if args.check:
        missing = missing_libs()
        if missing:
            print("Missing libraries: " + ", ".join(missing))
            return 1
        print("All required AI libraries are available.")
        return 0

    ok, missing = install(
        args.torch, args.pinned, only_missing=not args.all,
        torch_version=args.torch_version)
    if ok:
        print("All required AI libraries are available.")
        return 0
    print("Missing libraries: " + ", ".join(missing))
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
