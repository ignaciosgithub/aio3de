#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#

import hashlib
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import tarfile
import threading
import zipfile

import pytest

from o3de import sdk


def asset(name, url='http://local'):
    return {'name': name, 'browser_download_url': f'{url}/{name}', 'size': 0}


def test_select_sdk_assets_single_parts_and_legacy():
    rel = {'tag_name': 'v1', 'assets': [asset('aio3de-v1-linux-x86_64-profile.tar.zst'),
        asset('aio3de-sdk-v1-windows-x86_64.zip'), asset('aio3de-sdk-v1-linux-x86_64.tar.zst'),
        asset('aio3de-sdk-v1-linux-x86_64.tar.zst.part10'),
        asset('aio3de-sdk-v1-linux-x86_64.tar.zst.part09'), asset('SHA256SUMS')]}
    name, files, sums = sdk.select_sdk_assets(rel, 'linux-x86_64')
    assert name == 'aio3de-sdk-v1-linux-x86_64.tar.zst'
    assert [a['name'] for a in files] == [name]
    assert sums['name'] == 'SHA256SUMS'
    rel['assets'] = [a for a in rel['assets'] if not a['name'].endswith('.tar.zst')]
    name, files, _ = sdk.select_sdk_assets(rel, 'linux-x86_64')
    assert name.endswith('.tar.zst')
    assert [a['name'][-6:] for a in files] == ['part09', 'part10']
    assert sdk.select_sdk_assets({'tag_name': 'v2', 'assets': [asset('aio3de-v1-linux-x86_64-profile.tar.zst')]},
                                 'linux-x86_64') is None


def test_find_release_skips_missing_platform(monkeypatch):
    releases = [{'tag_name': 'v2', 'assets': []},
                {'tag_name': 'v1', 'assets': [asset('aio3de-sdk-v1-linux-x86_64.tar.zst')]}]
    monkeypatch.setattr(sdk, 'list_releases', lambda: releases)
    assert sdk.find_release(plat='linux-x86_64')['tag_name'] == 'v1'
    assert sdk.find_release('v2', 'linux-x86_64') is None
    assert sdk.find_release('v1', 'linux-x86_64')['tag_name'] == 'v1'


def test_sha256_parse_verify_and_delete(tmp_path):
    data = b'content'
    file = tmp_path / 'archive'
    file.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    sums = sdk.parse_sha256sums(f'{digest}  archive\n')
    sdk._verify(file, 'archive', sums, None)
    with pytest.raises(sdk.SdkError):
        sdk._verify(file, 'archive', {'archive': '0' * 64}, None)
    assert not file.exists()


def test_split_archive_removes_parts_after_verification(tmp_path, monkeypatch):
    name = 'aio3de-sdk-v1-linux-x86_64.tar.zst'
    chunks = {'part00': b'first', 'part01': b'second'}
    paths = []

    def fake_download(url, dest, _log=None):
        part = pathlib.Path(url).name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(chunks[part])
        paths.append(dest)
        return dest

    monkeypatch.setattr(sdk, 'download', fake_download)
    assets = [{'name': part, 'browser_download_url': part} for part in chunks]
    data = b''.join(chunks.values())
    sums = {part: hashlib.sha256(content).hexdigest() for part, content in chunks.items()}
    sums[name] = hashlib.sha256(data).hexdigest()
    archive = sdk._download_archive(name, assets, sums, tmp_path, None)
    assert archive.read_bytes() == data
    assert all(not path.exists() for path in paths)


def test_install_cli_returns_nonzero_on_registration_failure(monkeypatch):
    monkeypatch.setattr(sdk, 'install', lambda *_args, **_kwargs: False)
    assert sdk.main(['install', '--tag', 'v1']) == 1


def test_next_steps_include_project_configure_and_build(tmp_path):
    output = []
    sdk._print_next_steps(tmp_path, 'v1', output.append)
    assert any(f'{tmp_path}/scripts/o3de.sh" create-project' in line for line in output)
    assert any('cmake -B build/linux -S . -G "Ninja Multi-Config"' in line for line in output)
    assert any('RacingProject.GameLauncher Editor' in line for line in output)


class Server(http.server.BaseHTTPRequestHandler):
    routes = {}
    ignore_range = False

    def do_GET(self):
        if self.path not in self.routes:
            self.send_error(404)
            return
        data = self.routes[self.path]
        start = 0
        if self.headers.get('Range') and not self.ignore_range:
            start = int(self.headers['Range'].split('=')[1].split('-')[0])
        body = data[start:]
        self.send_response(206 if start else 200)
        if start:
            self.send_header('Content-Range', f'bytes {start}-{len(data)-1}/{len(data)}')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def serve(routes, ignore_range=False):
    Server.routes = routes
    Server.ignore_range = ignore_range
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, f'http://127.0.0.1:{server.server_port}'


@pytest.mark.parametrize('ignore', [False, True])
def test_download_resume_and_restart(tmp_path, ignore):
    data = b'0123456789' * 20
    server, base = serve({'/file': data}, ignore)
    try:
        dest = tmp_path / 'file'
        pathlib.Path(str(dest) + '.download').write_bytes(data[:23])
        sdk.download(base + '/file', dest)
        assert dest.read_bytes() == data
    finally:
        server.shutdown()


def test_extract_zip_flatten_and_existing_install(tmp_path):
    archive = tmp_path / 'sdk.zip'
    with zipfile.ZipFile(archive, 'w') as z:
        z.writestr('aio3de-sdk/scripts/o3de.sh', 'ok')
    partial = pathlib.Path(str(tmp_path / 'install') + '.partial')
    sdk._extract(archive, partial, None)
    assert (partial / 'scripts/o3de.sh').read_text() == 'ok'
    install = tmp_path / 'install'
    partial.replace(install)
    with pytest.raises(sdk.SdkError):
        sdk.install(install_dir=install)
    assert install.exists()


@pytest.mark.skipif(os.name == 'nt' or not shutil.which('zstd'), reason='tar.zst round trip needs Linux zstd')
def test_install_split_tar_zst_fake_api(tmp_path, monkeypatch, capsys):
    source = tmp_path / 'aio3de-sdk'
    (source / 'scripts').mkdir(parents=True)
    stub = source / 'scripts/o3de.sh'
    stub.write_text('#!/bin/sh\nprintf registered > \"$(dirname \"$0\")/registered\"\n')
    stub.chmod(0o755)
    tar = tmp_path / 'sdk.tar'
    with tarfile.open(tar, 'w') as out:
        out.add(source, arcname='aio3de-sdk')
    compressed = tmp_path / 'sdk.tar.zst'
    subprocess.run(['zstd', '-q', str(tar), '-o', str(compressed)], check=True)
    data = compressed.read_bytes()
    chunks = [data[:len(data)//2], data[len(data)//2:]]
    tag = 'v0.0-test'
    archive_name = f'aio3de-sdk-{tag}-linux-x86_64.tar.zst'
    names = [archive_name + '.part00', archive_name + '.part01']
    sums = ''.join(f'{hashlib.sha256(part).hexdigest()}  {name}\n' for name, part in zip(names, chunks))
    sums += f'{hashlib.sha256(data).hexdigest()}  {archive_name}\n'
    routes = {'/repos/ignaciosgithub/aio3de/releases': None,
              '/SHA256SUMS': sums.encode()}
    server, base = serve(routes)
    try:
        routes['/repos/ignaciosgithub/aio3de/releases'] = json.dumps([{'tag_name': tag, 'assets': [
            {'name': 'SHA256SUMS', 'browser_download_url': base + '/SHA256SUMS', 'size': len(sums)},
            *[{'name': n, 'browser_download_url': base + '/' + n, 'size': len(c)} for n, c in zip(names, chunks)]]}]).encode()
        routes.update({f'/{n}': c for n, c in zip(names, chunks)})
        monkeypatch.setenv('AIO3DE_SDK_API_URL', base)
        home = tmp_path / 'home'
        monkeypatch.setattr(pathlib.Path, 'home', lambda: home)
        monkeypatch.setattr(sdk, 'platform_key', lambda: 'linux-x86_64')
        install_dir = tmp_path / 'install'
        sibling_dirs = [pathlib.Path(str(install_dir) + suffix)
                        for suffix in ('.partial', '.partial.flatten')]
        for sibling_dir in sibling_dirs:
            sibling_dir.mkdir()
            (sibling_dir / 'preserve-me').write_text('keep')
        assert sdk.main(['install', '--tag', tag, '--install-dir', str(install_dir)]) == 0
        output = capsys.readouterr().out
        assert f'Project Manager: {install_dir}/bin/Linux/profile/Default/o3de' in output
        assert f'"{install_dir}/scripts/o3de.sh" create-project' in output
        assert (install_dir / 'scripts/registered').read_text() == 'registered'
        assert not (home / 'O3DE/Downloads' / f'aio3de-sdk-{tag}').exists()
        assert all((directory / 'preserve-me').read_text() == 'keep' for directory in sibling_dirs)
        monkeypatch.setattr(sdk, '_register_engine', lambda _dir, _log: False)
        assert sdk.install(tag, install_dir, force=True, log=lambda _m: None) is False
        assert install_dir.exists()
        assert (home / 'O3DE/Downloads' / f'aio3de-sdk-{tag}').exists()
        assert all((directory / 'preserve-me').read_text() == 'keep' for directory in sibling_dirs)
    finally:
        server.shutdown()
