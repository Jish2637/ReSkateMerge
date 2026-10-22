#!/usr/bin/env python3
"""Fetch the latest stable MSC client and its matching game metadata."""
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

REPO = 'Jish2637/ReSkateMerge'

def request(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={
        'User-Agent': 'MSC-SteamOS-Installer', 'Accept': 'application/vnd.github+json'
    }), timeout=60)

def select_asset(release):
    assets = [a for a in release['assets'] if re.fullmatch(
        r'ReSkateMerge-.+-Windows-x64\.zip', a['name'], re.I)]
    if len(assets) != 1:
        raise RuntimeError('Expected exactly one MSC Windows x64 client ZIP.')
    return assets[0]

def download(asset, target):
    digest = asset.get('digest', '')
    if not re.fullmatch(r'sha256:[0-9a-f]{64}', digest):
        raise RuntimeError('Release asset has no SHA-256 digest; cannot verify it.')
    with request(asset['browser_download_url']) as src, target.open('wb') as dst:
        shutil.copyfileobj(src, dst)
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest.split(':')[1]:
        raise RuntimeError('Release asset SHA-256 verification failed.')

def install_zip(archive, dest, backups):
    with zipfile.ZipFile(archive) as z:
        entries = []
        seen = set()
        for info in z.infolist():
            p = PurePosixPath(info.filename)
            if p.is_absolute() or '..' in p.parts or '\\' in info.filename or ':' in info.filename:
                raise RuntimeError('Unsafe path in MSC archive: ' + info.filename)
            if info.is_dir():
                continue
            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                raise RuntimeError('Symlinks are not supported in MSC archives.')
            if info.filename.casefold() in seen:
                raise RuntimeError('Duplicate path in MSC archive.')
            seen.add(info.filename.casefold())
            if info.filename in ('ReSkateLauncher.exe','ReSkate.dll','README.md','LICENSE','BUILD_INFO.json') or p.parts[0] in ('scripts','licenses','contrib'):
                entries.append(info)
        for name in ('ReSkateLauncher.exe','ReSkate.dll'):
            if not any(i.filename == name and i.file_size > 0 for i in entries):
                raise RuntimeError('Missing ' + name + ' in MSC client ZIP.')
        # Validate the entire selected set before writing any installed files.
        staged = []
        with tempfile.TemporaryDirectory(prefix='msc-stage-', dir=backups.parent) as td:
            for info in entries:
                target = dest.joinpath(*PurePosixPath(info.filename).parts)
                if not target.resolve().is_relative_to(dest.resolve()):
                    raise RuntimeError('Install path escapes game directory.')
                stage = Path(td).joinpath(*PurePosixPath(info.filename).parts)
                stage.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as src, stage.open('wb') as dst:
                    shutil.copyfileobj(src, dst)
                staged.append((target, stage, info.filename))
            for target, stage, name in staged:
                if target.exists():
                    backup = backups.joinpath(*PurePosixPath(name).parts)
                    backup.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(target, backup)
                target.parent.mkdir(parents=True, exist_ok=True)
                temp = target.with_name(target.name + '.msc-new')
                shutil.copyfile(stage, temp)
                os.replace(temp, target)
                print('Installed:', target)

def main():
    dest, tools = map(Path, sys.argv[1:3])
    with request(f'https://api.github.com/repos/{REPO}/releases/latest') as r:
        release = json.load(r)
    if release.get('draft') or release.get('prerelease'):
        raise RuntimeError('Expected a stable published release.')
    print('Latest MSC release:', release['tag_name'])
    asset = select_asset(release)
    manifests = [a for a in release['assets'] if a['name'] == 'launcher.json']
    if len(manifests) != 1:
        raise RuntimeError('Release must contain launcher.json with game requirements.')
    metadata = tools/'msc-launcher.json'
    download(manifests[0], metadata)
    game = json.loads(metadata.read_text(encoding='utf-8-sig'))['game']
    if str(game['app_id']) != '3354750' or str(game['depot_id']) != '3354751':
        raise RuntimeError('MSC manifest refers to an unexpected game/depot.')
    if not str(game['manifest_id']).isdigit() or not re.fullmatch(r'[0-9a-fA-F]{64}',game['skate_sha256']):
        raise RuntimeError('Invalid game manifest or executable hash.')
    archive = tools/'msc-client.zip'
    download(asset, archive)
    backups = tools/('backup-' + time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    install_zip(archive, dest, backups)
    (tools/'msc-game.json').write_text(json.dumps(game), encoding='utf-8')
    (tools/'msc-release.txt').write_text(release['tag_name'] + '\n', encoding='utf-8')
    print('Verified MSC client archive; replaced-file backups:', backups)

if __name__ == '__main__':
    main()
