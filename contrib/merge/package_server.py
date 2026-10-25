"""Package a tested server-only update while retaining the current client release pins."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
REPO = 'Jish2637/ReSkateMerge'

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def archive(folder, target):
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                item = zipfile.ZipInfo(path.relative_to(folder).as_posix(), (1980, 1, 1, 0, 0, 0))
                item.compress_type = zipfile.ZIP_DEFLATED
                item.create_system = 3
                item.external_attr = 0o100644 << 16
                out.writestr(item, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build/msc-hub-server-release')
    options = parser.parse_args()
    if git('status', '--porcelain'):
        raise SystemExit('Commit the complete release source before packaging.')
    build = ROOT / options.build
    cache = (build / 'CMakeCache.txt').read_text()
    version = re.search(r'^DINGOSDK_VERSION:STRING=(\d+\.\d+\.\d+)$', cache, re.M).group(1)
    commit = git('rev-parse', 'HEAD')
    updater = (build / 'generated/launcher_update_config.h').read_text()
    assert 'launcher_binary_updates = true' in updater and 'L"' + REPO + '"' in updater
    assert commit[:12] in (build / 'generated/crash_report_config.h').read_text(), 'Rebuild the final committed source'
    results = ET.parse(build / 'server-release-tests.xml').getroot()
    assert int(results.attrib['failures']) == 0 and int(results.attrib.get('skipped', 0)) == 0
    expected = {'msc_protocol55', 'msc_player_stats', 'server_config', 'msc_hub_server', 'launcher_updater'}
    assert expected <= {case.attrib['name'] for case in results.findall('testcase')}, 'Run the required release suites'
    binary = build / 'Release/ReSkateServer.exe'
    data = binary.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', data, pe+4)[0] == 0x8664
    output = ROOT / 'artifacts' / ('server-release-v' + version)
    output.mkdir(parents=True, exist_ok=True)
    stage = output / 'server'
    stage.mkdir(exist_ok=True)
    shutil.copy2(binary, stage / 'ReSkateServer.exe')
    shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
    shutil.copy2(ROOT / 'contrib/merge/MSC_HUB_SERVER_RELEASE.md', stage / 'INSTALL-AND-ROLLBACK.md')
    shutil.copy2(build / 'server-release-tests.xml', stage / 'TEST_RESULTS.xml')
    for path in (ROOT / 'External').rglob('*'):
        if path.is_file() and path.name.lower().startswith(('license', 'copying', 'notice')):
            dest = stage / 'licenses' / path.relative_to(ROOT / 'External')
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
    metadata = dict(version=version, source_commit=commit, source_repository='https://github.com/' + REPO,
                    architecture='Windows x64', automatic_binary_updates=True, update_repository=REPO,
                    binaries_sha256={'ReSkateServer.exe': sha(binary)},
                    ctest_tests=int(results.attrib['tests']), ctest_failures=0,
                    live_production_update='not yet verified',
                    broader_server_parks='separate poll-chat assertion failure; not a passing suite')
    (stage / 'BUILD_INFO.json').write_text(json.dumps(metadata, indent=2) + '\n')
    target = output / f'ReSkateServer-v{version}-Windows-x64.zip'
    archive(stage, target)
    shutil.copy2(binary, output / f'ReSkateServer-v{version}.exe')
    shutil.copy2(ROOT / 'contrib/merge/MSC_HUB_SERVER_RELEASE.md', output / f'ReSkateServer-v{version}-ReleaseNotes.md')
    source = output / f'ReSkateServer-v{version}-Source.zip'
    subprocess.run(['git', '-C', str(ROOT), 'archive', '--format=zip', f'--prefix=ReSkateServer-v{version}/', '--output=' + str(source), commit], check=True)
    with zipfile.ZipFile(source) as contents:
        assert contents.comment.decode() == commit and contents.testzip() is None
    previous = json.loads((ROOT / 'contrib/merge/server-release-base.json').read_text())
    manifest = json.loads(json.dumps(previous))
    manifest['server'] = dict(version=version, url='asset:' + target.name, sha256=sha(target), size=target.stat().st_size, exe_sha256=sha(binary))
    assert all(manifest[k] == previous[k] for k in previous if k != 'server')
    pending = output / f'launcher-server-v{version}.pending.json'
    pending.write_text(json.dumps(manifest, indent=2) + '\n')
    subprocess.run([str(build / 'Release/dingosdk_launcher_updater_tests.exe'), '--server-manifest', str(pending), version], check=True)
    subprocess.run([str(build / 'Release/dingosdk_launcher_updater_tests.exe'), '--server-archive', str(target), str(build / 'server-archive-fixture')], check=True)
    assets = [target, source, output / f'ReSkateServer-v{version}.exe', output / f'ReSkateServer-v{version}-ReleaseNotes.md', pending]
    (output / 'UPLOADS.json').write_text(json.dumps({path.name: {'sha256': sha(path), 'size': path.stat().st_size} for path in assets}, indent=2) + '\n')
    print('Prepared server', version, 'from', commit)
    print('Client pins unchanged:', manifest['launcher']['version'], manifest['runtime']['version'])
    print('Assets:', output)

if __name__ == '__main__':
    main()
