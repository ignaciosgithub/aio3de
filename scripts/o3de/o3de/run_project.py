#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""
'o3de run' - one command from project sources to a running game, identical on
Windows and Linux: configure (auto-detected generator), build the game launcher,
process assets until the Asset Processor is idle, then launch the game.
"""

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

from o3de import hub

PLATFORM_DIR = 'windows' if sys.platform.startswith('win') else 'linux'
EXECUTABLE_EXTENSION = '.exe' if sys.platform.startswith('win') else ''


def project_name(project_path: pathlib.Path) -> str or None:
    try:
        with (project_path / 'project.json').open(encoding='utf-8') as f:
            return json.load(f).get('project_name')
    except (OSError, json.JSONDecodeError):
        return None


def project_build_dir(project_path: pathlib.Path) -> pathlib.Path:
    return project_path / 'build' / PLATFORM_DIR


def stale_build_cache_source(build_dir: pathlib.Path, project_path: pathlib.Path) -> str or None:
    """The foreign source dir recorded in CMakeCache.txt when this build tree was configured
    somewhere else (a project copied from another machine/platform), else None."""
    try:
        cache = (build_dir / 'CMakeCache.txt').read_text(encoding='utf-8', errors='ignore')
    except OSError:
        return None
    match = re.search(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$', cache, re.MULTILINE)
    if not match:
        return None
    recorded = match.group(1).strip()
    try:
        if pathlib.Path(recorded).resolve() == project_path.resolve():
            return None
    except OSError:
        pass
    return recorded


def configure_command(project_path: pathlib.Path) -> list:
    third_party = str(hub.get_third_party_path() or (pathlib.Path.home() / '.o3de' / '3rdParty'))
    command = ['cmake', '-B', str(project_build_dir(project_path)), '-S', str(project_path),
               f'-DLY_3RDPARTY_PATH={third_party}']
    if sys.platform.startswith('win'):
        generator = hub.detect_windows_generator()
        return command + (['-G', generator] if generator else [])
    return command + ['-G', 'Ninja Multi-Config']


def uses_visual_studio_generator(build_dir: pathlib.Path) -> bool:
    try:
        cache = (build_dir / 'CMakeCache.txt').read_text(encoding='utf-8', errors='ignore')
    except OSError:
        return False
    return bool(re.search(r'^CMAKE_GENERATOR:\w+=Visual Studio', cache, re.MULTILINE))


def build_command(project_path: pathlib.Path, target: str, config: str) -> list:
    build_dir = project_build_dir(project_path)
    command = ['cmake', '--build', str(build_dir), '--target', target, '--config', config]
    if uses_visual_studio_generator(build_dir):
        # unbounded MSBuild parallelism exhausts RAM on unity MSVC builds (C1060/LNK1102)
        command += ['--'] + hub.memory_safe_msbuild_args()
    return command


def binary_path(project_path: pathlib.Path, name: str, config: str) -> pathlib.Path:
    return project_build_dir(project_path) / 'bin' / config / (name + EXECUTABLE_EXTENSION)


def find_tool(project_path: pathlib.Path, name: str, config: str) -> pathlib.Path or None:
    """A tool binary (e.g. AssetProcessorBatch) from the project build, the engine build,
    or an installed/prebuilt engine SDK - whichever exists."""
    executable = name + EXECUTABLE_EXTENSION
    engine_root = pathlib.Path(__file__).resolve().parents[3]
    candidates = [
        project_build_dir(project_path) / 'bin' / config / executable,
        engine_root / 'build' / PLATFORM_DIR / 'bin' / config / executable,
        engine_root / 'bin' / PLATFORM_DIR / config / 'Default' / executable,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def _stream(command: list, cwd: pathlib.Path = None) -> int:
    print('> ' + ' '.join(str(part) for part in command))
    tail = []
    process = subprocess.Popen([str(part) for part in command], cwd=str(cwd) if cwd else None,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               errors='replace')
    for line in process.stdout:
        sys.stdout.write(line)
        tail.append(line)
        if len(tail) > 200:
            tail.pop(0)
    process.wait()
    if process.returncode != 0:
        hint = hub.build_oom_hint(''.join(tail))
        if hint:
            print(f'HINT: {hint}')
    return process.returncode


def run_project(project_path: pathlib.Path, config: str = 'profile', skip_build: bool = False,
                skip_assets: bool = False, clean: bool = False) -> int:
    project_path = project_path.resolve()
    name = project_name(project_path)
    if not name:
        print(f'ERROR: {project_path} does not contain a readable project.json.')
        return 1

    build_dir = project_build_dir(project_path)

    if clean:
        for stale in (build_dir, project_path / 'Cache', project_path / 'user'):
            if stale.exists():
                print(f'Removing {stale} ...')
                shutil.rmtree(stale, ignore_errors=True)

    stale_source = stale_build_cache_source(build_dir, project_path)
    if stale_source:
        print(f'Build directory {build_dir} was configured for a different location '
              f'({stale_source}) - removing it so CMake can reconfigure cleanly.')
        shutil.rmtree(build_dir, ignore_errors=True)

    if not skip_build:
        if not (build_dir / 'CMakeCache.txt').is_file():
            if _stream(configure_command(project_path)) != 0:
                print('ERROR: CMake configure failed.')
                return 1
        if _stream(build_command(project_path, f'{name}.GameLauncher', config)) != 0:
            print('ERROR: Build failed.')
            return 1

    if not skip_assets:
        asset_processor_batch = find_tool(project_path, 'AssetProcessorBatch', config)
        if asset_processor_batch:
            print('Processing assets (this can take a while on first run)...')
            if _stream([asset_processor_batch, f'--project-path={project_path}']) != 0:
                print('WARNING: Asset processing reported failures; the game may still run '
                      'if no required asset failed (see the output above).')
        else:
            print('WARNING: AssetProcessorBatch not found; skipping asset processing. '
                  'The launcher will try to start the Asset Processor itself.')

    launcher = binary_path(project_path, f'{name}.GameLauncher', config)
    if not launcher.is_file():
        print(f'ERROR: {launcher} does not exist. Build the project first (or re-run without --no-build).')
        return 1
    print(f'Launching {launcher} ...')
    return subprocess.call([str(launcher), f'--project-path={project_path}'])


EXPORT_EXCLUDED_DIRS = {'build', 'Cache', 'user', '.git', '.vs', '.vscode', '__pycache__'}


def export_source(project_path: pathlib.Path, output: pathlib.Path = None) -> int:
    """Zip the project's sources and assets into a platform-agnostic archive: no build trees,
    no asset cache, no per-user state - unzip it on any platform and 'o3de run' it."""
    import zipfile

    project_path = project_path.resolve()
    name = project_name(project_path)
    if not name:
        print(f'ERROR: {project_path} does not contain a readable project.json.')
        return 1
    output = (output or (project_path.parent / f'{name}-source.zip')).resolve()

    file_count = 0
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(project_path.rglob('*')):
            relative = path.relative_to(project_path)
            if any(part in EXPORT_EXCLUDED_DIRS for part in relative.parts):
                continue
            if path.is_file():
                archive.write(path, arcname=str(pathlib.PurePosixPath(name) / relative.as_posix()))
                file_count += 1
    print(f'Exported {file_count} file(s) to {output}')
    return 0


def _run_run(args: argparse.Namespace) -> int:
    return run_project(args.project_path, config=args.config, skip_build=args.no_build,
                       skip_assets=args.no_assets, clean=args.clean)


def _run_export_source(args: argparse.Namespace) -> int:
    return export_source(args.project_path, args.output)


def add_parser_args(parser):
    parser.add_argument('-p', '--project', '-pp', '--project-path', dest='project_path',
                        type=pathlib.Path, required=True,
                        help='Path to the project to build, process, and run.')
    parser.add_argument('-c', '--config', default='profile',
                        choices=['debug', 'profile', 'release'],
                        help='Build configuration to use (default: profile).')
    parser.add_argument('--no-build', action='store_true',
                        help='Skip configure/build; just process assets and launch.')
    parser.add_argument('--no-assets', action='store_true',
                        help='Skip asset processing; just build and launch.')
    parser.add_argument('--clean', action='store_true',
                        help="Delete the project's build, Cache, and user folders first "
                             '(use when opening a project copied from another platform).')
    parser.set_defaults(func=_run_run)


def add_args(subparsers) -> None:
    """
    add_args is called to add the 'run' sub-command to the central o3de.py parser.
    Example: python o3de.py run --project /path/to/project
    :param subparsers: the caller instantiates subparsers and passes it in here
    """
    run_subparser = subparsers.add_parser(
        'run', help='Build the project, process its assets, and launch the game - one command, '
                    'same behavior on Windows and Linux.')
    add_parser_args(run_subparser)

    export_subparser = subparsers.add_parser(
        'export-source',
        help='Archive the project sources/assets into a clean platform-agnostic '
             'zip (no build/, Cache/, user/, or .git/).')
    export_subparser.add_argument('-p', '--project', '-pp', '--project-path', dest='project_path',
                                  type=pathlib.Path, required=True,
                                  help='Path to the project to archive.')
    export_subparser.add_argument('-o', '--output', type=pathlib.Path, default=None,
                                  help='Output zip path (default: <project parent>/<name>-source.zip).')
    export_subparser.set_defaults(func=_run_export_source)


def main():
    the_parser = argparse.ArgumentParser()
    add_parser_args(the_parser)
    the_args = the_parser.parse_args()
    sys.exit(the_args.func(the_args) if hasattr(the_args, 'func') else 1)


if __name__ == "__main__":
    main()
