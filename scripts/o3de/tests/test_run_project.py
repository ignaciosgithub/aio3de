#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

import json
import pathlib
import zipfile
from unittest.mock import patch

import pytest

from o3de import run_project


def make_project(tmp_path: pathlib.Path, name: str = 'TestGame') -> pathlib.Path:
    project = tmp_path / name
    project.mkdir()
    (project / 'project.json').write_text(json.dumps({'project_name': name}))
    return project


def test_project_name_read_from_project_json(tmp_path):
    project = make_project(tmp_path)
    assert run_project.project_name(project) == 'TestGame'


def test_project_name_missing_project_json(tmp_path):
    assert run_project.project_name(tmp_path) is None


def test_stale_build_cache_detects_foreign_source(tmp_path):
    project = make_project(tmp_path)
    build_dir = run_project.project_build_dir(project)
    build_dir.mkdir(parents=True)
    (build_dir / 'CMakeCache.txt').write_text(
        'CMAKE_HOME_DIRECTORY:INTERNAL=C:/Users/other/OtherGame\n')
    assert run_project.stale_build_cache_source(build_dir, project) == 'C:/Users/other/OtherGame'


def test_stale_build_cache_accepts_own_source(tmp_path):
    project = make_project(tmp_path)
    build_dir = run_project.project_build_dir(project)
    build_dir.mkdir(parents=True)
    (build_dir / 'CMakeCache.txt').write_text(
        f'CMAKE_HOME_DIRECTORY:INTERNAL={project}\n')
    assert run_project.stale_build_cache_source(build_dir, project) is None


def test_stale_build_cache_no_cache_file(tmp_path):
    project = make_project(tmp_path)
    assert run_project.stale_build_cache_source(run_project.project_build_dir(project), project) is None


def test_build_command_adds_memory_safe_args_for_visual_studio(tmp_path):
    project = make_project(tmp_path)
    build_dir = run_project.project_build_dir(project)
    build_dir.mkdir(parents=True)
    (build_dir / 'CMakeCache.txt').write_text('CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n')
    with patch('o3de.hub.memory_safe_msbuild_args', return_value=['/m:4', '/p:CL_MPCount=1']):
        command = run_project.build_command(project, 'TestGame.GameLauncher', 'profile')
    assert command[-3:] == ['--', '/m:4', '/p:CL_MPCount=1']
    assert '--target' in command and 'TestGame.GameLauncher' in command


def test_build_command_no_msbuild_args_for_ninja(tmp_path):
    project = make_project(tmp_path)
    build_dir = run_project.project_build_dir(project)
    build_dir.mkdir(parents=True)
    (build_dir / 'CMakeCache.txt').write_text('CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n')
    command = run_project.build_command(project, 'TestGame.GameLauncher', 'profile')
    assert '--' not in command


def test_export_source_excludes_build_cache_user_git(tmp_path):
    project = make_project(tmp_path)
    (project / 'Assets').mkdir()
    (project / 'Assets' / 'level.prefab').write_text('{}')
    for excluded in ('build', 'Cache', 'user', '.git'):
        directory = project / excluded
        directory.mkdir()
        (directory / 'junk.bin').write_text('x')

    output = tmp_path / 'out.zip'
    assert run_project.export_source(project, output) == 0

    names = zipfile.ZipFile(output).namelist()
    assert 'TestGame/project.json' in names
    assert 'TestGame/Assets/level.prefab' in names
    assert not any('junk.bin' in name for name in names)


def test_export_source_requires_project_json(tmp_path):
    assert run_project.export_source(tmp_path, tmp_path / 'out.zip') == 1


def test_run_project_requires_project_json(tmp_path):
    assert run_project.run_project(tmp_path) == 1


@pytest.mark.parametrize('flag', ['--project', '-p', '--project-path', '-pp'])
def test_run_parser_accepts_project_flag_spellings(flag):
    import argparse
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()
    run_project.add_args(subparsers)
    args = parser.parse_args(['run', flag, '/tmp/x'])
    assert args.project_path == pathlib.Path('/tmp/x')


def test_export_source_parser_accepts_project_flag():
    import argparse
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()
    run_project.add_args(subparsers)
    args = parser.parse_args(['export-source', '--project', '/tmp/x'])
    assert args.project_path == pathlib.Path('/tmp/x')
