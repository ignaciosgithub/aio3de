#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

import pathlib
import sys
from unittest.mock import patch

import pytest

from o3de import hub


@pytest.mark.parametrize("contents, expected", [
    ("cmake_minimum_required(VERSION 3.24)", "3.24"),
    ("cmake_minimum_required( VERSION 3.27.9 )", "3.27.9"),
    ("CMAKE_MINIMUM_REQUIRED(VERSION 3.30 FATAL_ERROR)", "3.30"),
    ("no version here", hub.DEFAULT_MIN_CMAKE_VERSION),
])
def test_get_required_cmake_version_parses_floor(tmp_path, contents, expected):
    (tmp_path / 'CMakeLists.txt').write_text(contents, encoding='utf-8')
    assert hub.get_required_cmake_version(tmp_path) == expected


def test_get_required_cmake_version_missing_file_uses_default(tmp_path):
    assert hub.get_required_cmake_version(tmp_path / 'does_not_exist') == hub.DEFAULT_MIN_CMAKE_VERSION


@pytest.mark.parametrize("text, expected", [
    ("cmake version 3.27.9", "3.27.9"),
    ("git version 2.34.1", "2.34.1"),
    ("clang version 14.0.0 something", "14.0.0"),
    ("no digits", None),
    ("", None),
])
def test_parse_first_version(text, expected):
    assert hub._parse_first_version(text) == expected


@pytest.mark.parametrize("found, required, expected_severity", [
    ("3.27.9", "3.24", hub.OK),
    ("3.24", "3.24", hub.OK),
    ("3.22.1", "3.24", hub.FAIL),
])
def test_check_cmake_version_comparison(tmp_path, found, required, expected_severity):
    (tmp_path / 'CMakeLists.txt').write_text(f"cmake_minimum_required(VERSION {required})", encoding='utf-8')
    with patch.object(hub, '_run_version_command', return_value=f"cmake version {found}"):
        result = hub.check_cmake(tmp_path)
    assert result.severity == expected_severity


def test_check_cmake_missing_is_fail(tmp_path):
    (tmp_path / 'CMakeLists.txt').write_text("cmake_minimum_required(VERSION 3.24)", encoding='utf-8')
    with patch.object(hub, '_run_version_command', return_value=None):
        result = hub.check_cmake(tmp_path)
    assert result.severity == hub.FAIL


def test_check_git_lfs_via_subcommand_when_no_standalone_binary():
    def fake_which(name):
        return None if name == 'git-lfs' else '/usr/bin/git'

    def fake_run(executable, args):
        if args[:1] == ['lfs']:
            return 'git-lfs/3.0.2 (GitHub; linux amd64)'
        return None

    with patch.object(hub.shutil, 'which', side_effect=fake_which), \
         patch.object(hub, '_run_version_command', side_effect=fake_run):
        result = hub.check_git_lfs()
    assert result.severity == hub.OK


def test_check_git_lfs_missing_is_fail():
    with patch.object(hub.shutil, 'which', return_value=None), \
         patch.object(hub, '_run_version_command', return_value=None):
        result = hub.check_git_lfs()
    assert result.severity == hub.FAIL


def test_check_third_party_prefers_env_path(tmp_path, monkeypatch):
    monkeypatch.setenv('LY_3RDPARTY_PATH', str(tmp_path))
    result = hub.check_third_party()
    assert result.severity == hub.OK
    assert str(tmp_path) in result.detail
    assert 'LY_3RDPARTY_PATH' in result.detail


def test_check_third_party_missing_dir_warns(monkeypatch):
    monkeypatch.setenv('LY_3RDPARTY_PATH', '/path/that/does/not/exist/o3de-packages')
    result = hub.check_third_party()
    assert result.severity == hub.WARN


def test_check_engine_registration(tmp_path):
    engine_path = tmp_path / 'engine'
    engine_path.mkdir()

    class FakeManifest:
        def __init__(self, engines):
            self._engines = engines

        def get_manifest_engines(self):
            return self._engines

    with patch.object(hub, '_try_import_manifest', return_value=FakeManifest([str(engine_path)])):
        assert hub.check_engine_registration(engine_path).severity == hub.OK
    with patch.object(hub, '_try_import_manifest', return_value=FakeManifest([])):
        assert hub.check_engine_registration(engine_path).severity == hub.WARN


def test_check_engine_registration_warns_when_manifest_unavailable(tmp_path):
    # Pre-bootstrap: neither the manifest module nor the manifest json file are available yet.
    with patch.object(hub, '_try_import_manifest', return_value=None), \
         patch.object(hub, '_manifest_engines_fallback', return_value=None):
        result = hub.check_engine_registration(tmp_path)
    assert result.severity == hub.WARN
    assert 'bootstrapped' in result.detail


def test_check_engine_registration_uses_manifest_json_fallback(tmp_path):
    # Pre-bootstrap but the manifest json exists: registration is read straight from it.
    with patch.object(hub, '_try_import_manifest', return_value=None), \
         patch.object(hub, '_manifest_engines_fallback', return_value=[str(tmp_path)]):
        assert hub.check_engine_registration(tmp_path).severity == hub.OK
    with patch.object(hub, '_try_import_manifest', return_value=None), \
         patch.object(hub, '_manifest_engines_fallback', return_value=[]):
        assert hub.check_engine_registration(tmp_path).severity == hub.WARN


def test_get_third_party_path_fallback_without_manifest(monkeypatch):
    monkeypatch.delenv('LY_3RDPARTY_PATH', raising=False)
    with patch.object(hub, '_try_import_manifest', return_value=None):
        path = hub.get_third_party_path()
    assert path.name == '3rdParty'


def test_engine_path_fallback_points_at_repo_root():
    # hub.py lives at <engine>/scripts/o3de/o3de/hub.py
    fallback = hub.engine_path_fallback()
    assert (fallback / 'scripts' / 'o3de' / 'o3de' / 'hub.py').is_file()


def test_windows_checks_are_none_off_windows():
    with patch.object(hub.sys, 'platform', 'linux'):
        assert hub.check_windows_vcredist() is None
        assert hub.check_windows_code_integrity() is None


def test_check_windows_code_integrity_enforced_is_fail():
    completed = type('C', (), {'stdout': '2\n', 'stderr': ''})()
    with patch.object(hub.sys, 'platform', 'win32'), \
         patch('subprocess.run', return_value=completed):
        result = hub.check_windows_code_integrity()
    assert result.severity == hub.FAIL
    assert 'Smart App Control' in result.hint


def test_check_windows_code_integrity_not_enforced_is_ok():
    completed = type('C', (), {'stdout': '0\n', 'stderr': ''})()
    with patch.object(hub.sys, 'platform', 'win32'), \
         patch('subprocess.run', return_value=completed):
        result = hub.check_windows_code_integrity()
    assert result.severity == hub.OK


def test_print_checks_returns_nonzero_only_on_failure(capsys):
    ok_only = [hub.CheckResult('a', hub.OK, 'fine'), hub.CheckResult('b', hub.WARN, 'meh', 'hint')]
    assert hub._print_checks(ok_only) == 0
    with_fail = ok_only + [hub.CheckResult('c', hub.FAIL, 'broken', 'fix it')]
    assert hub._print_checks(with_fail) == 1


def test_check_linux_runtime_libraries_empty_off_linux():
    with patch.object(hub.sys, 'platform', 'win32'):
        assert hub.check_linux_runtime_libraries() == []


def test_run_resolve_missing_project_json(tmp_path):
    class Args:
        project_path = tmp_path
    assert hub._run_resolve(Args()) == 1


def test_run_resolve_no_compatible_engine(tmp_path):
    (tmp_path / 'project.json').write_text('{"project_name": "P"}', encoding='utf-8')

    class Args:
        project_path = tmp_path

    with patch.object(hub, '_project_engine_summary', return_value={
            'name': 'P', 'requested_engine': 'o3de', 'resolved_engine': None, 'incompatible': set()}):
        assert hub._run_resolve(Args()) == 1


def test_run_resolve_compatible(tmp_path):
    (tmp_path / 'project.json').write_text('{"project_name": "P"}', encoding='utf-8')

    class Args:
        project_path = tmp_path

    with patch.object(hub, '_project_engine_summary', return_value={
            'name': 'P', 'requested_engine': 'o3de',
            'resolved_engine': pathlib.Path('/engine'), 'incompatible': set()}):
        assert hub._run_resolve(Args()) == 0


def test_run_resolve_incompatible_objects(tmp_path):
    (tmp_path / 'project.json').write_text('{"project_name": "P"}', encoding='utf-8')

    class Args:
        project_path = tmp_path

    with patch.object(hub, '_project_engine_summary', return_value={
            'name': 'P', 'requested_engine': 'o3de',
            'resolved_engine': pathlib.Path('/engine'), 'incompatible': {'GemX'}}):
        assert hub._run_resolve(Args()) == 1


# ---- engine path sanity ----------------------------------------------------------------------

@pytest.mark.parametrize("path_text, expected_severity", [
    ("/home/user/o3de", hub.OK),
    ("/home/user/my engine/o3de", hub.WARN),
    ("/home/user/Área de trabalho/o3de", hub.WARN),
])
def test_check_engine_path_sanity(path_text, expected_severity):
    result = hub.check_engine_path_sanity(pathlib.Path(path_text))
    assert result.severity == expected_severity


# ---- fix plan ("install missing") ------------------------------------------------------------

def _check(name, severity, detail=''):
    return hub.CheckResult(name, severity, detail)


def test_build_fix_plan_empty_when_all_ok():
    checks = [_check('CMake', hub.OK), _check('Git LFS', hub.OK)]
    assert hub.build_fix_plan(checks, pathlib.Path('/engine')) == []


def test_build_fix_plan_linux_packages_and_user_steps():
    checks = [
        _check('CMake', hub.FAIL),
        _check('Ninja', hub.WARN),
        _check('Git LFS', hub.FAIL),
        _check('3rd party packages', hub.WARN),
        _check('Engine Python venv', hub.FAIL, 'exists but is missing its dependencies'),
        _check('Engine registration', hub.WARN, 'this engine is not registered'),
        _check('Runtime lib: xcb-cursor (Qt/XCB)', hub.WARN),
    ]
    with patch.object(sys, 'platform', 'linux'), \
         patch.object(hub, 'detect_linux_package_manager', return_value='apt-get'), \
         patch.object(hub, 'get_third_party_path', return_value=pathlib.Path('/tp')):
        steps = hub.build_fix_plan(checks, pathlib.Path('/engine'))
    descriptions = [s.description for s in steps]
    package_step = steps[0]
    assert package_step.elevated
    assert package_step.command[:3] == ['apt-get', 'install', '-y']
    for package in ('cmake', 'ninja-build', 'git-lfs', 'libxcb-cursor0'):
        assert package in package_step.command
    assert any('Git LFS' in d for d in descriptions)
    assert any('3rd party' in d for d in descriptions)
    assert any('repair the engine Python' in d for d in descriptions)
    assert any('Register' in d for d in descriptions)
    # get_python repair must run before registration
    repair_index = next(i for i, d in enumerate(descriptions) if 'repair the engine Python' in d)
    register_index = next(i for i, d in enumerate(descriptions) if 'Register' in d)
    assert repair_index < register_index
    # non-package steps never run elevated
    assert all(not s.elevated for s in steps[1:])


def test_build_fix_plan_windows_uses_winget():
    checks = [_check('CMake', hub.FAIL), _check('Git LFS', hub.FAIL)]
    with patch.object(sys, 'platform', 'win32'):
        steps = hub.build_fix_plan(checks, pathlib.Path('C:/engine'))
    winget_steps = [s for s in steps if s.command and s.command[0] == 'winget']
    installed_ids = [s.command[s.command.index('--id') + 1] for s in winget_steps]
    assert 'Kitware.CMake' in installed_ids
    assert 'GitHub.GitLFS' in installed_ids


def test_run_fix_plan_skips_elevated_without_wrapper():
    step = hub.FixStep('needs root', ['apt-get', 'install', '-y', 'cmake'], elevated=True)
    lines = []
    with patch.object(hub, 'elevation_wrapper', return_value=None):
        assert hub.run_fix_plan([step], log=lines.append) == 0
    assert any('SKIP' in line for line in lines)


def test_run_fix_plan_runs_commands():
    step = hub.FixStep('say hi', [sys.executable, '-c', 'print("hi")'])
    lines = []
    assert hub.run_fix_plan([step], log=lines.append) == 0
    assert any('hi' in line for line in lines)


def test_run_fix_plan_stops_on_failure():
    steps = [
        hub.FixStep('fail', [sys.executable, '-c', 'import sys; sys.exit(3)']),
        hub.FixStep('never runs', [sys.executable, '-c', 'print("nope")']),
    ]
    lines = []
    assert hub.run_fix_plan(steps, log=lines.append) == 3
    assert not any('nope' in line for line in lines)


def test_o3de_sh_handles_paths_with_spaces(tmp_path):
    """Regression test: scripts/o3de.sh must work when the engine lives in a path containing
    spaces / non-ASCII characters (it used to break with an unquoted expansion)."""
    if sys.platform.startswith('win'):
        pytest.skip('bash launcher test')
    engine = tmp_path / 'Área de trabalho' / 'engine'
    (engine / 'scripts').mkdir(parents=True)
    (engine / 'python').mkdir()
    repo_root = pathlib.Path(hub.__file__).resolve().parents[3]
    script = (repo_root / 'scripts' / 'o3de.sh').read_text(encoding='utf-8')
    (engine / 'scripts' / 'o3de.sh').write_text(script, encoding='utf-8')
    (engine / 'scripts' / 'o3de.py').write_text(
        'import sys; print("ARGS:" + "|".join(sys.argv[1:]))', encoding='utf-8')
    python_stub = engine / 'python' / 'python.sh'
    python_stub.write_text('#!/bin/bash\nexec python3 "$@"\n', encoding='utf-8')
    python_stub.chmod(0o755)
    (engine / 'scripts' / 'o3de.sh').chmod(0o755)
    import subprocess
    completed = subprocess.run(['bash', str(engine / 'scripts' / 'o3de.sh'), 'hub', 'two words'],
                               capture_output=True, text=True, timeout=30)
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert 'ARGS:hub|two words' in completed.stdout


def test_check_engine_python_venv_broken(tmp_path):
    fake_python = tmp_path / 'bin' / 'python'
    with patch.object(hub, 'get_engine_venv_python', return_value=fake_python), \
         patch.object(hub.subprocess, 'run') as mock_run:
        mock_run.return_value.returncode = 1
        result = hub.check_engine_python_venv(tmp_path)
    assert result.severity == hub.FAIL
    assert 'missing its dependencies' in result.detail


def test_check_engine_python_venv_healthy(tmp_path):
    fake_python = tmp_path / 'bin' / 'python'
    with patch.object(hub, 'get_engine_venv_python', return_value=fake_python), \
         patch.object(hub.subprocess, 'run') as mock_run:
        mock_run.return_value.returncode = 0
        result = hub.check_engine_python_venv(tmp_path)
    assert result.severity == hub.OK


def test_check_engine_python_venv_not_set_up():
    with patch.object(hub, 'get_engine_venv_python', return_value=None):
        result = hub.check_engine_python_venv(pathlib.Path('/engine'))
    assert result.severity == hub.WARN


def test_check_linux_build_dependencies_missing_pkg_config():
    with patch.object(sys, 'platform', 'linux'), \
         patch.object(hub.shutil, 'which', return_value=None):
        result = hub.check_linux_build_dependencies()
    assert result.severity == hub.FAIL
    assert 'pkg-config' in result.detail


def test_check_linux_build_dependencies_missing_modules():
    with patch.object(sys, 'platform', 'linux'), \
         patch.object(hub.shutil, 'which', return_value='/usr/bin/pkg-config'), \
         patch.object(hub.subprocess, 'run') as mock_run:
        mock_run.return_value.returncode = 1
        result = hub.check_linux_build_dependencies()
    assert result.severity == hub.FAIL
    assert 'libunwind' in result.detail


def test_check_linux_build_dependencies_ok():
    with patch.object(sys, 'platform', 'linux'), \
         patch.object(hub.shutil, 'which', return_value='/usr/bin/pkg-config'), \
         patch.object(hub.subprocess, 'run') as mock_run:
        mock_run.return_value.returncode = 0
        assert hub.check_linux_build_dependencies().severity == hub.OK


def test_check_linux_build_dependencies_skipped_off_linux():
    with patch.object(sys, 'platform', 'win32'):
        assert hub.check_linux_build_dependencies() is None


def test_build_fix_plan_installs_linux_build_libraries():
    checks = [_check('Build libraries (Linux)', hub.FAIL, 'pkg-config not found')]
    with patch.object(sys, 'platform', 'linux'), \
         patch.object(hub, 'detect_linux_package_manager', return_value='apt-get'):
        steps = hub.build_fix_plan(checks, pathlib.Path('/engine'))
    package_step = steps[0]
    assert package_step.elevated
    for package in ('pkg-config', 'libunwind-dev', 'libzstd-dev', 'libegl1-mesa-dev'):
        assert package in package_step.command


def test_check_hardware_reports_cpu_ram_gpu():
    with patch.object(hub.os, 'cpu_count', return_value=8), \
         patch.object(hub, '_cpu_model', return_value='TestCPU'), \
         patch.object(hub, '_total_ram_gb', return_value=32.0), \
         patch.object(hub, '_gpu_names', return_value=['TestGPU']), \
         patch.object(hub, '_vulkan_loader_present', return_value=True):
        results = {r.name: r for r in hub.check_hardware()}
    assert results['CPU'].severity == hub.OK and 'TestCPU' in results['CPU'].detail
    assert results['RAM'].severity == hub.OK and '32' in results['RAM'].detail
    assert results['GPU'].severity == hub.OK and 'TestGPU' in results['GPU'].detail


def test_check_hardware_warns_when_weak():
    with patch.object(hub.os, 'cpu_count', return_value=2), \
         patch.object(hub, '_cpu_model', return_value=None), \
         patch.object(hub, '_total_ram_gb', return_value=8.0), \
         patch.object(hub, '_gpu_names', return_value=[]), \
         patch.object(hub, '_vulkan_loader_present', return_value=False):
        results = {r.name: r for r in hub.check_hardware()}
    assert results['CPU'].severity == hub.WARN
    assert results['RAM'].severity == hub.WARN
    assert results['GPU'].severity == hub.WARN


def test_check_hardware_gpu_without_vulkan():
    with patch.object(hub.os, 'cpu_count', return_value=8), \
         patch.object(hub, '_cpu_model', return_value='x'), \
         patch.object(hub, '_total_ram_gb', return_value=32.0), \
         patch.object(hub, '_gpu_names', return_value=['SomeGPU']), \
         patch.object(hub, '_vulkan_loader_present', return_value=False):
        results = {r.name: r for r in hub.check_hardware()}
    assert results['GPU'].severity == hub.WARN
    assert 'Vulkan' in results['GPU'].detail


def test_lfs_tracked_patterns_parses_track_output():
    output = ('Listing tracked patterns\n'
              '    *.png (.gitattributes)\n'
              '    Assets/** (Gems/.gitattributes)\n')
    with patch.object(hub, '_run_git', return_value=(0, output)):
        assert hub.lfs_tracked_patterns(pathlib.Path('/repo')) == ['*.png', 'Assets/**']


def test_lfs_pointer_files_only_reports_pointers():
    output = ('deadbeef01 * Assets/downloaded.png\n'
              'deadbeef02 - Assets/pointer_only.fbx\n'
              'deadbeef03 - Assets/pointer_two.png\n')
    with patch.object(hub, '_run_git', return_value=(0, output)):
        assert hub.lfs_pointer_files(pathlib.Path('/repo')) == [
            'Assets/pointer_only.fbx', 'Assets/pointer_two.png']


def test_lfs_pointer_files_git_failure_is_empty():
    with patch.object(hub, '_run_git', return_value=(1, 'not a git repository')):
        assert hub.lfs_pointer_files(pathlib.Path('/repo')) == []


def test_untracked_checkout_conflicts_intersects_target_tree():
    def fake_git(_repo, args, **_kwargs):
        if args[0] == 'status':
            return 0, '?? Assets/local_new.png\n?? Assets/collision.fbx\n M Code/tracked.cpp\n'
        if args[0] == 'ls-tree':
            return 0, 'Assets/collision.fbx\nCode/tracked.cpp\n'
        raise AssertionError(args)
    with patch.object(hub, '_run_git', side_effect=fake_git):
        assert hub.untracked_checkout_conflicts(pathlib.Path('/repo'), 'other') == [
            'Assets/collision.fbx']


def test_lfs_fetch_needed_lists_fetch_lines():
    output = ('fetch: Fetching reference refs/heads/other\n'
              'fetch deadbeef01 => Assets/big_model.fbx\n'
              'fetch deadbeef02 => Assets/texture.png\n')
    with patch.object(hub, '_run_git', return_value=(0, output)):
        needed = hub.lfs_fetch_needed(pathlib.Path('/repo'), 'other')
    assert 'fetch deadbeef01 => Assets/big_model.fbx' in needed
    assert 'fetch deadbeef02 => Assets/texture.png' in needed


def test_check_lfs_content_warns_on_pointers(tmp_path):
    (tmp_path / '.git').mkdir()
    with patch.object(hub, '_run_git', return_value=(0, 'git-lfs/3.0.2')), \
         patch.object(hub, 'lfs_pointer_files', return_value=['a.fbx', 'b.png']):
        result = hub.check_lfs_content(tmp_path)
    assert result.severity == hub.WARN and '2' in result.detail


def test_check_lfs_content_ok_when_downloaded(tmp_path):
    (tmp_path / '.git').mkdir()
    with patch.object(hub, '_run_git', return_value=(0, 'git-lfs/3.0.2')), \
         patch.object(hub, 'lfs_pointer_files', return_value=[]):
        assert hub.check_lfs_content(tmp_path).severity == hub.OK


def test_check_lfs_content_none_outside_git(tmp_path):
    assert hub.check_lfs_content(tmp_path) is None
