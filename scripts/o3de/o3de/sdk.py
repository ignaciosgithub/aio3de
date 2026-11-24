#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#

"""Install a prebuilt AIO3DE SDK from GitHub Releases using only the standard library."""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile


REPO = 'ignaciosgithub/aio3de'
USER_AGENT = 'aio3de-sdk-installer/1.0'
_ASSET_RE = re.compile(r'^(aio3de-sdk-.+-(?:linux|windows)-x86_64\.(?:tar\.zst|zip))(?:\.part(\d+))?$')


class SdkError(Exception):
    pass


def _log(log, message):
    if callable(log):
        log(message)
    elif log is not None:
        print(message, file=log)


def platform_key():
    machine = platform.machine().lower()
    if machine not in ('x86_64', 'amd64'):
        return None
    if sys.platform.startswith('linux'):
        return 'linux-x86_64'
    if sys.platform == 'win32':
        return 'windows-x86_64'
    return None


def _headers(api=False):
    headers = {'User-Agent': USER_AGENT}
    token = os.environ.get('GITHUB_TOKEN')
    if token:
        headers['Authorization'] = f'Bearer {token}'
    if api:
        headers['Accept'] = 'application/vnd.github+json'
    return headers


def _api_url(path):
    base = os.environ.get('AIO3DE_SDK_API_URL', 'https://api.github.com').rstrip('/')
    return f'{base}{path}'


def list_releases():
    request = urllib.request.Request(
        _api_url(f'/repos/{REPO}/releases'), headers=_headers(api=True))
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            data = json.loads(response.read().decode('utf-8'))
    except (OSError, urllib.error.URLError, ValueError) as exc:
        raise SdkError(f'Could not list GitHub releases: {exc}') from exc
    if not isinstance(data, list):
        raise SdkError('GitHub returned an invalid releases response.')
    return data


def select_sdk_assets(release, plat):
    tag = release.get('tag_name', '')
    assets = release.get('assets') or []
    singles = {}
    parts = {}
    sums = None
    for asset in assets:
        name = asset.get('name', '')
        if name == 'SHA256SUMS':
            sums = asset
            continue
        match = _ASSET_RE.match(name)
        if not match:
            continue
        archive_name = match.group(1)
        if not archive_name.startswith(f'aio3de-sdk-{tag}-{plat}.'):
            continue
        if match.group(2) is None:
            singles[archive_name] = asset
        else:
            parts.setdefault(archive_name, []).append((int(match.group(2)), asset))
    for archive_name in sorted(singles):
        return archive_name, [singles[archive_name]], sums
    for archive_name in sorted(parts):
        ordered = [asset for _, asset in sorted(parts[archive_name], key=lambda pair: pair[0])]
        return archive_name, ordered, sums
    return None


def find_release(tag=None, plat=None):
    plat = plat or platform_key()
    if not plat:
        return None
    for release in list_releases():
        if tag is not None and release.get('tag_name') != tag:
            continue
        if select_sdk_assets(release, plat):
            return release
    return None


def download(url, dest, log=None):
    """Download or resume an asset into dest, keeping resumable bytes in dest+'.download'."""
    dest = pathlib.Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    partial = pathlib.Path(str(dest) + '.download')
    offset = partial.stat().st_size if partial.exists() else 0
    headers = _headers()
    if offset:
        headers['Range'] = f'bytes={offset}-'
    request = urllib.request.Request(url, headers=headers)
    try:
        response = urllib.request.urlopen(request, timeout=60)
    except urllib.error.HTTPError as exc:
        content_range = exc.headers.get('Content-Range', '')
        match = re.match(r'bytes \*/(\d+)', content_range)
        if exc.code == 416 and match and offset == int(match.group(1)):
            partial.replace(dest)
            return dest
        if offset:
            partial.unlink(missing_ok=True)
            return download(url, dest, log)
        raise SdkError(f'Download failed for {url}: {exc}') from exc
    except (OSError, urllib.error.URLError) as exc:
        raise SdkError(f'Download failed for {url}: {exc}') from exc

    with response:
        status = getattr(response, 'status', response.getcode())
        append = bool(offset and status == 206)
        if offset and status not in (200, 206):
            raise SdkError(f'Unexpected HTTP status {status} while resuming {url}.')
        if append:
            content_range = response.headers.get('Content-Range', '')
            match = re.match(r'bytes (\d+)-\d+/(\d+)', content_range)
            if match and int(match.group(1)) != offset:
                partial.unlink(missing_ok=True)
                return download(url, dest, log)
            total = int(match.group(2)) if match else offset + int(response.headers.get('Content-Length', 0))
        else:
            offset = 0
            total = int(response.headers.get('Content-Length', 0))
        mode = 'ab' if append else 'wb'
        written = offset
        next_percent = 5
        with partial.open(mode) as output:
            while True:
                chunk = response.read(1024 * 256)
                if not chunk:
                    break
                output.write(chunk)
                written += len(chunk)
                if total:
                    percent = min(100, written * 100 // total)
                    while percent >= next_percent and next_percent <= 100:
                        _log(log, f'Downloading {dest.name}: {next_percent}%')
                        next_percent += 5
    partial.replace(dest)
    return dest


def parse_sha256sums(text):
    sums = {}
    for line in text.splitlines():
        match = re.match(r'^\s*([0-9a-fA-F]{64})\s+\*?(.+?)\s*$', line)
        if match:
            sums[match.group(2)] = match.group(1).lower()
    return sums


def _verify(path, name, sums, log):
    expected = sums.get(name)
    if expected is None:
        _log(log, f'Warning: SHA256SUMS has no entry for {name}; continuing without verification.')
        return
    digest = hashlib.sha256()
    with pathlib.Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    if digest.hexdigest() != expected:
        pathlib.Path(path).unlink(missing_ok=True)
        raise SdkError(f'SHA-256 mismatch for {name}; the downloaded file was deleted.')


def _download_archive(archive_name, assets, sums, downloads, log):
    downloaded = []
    for asset in assets:
        path = download(asset['browser_download_url'], downloads / asset['name'], log)
        size = asset.get('size')
        if size is not None and path.stat().st_size != size:
            path.unlink(missing_ok=True)
            raise SdkError(f'Incomplete download for {asset["name"]}.')
        _verify(path, asset['name'], sums, log)
        downloaded.append(path)
    split_parts = not (len(downloaded) == 1 and downloaded[0].name == archive_name)
    if not split_parts:
        archive = downloaded[0]
    else:
        archive = downloads / archive_name
        temporary = pathlib.Path(str(archive) + '.download')
        with temporary.open('wb') as output:
            for part in downloaded:
                with part.open('rb') as source:
                    shutil.copyfileobj(source, output, 1024 * 1024)
        temporary.replace(archive)
    _verify(archive, archive_name, sums, log)
    if split_parts:
        for part in downloaded:
            part.unlink(missing_ok=True)
    return archive


def _remove(path):
    path = pathlib.Path(path)
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def _safe_member(root, name):
    normalized = name.replace('\\', '/')
    member = pathlib.PurePosixPath(normalized)
    if member.is_absolute() or '..' in member.parts:
        raise SdkError(f'Unsafe archive path: {name}')
    target = (root / pathlib.Path(*member.parts)).resolve()
    if os.path.commonpath((str(root.resolve()), str(target))) != str(root.resolve()):
        raise SdkError(f'Unsafe archive path: {name}')
    return target


def _extract(archive, partial, log):
    partial.mkdir(parents=True, exist_ok=True)
    if archive.name.endswith('.zip'):
        with zipfile.ZipFile(archive) as source:
            for info in source.infolist():
                _safe_member(partial, info.filename)
            source.extractall(partial)
    elif archive.name.endswith('.tar.zst'):
        if not shutil.which('zstd'):
            raise SdkError('zstd is required to extract this SDK; install it with `sudo apt install zstd`.')
        listing = subprocess.run(['tar', '--zstd', '-tf', str(archive)],
                                capture_output=True, text=True, check=False)
        if listing.returncode != 0:
            listing = subprocess.run(['tar', '-I', 'zstd', '-tf', str(archive)],
                                    capture_output=True, text=True, check=False)
        if listing.returncode != 0:
            raise SdkError(f'Could not inspect SDK archive: {listing.stderr.strip()}')
        for name in listing.stdout.splitlines():
            _safe_member(partial, name)
        extracted = subprocess.run(['tar', '--zstd', '-xf', str(archive), '-C', str(partial)],
                                  capture_output=True, text=True, check=False)
        if extracted.returncode != 0:
            extracted = subprocess.run(['tar', '-I', 'zstd', '-xf', str(archive), '-C', str(partial)],
                                       capture_output=True, text=True, check=False)
        if extracted.returncode != 0:
            raise SdkError(f'Could not extract SDK archive: {extracted.stderr.strip()}')
    else:
        raise SdkError(f'Unsupported SDK archive format: {archive.name}')

    entries = list(partial.iterdir())
    if len(entries) == 1 and entries[0].is_dir():
        top_level = entries[0]
        for child in list(top_level.iterdir()):
            child.replace(partial / child.name)
        top_level.rmdir()
    _log(log, f'Extracted SDK to {partial}')


def _register_engine(install_dir, log):
    script = install_dir / 'scripts' / ('o3de.bat' if os.name == 'nt' else 'o3de.sh')
    command = [str(script), 'register', '--this-engine']
    if os.name == 'nt':
        command = [os.environ.get('COMSPEC', 'cmd.exe'), '/c', *command]
    _log(log, f'Registering SDK: {" ".join(command)}')
    result = subprocess.run(command, cwd=str(install_dir), check=False)
    if result.returncode:
        _log(log, f'Could not register the SDK. Rerun: {" ".join(command)}')
        _log(log, 'The first run bootstraps Python and requires CMake >= 3.24; `o3de hub install` can install CMake.')
        return False
    return True


def _print_next_steps(install_dir, tag, log):
    platform_dir = 'Windows' if os.name == 'nt' else 'Linux'
    executable = 'o3de.exe' if os.name == 'nt' else 'o3de'
    manager = install_dir / 'bin' / platform_dir / 'profile' / 'Default' / executable
    _log(log, f'Project Manager: {manager}')
    script = install_dir / 'scripts' / ('o3de.bat' if os.name == 'nt' else 'o3de.sh')
    project_path = pathlib.Path.home() / 'O3DEProjects' / 'RacingProject'
    _log(log, f'Create a Racing project: "{script}" create-project --project-path "{project_path}" --template-name RacingProject')
    if os.name == 'nt':
        build_dir = 'build\\windows'
        generator = 'Visual Studio 17 2022'
    else:
        build_dir = 'build/linux'
        generator = 'Ninja Multi-Config'
    _log(log, f'Configure from {project_path}: cmake -B {build_dir} -S . -G "{generator}"')
    _log(log, f'Build: cmake --build {build_dir} --config profile --target RacingProject.GameLauncher Editor')


def install(tag=None, install_dir=None, force=False, keep_download=False, no_register=False, log=None):
    plat = platform_key()
    if not plat:
        raise SdkError('no prebuilt SDK for this platform yet; build from source')
    requested_dir = pathlib.Path(install_dir).expanduser().absolute() if install_dir else None
    if requested_dir and (requested_dir.exists() or requested_dir.is_symlink()) and not force:
        raise SdkError(f'{requested_dir} already exists; use --force to replace it.')
    release = find_release(tag, plat)
    if not release:
        wanted = f' tagged {tag}' if tag else ''
        raise SdkError(f'No release{wanted} has a prebuilt SDK for {plat}.')
    tag = release.get('tag_name', '')
    if not re.match(r'^[A-Za-z0-9._-]+$', tag):
        raise SdkError('The release tag contains unsupported path characters.')
    selected = select_sdk_assets(release, plat)
    if not selected:
        raise SdkError(f'Release {tag} has no prebuilt SDK for {plat}.')
    archive_name, assets, sums_asset = selected
    install_dir = requested_dir or (
        pathlib.Path.home() / 'O3DE' / 'Engines' / f'aio3de-sdk-{tag}')
    install_dir = install_dir.absolute()
    if (install_dir.exists() or install_dir.is_symlink()) and not force:
        raise SdkError(f'{install_dir} already exists; use --force to replace it.')
    downloads = pathlib.Path.home() / 'O3DE' / 'Downloads' / f'aio3de-sdk-{tag}'
    downloads.mkdir(parents=True, exist_ok=True)
    sums = {}
    sums_path = None
    if sums_asset:
        sums_path = download(sums_asset['browser_download_url'], downloads / 'SHA256SUMS', log)
        sums = parse_sha256sums(sums_path.read_text(encoding='utf-8', errors='replace'))
    else:
        _log(log, 'Warning: release has no SHA256SUMS; continuing without checksum verification.')
    archive = _download_archive(archive_name, assets, sums, downloads, log)
    install_dir.parent.mkdir(parents=True, exist_ok=True)
    temporary = pathlib.Path(tempfile.mkdtemp(prefix='.aio3de-sdk-', dir=install_dir.parent))
    try:
        _extract(archive, temporary, log)
        if install_dir.exists() or install_dir.is_symlink():
            if not force:
                raise SdkError(f'{install_dir} already exists; use --force to replace it.')
            _remove(install_dir)
        temporary.replace(install_dir)
    except Exception:
        _remove(temporary)
        raise
    registered = True
    if not no_register:
        registered = _register_engine(install_dir, log)
    if not keep_download and registered:
        _remove(downloads)
    _print_next_steps(install_dir, tag, log)
    return install_dir if registered else False


def _format_size(size):
    value = float(size)
    for unit in ('B', 'KiB', 'MiB', 'GiB'):
        if value < 1024 or unit == 'GiB':
            return f'{value:.1f} {unit}'
        value /= 1024
    return f'{value:.1f} GiB'


def _run_list(_args):
    plat = platform_key()
    if not plat:
        raise SdkError('no prebuilt SDK for this platform yet; build from source')
    found = 0
    for release in list_releases():
        selected = select_sdk_assets(release, plat)
        if not selected:
            continue
        archive_name, assets, _sums = selected
        size = sum(int(asset.get('size', 0)) for asset in assets)
        print(f'{release.get("tag_name", "")}\t{_format_size(size)}\t{archive_name}')
        found += 1
    if not found:
        print(f'No prebuilt SDK releases found for {plat}.')
    return 0


def _run_install(args):
    result = install(args.tag, args.install_dir, args.force, args.keep_download, args.no_register)
    return 1 if result is False else 0


def _handle_error(function, args):
    try:
        return function(args)
    except SdkError as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 1


def add_parser_args(parser):
    subparsers = parser.add_subparsers(dest='sdk_command')
    list_parser = subparsers.add_parser('list', help='List releases with an SDK for this platform.')
    list_parser.set_defaults(func=lambda args: _handle_error(_run_list, args))
    install_parser = subparsers.add_parser('install', help='Download, verify, extract, and register an SDK.')
    install_parser.add_argument('--tag', help='Release tag (defaults to newest compatible release).')
    install_parser.add_argument('--install-dir', type=pathlib.Path, help='Install directory.')
    install_parser.add_argument('--force', action='store_true', help='Replace an existing install directory.')
    install_parser.add_argument('--keep-download', action='store_true', help='Keep archive files after install.')
    install_parser.add_argument('--no-register', action='store_true', help='Do not register the SDK.')
    install_parser.set_defaults(func=lambda args: _handle_error(_run_install, args))
    parser.set_defaults(func=lambda _args: (parser.print_help() or 1))


def main(argv=None):
    parser = argparse.ArgumentParser(description='Install prebuilt AIO3DE SDKs.')
    add_parser_args(parser)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except SdkError as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
