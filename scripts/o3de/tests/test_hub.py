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
    # Pre-bootstrap: the manifest module (and its venv-only deps) cannot be imported yet.
    with patch.object(hub, '_try_import_manifest', return_value=None):
        result = hub.check_engine_registration(tmp_path)
    assert result.severity == hub.WARN
    assert 'bootstrapped' in result.detail


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
