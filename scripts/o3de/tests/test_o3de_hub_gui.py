#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

import importlib.util
import pathlib
import sys

import pytest

# The GUI lives at <engine>/scripts/o3de_hub_gui.py (not inside the o3de package). Load it by path so
# these tests do not depend on cwd. Importing it must NOT require Tkinter or a display - the Tk import
# is deferred into main() exactly so the pure helpers stay headlessly testable.
_GUI_PATH = pathlib.Path(__file__).resolve().parents[2] / 'o3de_hub_gui.py'


@pytest.fixture(scope='module')
def gui():
    spec = importlib.util.spec_from_file_location('o3de_hub_gui', _GUI_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules['o3de_hub_gui'] = module
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("name, valid", [
    ("MyGame", True),
    ("My_Game-2", True),
    ("a", True),
    ("", False),
    ("1Game", False),
    ("My Game", False),
    ("My.Game", False),
    ("x" * 64, False),
])
def test_validate_project_name(gui, name, valid):
    assert (gui.validate_project_name(name) is None) == valid


def test_build_create_project_command(gui):
    command = gui.build_create_project_command(pathlib.Path('/engine'),
                                               pathlib.Path('/projects/MyGame'), 'MyGame')
    assert command[1:] == ['create-project', '--project-path', str(pathlib.Path('/projects/MyGame')),
                           '--project-name', 'MyGame']
    assert command[0].endswith('o3de.bat') or command[0].endswith('o3de.sh')


def test_build_register_engine_command(gui):
    command = gui.build_register_engine_command(pathlib.Path('/engine'))
    assert command[1:] == ['register', '--this-engine']


@pytest.mark.parametrize("text, expected", [
    ("see https://aka.ms/vs/17/release/vc_redist.x64.exe and retry.",
     "https://aka.ms/vs/17/release/vc_redist.x64.exe"),
    ("install it (https://git-lfs.com).", "https://git-lfs.com"),
    ("no link here", None),
    ("", None),
])
def test_first_url(gui, text, expected):
    assert gui.first_url(text) == expected


def test_o3de_launcher_platform(gui):
    launcher = gui.o3de_launcher(pathlib.Path('/engine'))
    assert launcher.name in ('o3de.bat', 'o3de.sh')

def test_build_configure_command(gui):
    from unittest.mock import patch
    with patch.object(gui.hub, 'get_third_party_path', return_value=pathlib.Path('/tp')):
        command = gui.build_configure_command(pathlib.Path('/projects/MyGame'))
    assert command[0] == 'cmake'
    assert '-DLY_3RDPARTY_PATH=' + str(pathlib.Path('/tp')) in command
    build_dir = str(gui.project_build_dir(pathlib.Path('/projects/MyGame')))
    assert command[command.index('-B') + 1] == build_dir
    assert command[command.index('-S') + 1] == str(pathlib.Path('/projects/MyGame'))


def test_build_build_command(gui):
    command = gui.build_build_command(pathlib.Path('/projects/MyGame'), 'Editor')
    assert command[:2] == ['cmake', '--build']
    assert 'Editor' in command
    assert 'profile' in command


def test_binary_path(gui):
    path = gui.binary_path(pathlib.Path('/projects/MyGame'), 'Editor')
    assert path.name in ('Editor', 'Editor.exe')
    assert 'profile' in str(path)


def test_registered_projects_reads_manifest(gui, tmp_path, monkeypatch):
    manifest_dir = tmp_path / '.o3de'
    manifest_dir.mkdir()
    (manifest_dir / 'o3de_manifest.json').write_text('{"projects": ["/p/one", "/p/two"]}')
    monkeypatch.setenv('USERPROFILE', str(tmp_path))
    monkeypatch.setattr(pathlib.Path, 'home', staticmethod(lambda: tmp_path))
    assert gui.registered_projects() == ['/p/one', '/p/two']


def test_registered_projects_missing_manifest(gui, tmp_path, monkeypatch):
    monkeypatch.setenv('USERPROFILE', str(tmp_path))
    monkeypatch.setattr(pathlib.Path, 'home', staticmethod(lambda: tmp_path))
    assert gui.registered_projects() == []
