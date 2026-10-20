"""Package a clean, tested ReSkateMerge Release build; never include game data."""
import hashlib
import json
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
    tests = ET.parse(build / 'test-results.xml').getroot()
    if int(tests.attrib.get('failures', '0')) or int(tests.attrib.get('errors', '0')):
        raise SystemExit('CTest reported failures.')
    if int(tests.attrib.get('tests', '0')) < 15:
        raise SystemExit('Expected detection, terminology and first-person regression suites.')
    names = {case.attrib['name'] for case in tests.iter('testcase')}
    required = {'trick_terminology', 'skate_rules', 'trick_logger', 'first_person_camera_host'}
    if not required.issubset(names):
        raise SystemExit('Missing required feature regressions: '+str(required-names))
    for case in tests.iter('testcase'):
        if case.attrib['name'] in required and case.find('skipped') is not None:
            raise SystemExit('Required feature regression was skipped: '+case.attrib['name'])
    update_config = (build / 'generated/launcher_update_config.h').read_text()
    if 'launcher_binary_updates = false' not in update_config:
        raise SystemExit('Custom launcher update protection is not enabled.')
    output = ROOT / 'artifacts'
    stage = output / 'client-v0.1.0-merged.1'
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
                         (ROOT/'contrib/merge/PROVENANCE.md','PROVENANCE.md'),
                         (ROOT/'contrib/merge/VALIDATION.md','VALIDATION.md')):
        shutil.copy2(source, stage/name)
    # Retain the vendored dependency notices along with distributed binaries.
    for source in (ROOT/'External').rglob('*'):
        if source.is_file() and source.name.lower().startswith(('license','copying','notice')):
            target=stage/'licenses'/source.relative_to(ROOT/'External')
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,target)
    metadata={'version':'0.1.0-merged.1','source_commit':git('rev-parse','HEAD'),
              'source_repository':'https://github.com/Jish2637/ReSkateMerge',
              'architecture':'Windows x64','automatic_binary_updates':False,
              'binaries_sha256':hashes,'ctest_tests':int(tests.attrib['tests']),
              'ctest_failures':0,'ctest_skipped':int(tests.attrib.get('skipped','0')),
              'live_game_validation':False}
    (stage/'BUILD_INFO.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    name='ReSkateMerge-v0.1.0-merged.1-Windows-x64.zip'
    archive=output/name
    count=package(stage,archive)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    (output/'SHA256SUMS.txt').write_text(f'{digest}  {name}\n',encoding='ascii')
    print(f'Packaged {count} files: {archive}\nSource: {metadata["source_commit"]}\nSHA256: {digest}')

if __name__=='__main__':
    main()
