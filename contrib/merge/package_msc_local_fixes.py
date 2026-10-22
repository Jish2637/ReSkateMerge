"""Package the local-only terminology, leaderboard and FP test pair."""
from pathlib import Path
import difflib,hashlib,json,shutil,struct,subprocess
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
def main():
    report=ROOT/'build/vs2026-x64/msc-local-fixes-tests.xml';suite=ET.parse(report).getroot();cases=list(suite.iter('testcase'))
    required={'trick_terminology','multiplayer_protocol','server_player_stats','server_park_mods','skate_rules','throwdown_relay'}
    if not required.issubset({c.attrib['name'] for c in cases}) or any(c.find(tag) is not None for c in cases for tag in ('failure','error','skipped')):raise SystemExit('Required regressions did not all pass')
    output=ROOT/'artifacts/msc-local-labels-fp-test2-x64';output.mkdir(parents=True,exist_ok=True)
    binaries={'client/ReSkate.dll':ROOT/'build/vs2026-x64/Release/ReSkate.dll','server/ReSkateServer.exe':ROOT/'build/vs2026-server-release/Release/ReSkateServer.exe'}
    for name,source in binaries.items():
        data=source.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
        if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,pe+4)[0]!=0x8664:raise SystemExit('Expected Windows x64 binary: '+name)
        target=output/name;target.parent.mkdir(exist_ok=True);shutil.copy2(source,target)
    if b'/jish-fp 1' not in (output/'client/ReSkate.dll').read_bytes():raise SystemExit('Automatic FP reporter missing')
    if b'fp_presence' not in (output/'server/ReSkateServer.exe').read_bytes():raise SystemExit('FP capability missing')
    shutil.copy2(ROOT/'contrib/merge/MSC_LOCAL_FIXES.md',output/'INSTALL-AND-TEST.md')
    shutil.copy2(report,output/'test-results.xml')
    patch=subprocess.check_output(['git','diff','--binary','HEAD'],cwd=ROOT).decode('utf-8')
    for name in subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines():
        patch+=''.join(difflib.unified_diff([], (ROOT/name).read_text(encoding='utf-8').splitlines(True),fromfile='/dev/null',tofile='b/'+name))
    (output/'source-changes.patch').write_text(patch,encoding='utf-8',newline='\n')
    info={'private_test':True,'published':False,'source_base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'source_patch_sha256':hashlib.sha256(patch.encode()).hexdigest(),'requires_no_update':True,'binary_version_stamps':{'client':'0.1.3','server':'2.1.2'},'tests':int(suite.attrib['tests']),'live_game_validation':False}
    (output/'BUILD_INFO.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
    files=sorted(p for p in output.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt')
    (output/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(output).as_posix()+'\n' for p in files),encoding='ascii')
    print(output);print('Passed tests:',info['tests'])
    for name in binaries:print(name,hashlib.sha256((output/name).read_bytes()).hexdigest())
if __name__=='__main__':main()
