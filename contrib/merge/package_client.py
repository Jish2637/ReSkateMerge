"""Package a clean, tested ReSkateMerge Release build; never include game data."""
import hashlib
import json
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'contrib'))
from package_release_zip import package

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()

def main():
    if git('status', '--porcelain'):
        raise SystemExit('Commit source changes before packaging.')
    build = ROOT / 'build/vs2026-x64'
    version_match = re.search(r'^DINGOSDK_VERSION:STRING=([0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?)$',
                              (build / 'CMakeCache.txt').read_text(), re.MULTILINE)
    if not version_match:
        raise SystemExit('No valid configured release version found.')
    version = version_match.group(1)
    tests = ET.parse(build / 'test-results.xml').getroot()
    if int(tests.attrib.get('failures', '0')) or int(tests.attrib.get('errors', '0')):
        raise SystemExit('CTest reported failures.')
    if int(tests.attrib.get('tests', '0')) < 15:
        raise SystemExit('Expected detection, terminology and first-person regression suites.')
    names = {case.attrib['name'] for case in tests.iter('testcase')}
    required = {'launcher_updater', 'trick_terminology', 'skate_rules', 'trick_logger', 'throwdown_relay', 'first_person_camera_host', 'playstation_report', 'hid_metadata_reentrancy', 'first_person_native_buttons', 'first_person_native_stick', 'first_person_shared_settings'}
    if not required.issubset(names):
        raise SystemExit('Missing required feature regressions: '+str(required-names))
    for case in tests.iter('testcase'):
        if case.attrib['name'] in required and case.find('skipped') is not None:
            raise SystemExit('Required feature regression was skipped: '+case.attrib['name'])
    update_config = (build / 'generated/launcher_update_config.h').read_text()
    if 'launcher_binary_updates = true' not in update_config or 'L"Jish2637/ReSkateMerge"' not in update_config:
        raise SystemExit('Expected updates enabled exclusively from Jish2637/ReSkateMerge.')
    sha = git('rev-parse', 'HEAD')
    if sha[:12] not in (build/'generated/crash_report_config.h').read_text():
        raise SystemExit('Reconfigure and rebuild the final committed source before packaging.')
    output = ROOT / 'artifacts'
    stage = output / ('client-v' + version)
    stage.mkdir(parents=True, exist_ok=False)
    hashes = {}
    for name in ('ReSkateLauncher.exe', 'ReSkate.dll'):
        source = build / 'Release' / name
        data = source.read_bytes()
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[:2] != b'MZ' or data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x8664:
            raise SystemExit('Expected Windows x64 binary: '+name)
        shutil.copy2(source, stage / name)
        hashes[name] = hashlib.sha256(data).hexdigest()
    presets = stage / 'scripts/Custom/FirstPersonNative/presets/bundled'
    presets.mkdir(parents=True)
    shutil.copy2(build / 'Release/scripts/Custom/FirstPersonNative/presets/bundled/recommended.json', presets)
    for source, name in ((ROOT/'README.md','README.md'),(ROOT/'LICENSE','LICENSE'),
                         (ROOT/'contrib/merge/PROVENANCE.md','contrib/merge/PROVENANCE.md'),
                         (ROOT/'contrib/merge/CONTROLLER_FIX.md','contrib/merge/CONTROLLER_FIX.md'),
                         (ROOT/'contrib/merge/VALIDATION.md','contrib/merge/VALIDATION.md')):
        (stage/name).parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source, stage/name)
    # Retain the vendored dependency notices along with distributed binaries.
    for source in (ROOT/'External').rglob('*'):
        if source.is_file() and source.name.lower().startswith(('license','copying','notice')):
            target=stage/'licenses'/source.relative_to(ROOT/'External')
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,target)
    metadata={'version':version,'source_commit':sha,
              'source_repository':'https://github.com/Jish2637/ReSkateMerge',
              'architecture':'Windows x64','automatic_binary_updates':True,
              'update_repository':'Jish2637/ReSkateMerge',
              'binaries_sha256':hashes,'ctest_tests':int(tests.attrib['tests']),
              'ctest_failures':0,'ctest_skipped':int(tests.attrib.get('skipped','0')),
              'live_game_validation':False}
    (stage/'BUILD_INFO.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    manifest=json.loads((ROOT/'contrib/merge/launcher-base.json').read_text())
    supported=(ROOT/'Engine/Game/Build/supported_build.h').read_text()
    for value in (manifest['game']['skate_sha256'], manifest['game']['manifest_id'], manifest['game']['build_id']):
        if value not in supported:
            raise SystemExit('Manifest game pins disagree with the native supported build.')
    for key,name in (('launcher','ReSkateLauncher.exe'),('runtime','ReSkate.dll')):
        manifest[key]={'version':version,'url':'asset:'+name,'sha256':hashes[name],'size':(stage/name).stat().st_size}
    manifest_path=output/f'launcher-v{version}.json'
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    name=f'ReSkateMerge-v{version}-Windows-x64.zip'
    archive=output/name
    count=package(stage,archive)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    sums=f'{digest}  {name}\n'
    sums+=''.join(f'{hashes[binary]}  {binary}\n' for binary in hashes)
    sums+=f'{hashlib.sha256(manifest_path.read_bytes()).hexdigest()}  launcher.json\n'
    (output/f'SHA256SUMS-v{version}.txt').write_text(sums,encoding='ascii')
    print(f'Packaged {count} files: {archive}\nSource: {metadata["source_commit"]}\nSHA256: {digest}')

if __name__=='__main__':
    main()
