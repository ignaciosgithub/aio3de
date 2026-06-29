#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""
The "engine hub" sub-command: a single, cross-platform (Windows + Linux + macOS) entry point that

  * checks that the host has everything needed to configure and build the engine ("o3de hub doctor"),
  * shows how registered engines and projects relate to each other ("o3de hub status"), and
  * finds the engine (and reports missing gem dependencies) a given project needs ("o3de hub resolve").

It deliberately builds on the existing manifest / compatibility modules instead of duplicating their
logic; the new value it adds is the build-prerequisite "doctor" (which O3DE otherwise leaves to prose
in the docs) and a consolidated project<->engine separation view.
"""

import argparse
import logging
import os
import pathlib
import re
import shutil
import subprocess
import sys

from o3de import manifest, compatibility, utils

logger = logging.getLogger('o3de.hub')
logging.basicConfig(format=utils.LOG_FORMAT)

# Result severities, ordered so the worst one can drive the process exit code.
OK = 'OK'
WARN = 'WARN'
FAIL = 'FAIL'
_SEVERITY = {OK: 0, WARN: 1, FAIL: 2}

# Minimum versions the engine relies on. The CMake floor is read from the engine's CMakeLists.txt at
# runtime (this is just the fallback if that file can not be parsed).
DEFAULT_MIN_CMAKE_VERSION = '3.24'
MIN_PYTHON_VERSION = (3, 10)
# A full source build of the engine + a project pulls large 3rd party packages and intermediate
# artifacts; warn when the engine drive has noticeably less than this free.
RECOMMENDED_FREE_GB = 60


class CheckResult:
    """A single doctor check outcome plus an optional remediation hint."""
    def __init__(self, name: str, severity: str, detail: str, hint: str = ''):
        self.name = name
        self.severity = severity
        self.detail = detail
        self.hint = hint


def _run_version_command(executable: str, args: list) -> str or None:
    """Runs '<executable> <args...>' and returns its combined output, or None if it can not be run."""
    resolved = shutil.which(executable)
    if not resolved:
        return None
    try:
        completed = subprocess.run([resolved] + args, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return None
    return (completed.stdout or '') + (completed.stderr or '')


def _parse_first_version(text: str) -> str or None:
    if not text:
        return None
    match = re.search(r'(\d+\.\d+(?:\.\d+)?)', text)
    return match.group(1) if match else None


def _version_tuple(version_string: str) -> tuple:
    return tuple(int(part) for part in version_string.split('.'))


def get_required_cmake_version(engine_path: pathlib.Path) -> str:
    """Reads the cmake_minimum_required version from the engine's top level CMakeLists.txt."""
    cmake_lists = pathlib.Path(engine_path) / 'CMakeLists.txt'
    try:
        contents = cmake_lists.read_text(encoding='utf-8', errors='ignore')
    except OSError:
        return DEFAULT_MIN_CMAKE_VERSION
    match = re.search(r'cmake_minimum_required\s*\(\s*VERSION\s+(\d+\.\d+(?:\.\d+)?)', contents, re.IGNORECASE)
    return match.group(1) if match else DEFAULT_MIN_CMAKE_VERSION


def check_python() -> CheckResult:
    current = sys.version_info[:3]
    detail = '.'.join(str(part) for part in current)
    if current[:2] < MIN_PYTHON_VERSION:
        required = '.'.join(str(part) for part in MIN_PYTHON_VERSION)
        return CheckResult('Python', FAIL, f'found {detail}, need >= {required}',
                           'Use the engine-provided Python (python/get_python.* ) or install a newer interpreter.')
    return CheckResult('Python', OK, detail)


def check_cmake(engine_path: pathlib.Path) -> CheckResult:
    required = get_required_cmake_version(engine_path)
    output = _run_version_command('cmake', ['--version'])
    if output is None:
        return CheckResult('CMake', FAIL, 'not found on PATH',
                           f'Install CMake >= {required} and ensure it is on PATH (https://cmake.org/download/).')
    found = _parse_first_version(output)
    if not found:
        return CheckResult('CMake', WARN, 'installed but version could not be parsed', f'Verify it is >= {required}.')
    if _version_tuple(found) < _version_tuple(required):
        return CheckResult('CMake', FAIL, f'found {found}, need >= {required}',
                           'The OS package is often too old; install a newer build from cmake.org or Kitware APT.')
    return CheckResult('CMake', OK, found)


def check_compiler() -> CheckResult:
    if sys.platform.startswith('win'):
        if shutil.which('cl'):
            return CheckResult('C++ compiler', OK, 'MSVC (cl) on PATH')
        program_files_x86 = os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')
        vswhere = pathlib.Path(program_files_x86) / 'Microsoft Visual Studio' / 'Installer' / 'vswhere.exe'
        if vswhere.is_file():
            output = _run_version_command(str(vswhere),
                                          ['-latest', '-products', '*',
                                           '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                           '-property', 'installationVersion'])
            found = _parse_first_version(output) if output else None
            if found:
                return CheckResult('C++ compiler', OK, f'Visual Studio {found} (VC++ tools installed)')
        return CheckResult('C++ compiler', FAIL, 'Visual Studio C++ toolset not detected',
                           'Install Visual Studio with the "Desktop development with C++" workload.')
    # Linux / macOS
    for compiler in ('clang', 'gcc', 'cc'):
        output = _run_version_command(compiler, ['--version'])
        if output:
            return CheckResult('C++ compiler', OK, f'{compiler}: {_parse_first_version(output) or "found"}')
    return CheckResult('C++ compiler', FAIL, 'no clang/gcc found on PATH',
                       'Install clang (recommended) or gcc, e.g. "sudo apt install clang lld".')


def check_ninja() -> CheckResult:
    output = _run_version_command('ninja', ['--version'])
    if output is None:
        return CheckResult('Ninja', WARN, 'not found on PATH',
                           'Optional but recommended for fast incremental builds (install "ninja-build").')
    return CheckResult('Ninja', OK, _parse_first_version(output) or 'found')


def check_git() -> CheckResult:
    output = _run_version_command('git', ['--version'])
    if output is None:
        return CheckResult('Git', FAIL, 'not found on PATH', 'Install Git (https://git-scm.com/downloads).')
    return CheckResult('Git', OK, _parse_first_version(output) or 'found')


def check_git_lfs() -> CheckResult:
    # git-lfs may be a standalone binary or only reachable as the "git lfs" subcommand.
    if shutil.which('git-lfs'):
        output = _run_version_command('git-lfs', ['version'])
        return CheckResult('Git LFS', OK, _parse_first_version(output or '') or 'found')
    output = _run_version_command('git', ['lfs', 'version'])
    if output and 'git-lfs' in output.lower():
        return CheckResult('Git LFS', OK, _parse_first_version(output) or 'found')
    return CheckResult('Git LFS', FAIL, 'not installed',
                       'This engine requires Git LFS. Install it and run "git lfs install" (https://git-lfs.com).')


def get_third_party_path() -> pathlib.Path or None:
    """Resolves the configured 3rd party package path without creating it."""
    env_path = os.environ.get('LY_3RDPARTY_PATH')
    if env_path:
        return pathlib.Path(env_path)
    default_path = manifest.get_o3de_folder() / '3rdParty'
    return default_path


def check_third_party() -> CheckResult:
    third_party_path = get_third_party_path()
    source = 'LY_3RDPARTY_PATH' if os.environ.get('LY_3RDPARTY_PATH') else 'default (~/.o3de/3rdParty)'
    if third_party_path and third_party_path.is_dir():
        return CheckResult('3rd party packages', OK, f'{third_party_path} [{source}]')
    return CheckResult('3rd party packages', WARN,
                       f'{third_party_path} does not exist yet [{source}]',
                       'It will be created on first configure; set LY_3RDPARTY_PATH to relocate the cache.')


def check_disk_space(engine_path: pathlib.Path) -> CheckResult:
    try:
        usage = shutil.disk_usage(str(engine_path))
    except OSError:
        return CheckResult('Disk space', WARN, 'could not be determined')
    free_gb = usage.free / (1024 ** 3)
    if free_gb < RECOMMENDED_FREE_GB:
        return CheckResult('Disk space', WARN, f'{free_gb:.0f} GB free on engine drive',
                           f'A full source build wants ~{RECOMMENDED_FREE_GB} GB; free space or use a larger drive.')
    return CheckResult('Disk space', OK, f'{free_gb:.0f} GB free')


def check_engine_registration(engine_path: pathlib.Path) -> CheckResult:
    registered = [pathlib.Path(p).resolve() for p in manifest.get_manifest_engines()]
    if pathlib.Path(engine_path).resolve() in registered:
        return CheckResult('Engine registration', OK, 'this engine is registered')
    return CheckResult('Engine registration', WARN, 'this engine is not registered',
                       'Run "scripts/o3de register --this-engine" so projects can resolve it.')


def check_linux_runtime_libraries() -> list:
    """Linux-only: probe a few shared libraries whose absence commonly breaks the Editor at runtime."""
    if not sys.platform.startswith('linux'):
        return []
    import ctypes.util
    # (human name, library base name passed to find_library, remediation package)
    probes = [
        ('xcb-cursor (Qt/XCB)', 'xcb-cursor', 'libxcb-cursor0'),
        ('EGL (Qt rendering)', 'EGL', 'libegl1 / libegl-mesa0'),
    ]
    results = []
    for human_name, lib_name, package in probes:
        if ctypes.util.find_library(lib_name):
            results.append(CheckResult(f'Runtime lib: {human_name}', OK, 'present'))
        else:
            results.append(CheckResult(f'Runtime lib: {human_name}', WARN, 'not found',
                                       f'Editor may fail to start; install "{package}".'))
    return results


def run_doctor(engine_path: pathlib.Path) -> list:
    checks = [
        check_python(),
        check_cmake(engine_path),
        check_compiler(),
        check_ninja(),
        check_git(),
        check_git_lfs(),
        check_third_party(),
        check_disk_space(engine_path),
        check_engine_registration(engine_path),
    ]
    checks.extend(check_linux_runtime_libraries())
    return checks


def _print_checks(checks: list) -> int:
    name_width = max((len(check.name) for check in checks), default=0)
    worst = OK
    for check in checks:
        if _SEVERITY[check.severity] > _SEVERITY[worst]:
            worst = check.severity
        print(f'  [{check.severity:>4}] {check.name.ljust(name_width)}  {check.detail}')
        if check.hint and check.severity != OK:
            print(f'         -> {check.hint}')
    print('')
    fail_count = sum(1 for c in checks if c.severity == FAIL)
    warn_count = sum(1 for c in checks if c.severity == WARN)
    print(f'Summary: {len(checks)} checks, {fail_count} failed, {warn_count} warnings.')
    # Fail the command only on hard failures; warnings are advisory.
    return 1 if worst == FAIL else 0


def _run_doctor(args) -> int:
    engine_path = pathlib.Path(args.engine_path) if args.engine_path else manifest.get_this_engine_path()
    print(f'Engine: {engine_path}\n')
    print('Build prerequisites:')
    return _print_checks(run_doctor(engine_path))


def _project_engine_summary(project_path: pathlib.Path) -> dict:
    """Collects the engine-separation facts for a single project."""
    project_json = manifest.get_project_json_data(project_path=project_path) or {}
    requested_engine = project_json.get('engine', '')
    resolved_engine = compatibility.get_most_compatible_project_engine_path(
        project_path=project_path, project_json_data=dict(project_json))
    incompatible = set()
    if resolved_engine:
        incompatible = compatibility.get_project_engine_incompatible_objects(project_path, resolved_engine)
    return {
        'name': project_json.get('project_name', pathlib.Path(project_path).name),
        'requested_engine': requested_engine,
        'resolved_engine': resolved_engine,
        'incompatible': incompatible,
    }


def _run_status(args) -> int:
    this_engine = manifest.get_this_engine_path()
    this_engine_json = manifest.get_engine_json_data(engine_path=this_engine) or {}
    print('This engine:')
    print(f'  path    : {this_engine}')
    print(f'  name    : {this_engine_json.get("engine_name", "<unknown>")}')
    print(f'  version : {this_engine_json.get("version", this_engine_json.get("O3DEVersion", "<unknown>"))}')
    print('')

    engines = manifest.get_manifest_engines()
    print(f'Registered engines ({len(engines)}):')
    for engine in engines:
        print(f'  - {engine}')
    print('')

    projects = manifest.get_all_projects()
    print(f'Registered projects ({len(projects)}):')
    if not projects:
        print('  (none registered)')
    for project in projects:
        summary = _project_engine_summary(pathlib.Path(project))
        resolved = summary['resolved_engine'] or '<no compatible engine found>'
        print(f'  - {summary["name"]} ({project})')
        print(f'      wants engine : {summary["requested_engine"] or "<unspecified>"}')
        print(f'      resolves to  : {resolved}')
        if summary['incompatible']:
            print(f'      INCOMPATIBLE : {", ".join(sorted(summary["incompatible"]))}')
    return 0


def _run_resolve(args) -> int:
    project_path = pathlib.Path(args.project_path).resolve()
    if not (project_path / 'project.json').is_file():
        logger.error(f'No project.json found at {project_path}.')
        return 1

    summary = _project_engine_summary(project_path)
    print(f'Project: {summary["name"]} ({project_path})')
    print(f'  declared engine : {summary["requested_engine"] or "<unspecified>"}')

    resolved_engine = summary['resolved_engine']
    if not resolved_engine:
        print('  resolved engine : <none>')
        logger.error('No compatible registered engine was found for this project. '
                     'Register the matching engine with "scripts/o3de register --this-engine".')
        return 1
    print(f'  resolved engine : {resolved_engine}')

    if summary['incompatible']:
        print('  incompatible objects:')
        for item in sorted(summary['incompatible']):
            print(f'    - {item}')
        logger.warning('The project resolved an engine but has incompatible objects (see above).')
        return 1

    print('  status          : compatible')
    return 0


def add_parser_args(parser):
    subparsers = parser.add_subparsers(
        title='hub sub-commands',
        help='Run "o3de hub <sub-command> -h" for details.')

    doctor_parser = subparsers.add_parser(
        'doctor', help='Check that this host has the prerequisites to configure and build the engine.')
    doctor_parser.add_argument('-ep', '--engine-path', type=pathlib.Path, default=None,
                               help='Path to the engine to check (defaults to this engine).')
    doctor_parser.set_defaults(func=_run_doctor)

    status_parser = subparsers.add_parser(
        'status', help='Show registered engines, projects, and how each project maps to an engine.')
    status_parser.set_defaults(func=_run_status)

    resolve_parser = subparsers.add_parser(
        'resolve', help='Find the compatible engine (and report missing dependencies) for a project.')
    resolve_parser.add_argument('-pp', '--project-path', type=pathlib.Path, required=True,
                                help='Path to the project to resolve.')
    resolve_parser.set_defaults(func=_run_resolve)

    # When "o3de hub" is run with no sub-command, print help instead of failing silently.
    def _print_hub_help(_args):
        parser.print_help()
        return 1
    parser.set_defaults(func=_print_hub_help)


def add_args(subparsers) -> None:
    """
    add_args is called to add the 'hub' sub-command to the central o3de.py parser.
    Example: python o3de.py hub doctor
    :param subparsers: the caller instantiates subparsers and passes it in here
    """
    hub_subparser = subparsers.add_parser('hub')
    add_parser_args(hub_subparser)


def main():
    the_parser = argparse.ArgumentParser()
    add_parser_args(the_parser)
    the_args = the_parser.parse_args()
    ret = the_args.func(the_args) if hasattr(the_args, 'func') else 1
    sys.exit(ret)


if __name__ == "__main__":
    main()
