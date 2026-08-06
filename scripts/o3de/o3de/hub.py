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

# NOTE: the o3de manifest/compatibility/utils modules pull in third-party packages (packaging,
# resolvelib) that only exist inside the engine's bundled venv. The build-prerequisite "doctor"
# checks below are deliberately stdlib-only so this module can be imported and run by a *system*
# Python before that venv exists (e.g. from the pre-build GUI hub). Anything that needs the
# manifest is imported lazily inside the function that uses it and degrades gracefully if the
# engine tooling has not been bootstrapped yet.
LOG_FORMAT = '[%(levelname)s] %(name)s: %(message)s'
logger = logging.getLogger('o3de.hub')
logging.basicConfig(format=LOG_FORMAT)

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


def engine_git_revision(engine_path: pathlib.Path) -> str:
    """The engine checkout's exact git revision ('<hash> (<branch>, <date>[, modified])'), because
    engine_version alone cannot distinguish two different checkouts of the same version.

    Uses the git CLI when available and falls back to reading .git/HEAD directly, so it still
    works on hosts without git (e.g. an engine copied as an archive). Returns '' when the
    engine path is not a git checkout at all."""
    git_dir = pathlib.Path(engine_path) / '.git'
    if not git_dir.exists():
        return ''
    git_exe = shutil.which('git')
    if git_exe:
        def _git(*git_args):
            try:
                result = subprocess.run([git_exe, '-C', str(engine_path), *git_args],
                                        capture_output=True, text=True, timeout=15)
                return result.stdout.strip() if result.returncode == 0 else ''
            except (OSError, subprocess.SubprocessError):
                return ''
        commit = _git('rev-parse', '--short=12', 'HEAD')
        if commit:
            branch = _git('rev-parse', '--abbrev-ref', 'HEAD')
            date = _git('log', '-1', '--format=%cd', '--date=short')
            dirty = _git('status', '--porcelain', '--untracked-files=no')
            parts = [p for p in (branch if branch != 'HEAD' else 'detached', date) if p]
            if dirty:
                parts.append('modified')
            return f'{commit} ({", ".join(parts)})' if parts else commit
    # No usable git CLI: resolve HEAD by hand.
    try:
        head = (git_dir / 'HEAD').read_text(encoding='utf-8').strip()
        if head.startswith('ref: '):
            ref = head[len('ref: '):]
            ref_file = git_dir / ref
            if ref_file.exists():
                commit = ref_file.read_text(encoding='utf-8').strip()
            else:
                commit = ''
                packed = git_dir / 'packed-refs'
                if packed.exists():
                    for line in packed.read_text(encoding='utf-8').splitlines():
                        if line.endswith(' ' + ref):
                            commit = line.split(' ', 1)[0]
                            break
            branch = ref.rsplit('/', 1)[-1]
            return f'{commit[:12]} ({branch})' if commit else ''
        return head[:12]
    except OSError:
        return ''


class CheckResult:
    """A single doctor check outcome plus an optional remediation hint."""
    def __init__(self, name: str, severity: str, detail: str, hint: str = ''):
        self.name = name
        self.severity = severity
        self.detail = detail
        self.hint = hint


def _try_import_manifest():
    """Imports o3de.manifest lazily, returning None if the engine venv (its deps) is not ready yet.

    The manifest module pulls in third-party packages that only exist inside the bundled venv, so
    importing it can fail when the doctor runs under a system Python before bootstrap. Callers treat
    a None result as "engine tooling not bootstrapped yet" rather than crashing.
    """
    try:
        from o3de import manifest
        return manifest
    except ImportError:
        return None


def engine_path_fallback() -> pathlib.Path:
    """Best-effort engine root when the manifest module is unavailable: this file lives at
    <engine>/scripts/o3de/o3de/hub.py, so the engine root is four parents up."""
    return pathlib.Path(__file__).resolve().parents[3]


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
    manifest = _try_import_manifest()
    if manifest is None:
        return pathlib.Path.home() / '.o3de' / '3rdParty'
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


def _manifest_engines_fallback() -> list or None:
    """Reads registered engines straight out of ~/.o3de/o3de_manifest.json with the stdlib, for
    when the o3de manifest module (and its venv-only dependencies) can't be imported.
    Returns None when the manifest file does not exist or can't be parsed."""
    import json
    manifest_file = pathlib.Path(os.environ.get('USERPROFILE') or pathlib.Path.home()) / '.o3de' / 'o3de_manifest.json'
    try:
        data = json.loads(manifest_file.read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None
    engines = data.get('engines', [])
    return engines if isinstance(engines, list) else None


def check_engine_registration(engine_path: pathlib.Path) -> CheckResult:
    manifest = _try_import_manifest()
    if manifest is None:
        engines = _manifest_engines_fallback()
        if engines is None:
            return CheckResult('Engine registration', WARN, 'engine tooling not bootstrapped yet',
                               'Run any "scripts/o3de" command first to set up the bundled Python, then re-check.')
        registered = [pathlib.Path(p).resolve() for p in engines]
        if pathlib.Path(engine_path).resolve() in registered:
            return CheckResult('Engine registration', OK, 'this engine is registered')
        return CheckResult('Engine registration', WARN, 'this engine is not registered',
                           'Run "scripts/o3de register --this-engine" so projects can resolve it.')
    registered = [pathlib.Path(p).resolve() for p in manifest.get_manifest_engines()]
    if pathlib.Path(engine_path).resolve() in registered:
        return CheckResult('Engine registration', OK, 'this engine is registered')
    return CheckResult('Engine registration', WARN, 'this engine is not registered',
                       'Run "scripts/o3de register --this-engine" so projects can resolve it.')


# pkg-config modules the engine's CMake configure requires (cmake/Platform/Linux + Qt tooling),
# with the human-readable source of each requirement.
_LINUX_BUILD_PC_MODULES = ['libunwind', 'libzstd', 'fontconfig', 'xkbcommon', 'egl', 'glesv2', 'glu']


def check_linux_build_dependencies() -> CheckResult or None:
    """Linux-only: the engine's CMake configure hard-requires pkg-config and a set of development
    libraries; missing ones fail configure with errors like 'Could NOT find PkgConfig'."""
    if not sys.platform.startswith('linux'):
        return None
    if not shutil.which('pkg-config'):
        return CheckResult('Build libraries (Linux)', FAIL, 'pkg-config not found',
                           'Required by the engine CMake configure; install "pkg-config" plus the '
                           'development libraries listed in docs/aio3de/BUILDING_LINUX.md.')
    missing = []
    for module in _LINUX_BUILD_PC_MODULES:
        completed = subprocess.run(['pkg-config', '--exists', module], capture_output=True)
        if completed.returncode != 0:
            missing.append(module)
    if missing:
        return CheckResult('Build libraries (Linux)', FAIL,
                           f'missing development libraries: {", ".join(missing)}',
                           'The engine CMake configure needs these; install the package list in '
                           'docs/aio3de/BUILDING_LINUX.md.')
    return CheckResult('Build libraries (Linux)', OK, 'pkg-config and development libraries present')


def check_linux_runtime_libraries() -> list:
    """Linux-only: probe a few shared libraries whose absence commonly breaks the Editor at runtime."""
    if not sys.platform.startswith('linux'):
        return []
    import ctypes
    import ctypes.util
    # (human name, find_library base name, soname to try loading, remediation package)
    probes = [
        ('xcb-cursor (Qt/XCB)', 'xcb-cursor', 'libxcb-cursor.so.0', 'libxcb-cursor0'),
        ('EGL (Qt rendering)', 'EGL', 'libEGL.so.1', 'libegl1 / libegl-mesa0'),
    ]

    def _library_present(lib_name, soname):
        # find_library shells out to ldconfig/gcc and can miss libraries the dynamic loader
        # finds fine, so fall back to actually dlopen-ing the soname.
        if ctypes.util.find_library(lib_name):
            return True
        try:
            ctypes.CDLL(soname)
            return True
        except OSError:
            return False

    results = []
    for human_name, lib_name, soname, package in probes:
        if _library_present(lib_name, soname):
            results.append(CheckResult(f'Runtime lib: {human_name}', OK, 'present'))
        else:
            results.append(CheckResult(f'Runtime lib: {human_name}', WARN, 'not found',
                                       f'Editor may fail to start; install "{package}".'))
    return results


VC_REDIST_URL = 'https://aka.ms/vs/17/release/vc_redist.x64.exe'


def check_windows_vcredist() -> CheckResult or None:
    """Windows-only: the engine's bundled (embeddable) Python needs the MSVC runtime (vcruntime140.dll).
    When it is missing, the bundled python.exe crashes on launch and venv creation fails cryptically."""
    if not sys.platform.startswith('win'):
        return None
    import ctypes
    for dll in ('vcruntime140.dll', 'vcruntime140_1.dll'):
        handle = ctypes.windll.kernel32.LoadLibraryW(dll)
        if handle:
            return CheckResult('VC++ redistributable', OK, f'{dll} present')
    return CheckResult('VC++ redistributable', FAIL, 'vcruntime140.dll not found',
                       f'The bundled Python needs the MSVC runtime. Install the x64 redistributable: {VC_REDIST_URL}')


def check_windows_code_integrity() -> CheckResult or None:
    """Windows-only: detect an enforced user-mode code-integrity policy (Smart App Control / WDAC /
    Device Guard). When enforced it blocks the *unsigned* bundled python.exe (and the engine binaries
    you build), surfacing as 'blocked by your organization's Device Guard policy' / a silent crash."""
    if not sys.platform.startswith('win'):
        return None
    # User-mode Code Integrity (UMCI) enforcement: 2 == enforced.
    enforced = False
    try:
        import subprocess as _sp
        completed = _sp.run(
            ['powershell', '-NoProfile', '-Command',
             '(Get-CimInstance -Namespace root/Microsoft/Windows/DeviceGuard '
             '-ClassName Win32_DeviceGuard).UsermodeCodeIntegrityPolicyEnforcementStatus'],
            capture_output=True, text=True, timeout=20)
        value = (completed.stdout or '').strip()
        enforced = value == '2'
    except (OSError, subprocess.SubprocessError, ValueError):
        enforced = False
    if enforced:
        return CheckResult(
            'Code integrity policy', FAIL,
            'user-mode code integrity is ENFORCED (Smart App Control / WDAC / Device Guard)',
            'This blocks the unsigned bundled Python and the engine binaries you build. Turn off '
            '"Smart App Control" (Windows Security > App & browser control > Smart App Control settings). '
            'Note this switch is one-way and needs a Windows reinstall to re-enable.')
    return CheckResult('Code integrity policy', OK, 'not enforced (unsigned binaries allowed)')


def get_engine_venv_python(engine_path: pathlib.Path) -> pathlib.Path or None:
    """Locates the per-engine Python venv interpreter (~/.o3de/Python/venv/<engine id>), or None
    when it can't be determined (no cmake to compute the engine id) or doesn't exist yet."""
    if not shutil.which('cmake'):
        return None
    calc = pathlib.Path(engine_path) / 'cmake' / 'CalculateEnginePathId.cmake'
    if not calc.is_file():
        return None
    # python/python.sh|cmd pass "<engine>/python/.." to this script; the ID is a hash of the
    # normalized path *string* (which keeps a trailing slash for ".."), so the exact same
    # argument must be used here to land on the same venv folder.
    hash_argument = str(pathlib.Path(engine_path).resolve() / 'python' / '..')
    try:
        completed = subprocess.run(['cmake', '-P', str(calc), hash_argument],
                                   capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    engine_id = ((completed.stdout or '') + (completed.stderr or '')).strip()
    if completed.returncode != 0 or not engine_id:
        return None
    home = pathlib.Path(os.environ.get('USERPROFILE') or pathlib.Path.home())
    venv = home / '.o3de' / 'Python' / 'venv' / engine_id
    python = venv / ('Scripts/python.exe' if sys.platform.startswith('win') else 'bin/python')
    return python if python.is_file() else None


def check_engine_python_venv(engine_path: pathlib.Path) -> CheckResult:
    """Detects a half-created engine venv: it exists (so the launcher considers Python 'ready')
    but the dependency install failed/was interrupted, which then surfaces later as
    "ModuleNotFoundError: No module named 'packaging'" from every o3de command."""
    venv_python = get_engine_venv_python(engine_path)
    if venv_python is None:
        return CheckResult('Engine Python venv', WARN, 'not set up yet',
                           'Created automatically by the first "scripts/o3de" command '
                           '(or run python/get_python.sh / get_python.bat).')
    try:
        completed = subprocess.run([str(venv_python), '-c', 'import packaging, resolvelib, o3de'],
                                   capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return CheckResult('Engine Python venv', FAIL, f'{venv_python} could not be run',
                           'Re-run python/get_python.sh (Linux/macOS) or python\\get_python.bat (Windows).')
    if completed.returncode != 0:
        return CheckResult('Engine Python venv', FAIL, 'exists but is missing its dependencies',
                           'A previous setup was interrupted. Re-run python/get_python.sh '
                           '(Linux/macOS) or python\\get_python.bat (Windows) to repair it.')
    return CheckResult('Engine Python venv', OK, 'dependencies import correctly')


def check_engine_path_sanity(engine_path: pathlib.Path) -> CheckResult:
    """Paths with spaces or non-ASCII characters break assorted build scripts and third-party
    tooling; warn early instead of letting the failure surface as a cryptic build error."""
    path_text = str(engine_path)
    problems = []
    if ' ' in path_text:
        problems.append('contains spaces')
    if not path_text.isascii():
        problems.append('contains non-ASCII characters')
    if problems:
        return CheckResult('Engine path', WARN, f'{path_text} ({", ".join(problems)})',
                           'Some build tools mishandle such paths; if you hit odd build/setup errors, '
                           'move the engine to a plain path like ~/o3de or C:\\o3de.')
    return CheckResult('Engine path', OK, path_text)


def run_doctor(engine_path: pathlib.Path) -> list:
    checks = [
        check_python(),
        check_engine_path_sanity(engine_path),
        check_cmake(engine_path),
        check_compiler(),
        check_ninja(),
        check_git(),
        check_git_lfs(),
        check_third_party(),
        check_disk_space(engine_path),
        check_engine_python_venv(engine_path),
        check_engine_registration(engine_path),
    ]
    build_deps_check = check_linux_build_dependencies()
    if build_deps_check is not None:
        checks.append(build_deps_check)
    checks.extend(check_linux_runtime_libraries())
    for windows_check in (check_windows_vcredist(), check_windows_code_integrity()):
        if windows_check is not None:
            checks.append(windows_check)
    return checks


# ---- automatic remediation ("install missing") --------------------------------------------------
#
# Maps each failing/warning doctor check onto concrete install actions so both the CLI
# ("o3de hub install") and the GUI hub's "Install missing" button can fix the machine in one go.
# Two kinds of action exist:
#   * system packages, installed in a single elevated package-manager invocation, and
#   * per-user steps (git lfs install, creating the 3rdParty dir, bootstrapping the engine venv,
#     registering the engine) that must NOT run as root.

_LINUX_PACKAGE_NAMES = {
    # check name -> {package manager: [packages]}
    'CMake': {'apt-get': ['cmake'], 'dnf': ['cmake'], 'pacman': ['cmake']},
    'Ninja': {'apt-get': ['ninja-build'], 'dnf': ['ninja-build'], 'pacman': ['ninja']},
    'Git': {'apt-get': ['git'], 'dnf': ['git'], 'pacman': ['git']},
    'Git LFS': {'apt-get': ['git-lfs'], 'dnf': ['git-lfs'], 'pacman': ['git-lfs']},
    'C++ compiler': {'apt-get': ['clang', 'lld'], 'dnf': ['clang', 'lld'], 'pacman': ['clang', 'lld']},
    'Runtime lib: xcb-cursor (Qt/XCB)': {'apt-get': ['libxcb-cursor0'], 'dnf': ['xcb-util-cursor'],
                                         'pacman': ['xcb-util-cursor']},
    'Runtime lib: EGL (Qt rendering)': {'apt-get': ['libegl1'], 'dnf': ['mesa-libEGL'],
                                        'pacman': ['libglvnd']},
    # Everything the engine CMake configure needs (the verified list from BUILDING_LINUX.md).
    'Build libraries (Linux)': {
        'apt-get': ['pkg-config', 'binutils', 'libglu1-mesa-dev', 'libxcb-xinerama0',
                    'libfontconfig1-dev', 'libxcb-xkb-dev', 'libxcb-randr0-dev',
                    'libxkbcommon-x11-dev', 'libxkbcommon-dev', 'libxcb-xfixes0-dev',
                    'libxcb-xinput-dev', 'libxcb-xinput0', 'libxcb-icccm4-dev',
                    'libxcb-image0-dev', 'libxcb-keysyms1-dev', 'libxcb-render-util0-dev',
                    'libpcre2-16-0', 'libunwind-dev', 'libzstd-dev', 'mesa-common-dev',
                    'libvulkan1', 'libegl1-mesa-dev', 'libgles2-mesa-dev'],
        'dnf': ['pkgconf-pkg-config', 'binutils', 'mesa-libGLU-devel', 'fontconfig-devel',
                'libxkbcommon-x11-devel', 'libxkbcommon-devel', 'xcb-util-keysyms-devel',
                'xcb-util-image-devel', 'xcb-util-wm-devel', 'xcb-util-renderutil-devel',
                'pcre2-utf16', 'libunwind-devel', 'libzstd-devel', 'mesa-libGL-devel',
                'vulkan-loader', 'mesa-libEGL-devel', 'mesa-libGLES-devel'],
        'pacman': ['pkgconf', 'binutils', 'glu', 'fontconfig', 'libxkbcommon-x11',
                   'libxkbcommon', 'xcb-util-keysyms', 'xcb-util-image', 'xcb-util-wm',
                   'xcb-util-renderutil', 'pcre2', 'libunwind', 'zstd', 'mesa', 'vulkan-icd-loader'],
    },
}

_WINGET_IDS = {
    'CMake': ['Kitware.CMake'],
    'Ninja': ['Ninja-build.Ninja'],
    'Git': ['Git.Git'],
    'Git LFS': ['GitHub.GitLFS'],
    'VC++ redistributable': ['Microsoft.VCRedist.2015+.x64'],
}


def detect_linux_package_manager() -> str or None:
    for manager in ('apt-get', 'dnf', 'pacman'):
        if shutil.which(manager):
            return manager
    return None


def _package_install_command(manager: str, packages: list) -> list:
    if manager == 'apt-get':
        return ['apt-get', 'install', '-y'] + packages
    if manager == 'dnf':
        return ['dnf', 'install', '-y'] + packages
    if manager == 'pacman':
        return ['pacman', '-S', '--noconfirm'] + packages
    raise ValueError(f'unsupported package manager: {manager}')


class FixStep:
    """One command to run to remediate the machine. 'elevated' steps need root/admin."""
    def __init__(self, description: str, command: list, elevated: bool = False, cwd: str = None):
        self.description = description
        self.command = command
        self.elevated = elevated
        self.cwd = cwd


def build_fix_plan(checks: list, engine_path: pathlib.Path) -> list:
    """Turns the failing/warning doctor checks into an ordered list of FixSteps.
    Returns an empty list when there is nothing to fix (everything actionable is OK)."""
    engine_path = pathlib.Path(engine_path)
    by_name = {check.name: check for check in checks}

    def needs_fix(name):
        check = by_name.get(name)
        return check is not None and check.severity != OK

    steps = []
    if sys.platform.startswith('win'):
        winget_ids = []
        for name, ids in _WINGET_IDS.items():
            if needs_fix(name):
                winget_ids.extend(ids)
        for winget_id in winget_ids:
            # winget triggers its own UAC elevation prompt per package; no wrapper needed.
            steps.append(FixStep(f'Install {winget_id} (winget)',
                                 ['winget', 'install', '--id', winget_id, '-e',
                                  '--accept-source-agreements', '--accept-package-agreements'],
                                 elevated=False))
    else:
        manager = detect_linux_package_manager()
        packages = []
        for name, per_manager in _LINUX_PACKAGE_NAMES.items():
            if needs_fix(name) and manager in per_manager:
                packages.extend(per_manager[manager])
        if packages and manager:
            steps.append(FixStep(f'Install system packages: {" ".join(packages)}',
                                 _package_install_command(manager, packages), elevated=True))

    if needs_fix('Git LFS'):
        steps.append(FixStep('Enable Git LFS for this user', ['git', 'lfs', 'install']))
        steps.append(FixStep('Fetch LFS content for this checkout', ['git', 'lfs', 'pull'],
                             cwd=str(engine_path)))

    if needs_fix('3rd party packages'):
        third_party = get_third_party_path()
        if third_party:
            steps.append(FixStep(f'Create 3rd party package folder {third_party}',
                                 [sys.executable, '-c',
                                  f'import pathlib; pathlib.Path({str(third_party)!r}).mkdir(parents=True, exist_ok=True)']))

    if sys.platform.startswith('win'):
        launcher = engine_path / 'scripts' / 'o3de.bat'
        get_python = engine_path / 'python' / 'get_python.bat'
    else:
        launcher = engine_path / 'scripts' / 'o3de.sh'
        get_python = engine_path / 'python' / 'get_python.sh'

    if needs_fix('Engine Python venv'):
        steps.append(FixStep('Set up / repair the engine Python environment (can take several minutes)',
                             [str(get_python)], cwd=str(engine_path)))

    if needs_fix('Engine registration'):
        steps.append(FixStep('Register this engine',
                             [str(launcher), 'register', '--this-engine'], cwd=str(engine_path)))

    return steps


def elevation_wrapper(gui: bool) -> list or None:
    """How to run an elevated command on this host: a command prefix, [] when already root,
    or None when no usable elevation mechanism exists."""
    if sys.platform.startswith('win'):
        return []  # winget handles its own UAC elevation.
    if hasattr(os, 'geteuid') and os.geteuid() == 0:
        return []
    if gui and os.environ.get('DISPLAY') and shutil.which('pkexec'):
        return ['pkexec']
    if shutil.which('sudo'):
        return ['sudo']
    if shutil.which('pkexec'):
        return ['pkexec']
    return None


def run_fix_plan(steps: list, gui: bool = False, log=print) -> int:
    """Executes the FixSteps in order, streaming output through 'log'. Stops at the first
    hard failure and returns its exit code; 0 when everything succeeded."""
    wrapper = elevation_wrapper(gui)
    for step in steps:
        command = list(step.command)
        if step.elevated:
            if wrapper is None:
                log(f'SKIP (needs root, no sudo/pkexec found): {step.description}')
                log(f'  run manually: sudo {" ".join(command)}')
                continue
            command = wrapper + command
        log(f'==> {step.description}')
        log(f'    $ {" ".join(command)}')
        try:
            process = subprocess.Popen(command, cwd=step.cwd, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True)
        except OSError as exc:
            log(f'    ERROR: could not start: {exc}')
            return 1
        for line in process.stdout:
            log('    ' + line.rstrip('\n'))
        process.wait()
        if process.returncode != 0:
            log(f'    FAILED (exit code {process.returncode})')
            return process.returncode
    return 0


def _run_install(args) -> int:
    engine_path = _resolve_engine_path(args.engine_path)
    print(f'Engine: {engine_path}')
    checks = run_doctor(engine_path)
    steps = build_fix_plan(checks, engine_path)
    if not steps:
        print('Nothing to install - all actionable checks are OK.')
        return _print_checks(checks)
    print(f'Planned fixes ({len(steps)}):')
    for step in steps:
        print(f'  - {step.description}')
    if args.dry_run:
        return 0
    result = run_fix_plan(steps)
    print('')
    print('Re-running checks:')
    return _print_checks(run_doctor(engine_path)) or result


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


def _resolve_engine_path(explicit_path) -> pathlib.Path:
    if explicit_path:
        return pathlib.Path(explicit_path)
    manifest = _try_import_manifest()
    if manifest is not None:
        resolved = manifest.get_this_engine_path()
        if resolved:
            return pathlib.Path(resolved)
    return engine_path_fallback()


def _run_doctor(args) -> int:
    engine_path = _resolve_engine_path(args.engine_path)
    print(f'Engine: {engine_path}')
    revision = engine_git_revision(engine_path)
    if revision:
        print(f'Commit: {revision}')
    print('')
    print('Build prerequisites:')
    return _print_checks(run_doctor(engine_path))


def _project_engine_summary(project_path: pathlib.Path) -> dict:
    """Collects the engine-separation facts for a single project."""
    from o3de import manifest, compatibility
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
    from o3de import manifest
    this_engine = manifest.get_this_engine_path()
    this_engine_json = manifest.get_engine_json_data(engine_path=this_engine) or {}
    print('This engine:')
    print(f'  path    : {this_engine}')
    print(f'  name    : {this_engine_json.get("engine_name", "<unknown>")}')
    print(f'  version : {this_engine_json.get("version", this_engine_json.get("O3DEVersion", "<unknown>"))}')
    revision = engine_git_revision(pathlib.Path(this_engine)) if this_engine else ''
    print(f'  commit  : {revision or "<not a git checkout>"}')
    print('')

    engines = manifest.get_manifest_engines()
    print(f'Registered engines ({len(engines)}):')
    for engine in engines:
        engine_rev = engine_git_revision(pathlib.Path(engine))
        print(f'  - {engine}' + (f'  [{engine_rev}]' if engine_rev else ''))
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

    install_parser = subparsers.add_parser(
        'install', help='Install everything the doctor reports as missing (packages, Git LFS, '
                        '3rdParty folder, engine Python bootstrap, engine registration).')
    install_parser.add_argument('-ep', '--engine-path', type=pathlib.Path, default=None,
                                help='Path to the engine to fix (defaults to this engine).')
    install_parser.add_argument('--dry-run', action='store_true',
                                help='Only print what would be installed/run.')
    install_parser.set_defaults(func=_run_install)

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
