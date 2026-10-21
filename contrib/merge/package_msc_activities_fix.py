"""Package the client-only MSC Activities popup repair after UI regressions pass."""
from pathlib import Path
import difflib, hashlib, shutil, struct, subprocess
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
def main():
    output=ROOT/'artifacts/msc-phase3-test2-activities-fix-x64'
    report=ROOT/'build/vs2026-x64/msc-activities-fix-tests.xml'
    suite=ET.parse(report).getroot()
    cases=list(suite.iter('testcase'))
    if int(suite.attrib.get('tests','0'))<19 or any(case.find(tag) is not None for case in cases for tag in ('failure','error','skipped')) or not any(c.attrib.get('name')=='msc_activities_ui' for c in cases):
        raise SystemExit('Required client regressions did not pass')
    binary=ROOT/'build/vs2026-x64/Release/ReSkate.dll';data=binary.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
    if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,pe+4)[0]!=0x8664:raise SystemExit('Expected Windows x64 DLL')
    (output/'client').mkdir(parents=True,exist_ok=True);(output/'verification').mkdir(exist_ok=True)
    shutil.copy2(binary,output/'client/ReSkate.dll')
    shutil.copy2(ROOT/'contrib/merge/MSC_ACTIVITIES_FIX.md',output/'INSTALL-AND-TEST.md')
    shutil.copy2(report,output/'verification/client-tests.xml')
    baseline=ROOT/'build/msc-phase3-activities-baseline-result.log'
    if 'Create/join failed to open stake confirmation in the parent scope' not in baseline.read_text():raise SystemExit('Original-bug reproduction evidence missing')
    shutil.copy2(baseline,output/'verification/expected-original-bug-reproduction.txt')
    patch=subprocess.check_output(['git','diff','--binary','HEAD'],cwd=ROOT).decode('utf-8')
    for name in subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines():
        patch+=''.join(difflib.unified_diff([], (ROOT/name).read_text(encoding='utf-8').splitlines(True),fromfile='/dev/null',tofile='b/'+name))
    (output/'client-source.patch').write_text(patch,encoding='utf-8',newline='\n')
    base=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    (output/'SOURCE-BASE.txt').write_text('Client base: '+base+' plus client-source.patch\nCompatible server: MSC Phase 3 test 1 (unchanged)\n',encoding='utf-8')
    files=sorted(p for p in output.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt')
    (output/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(output).as_posix()+'\n' for p in files),encoding='ascii')
    print(output);print('ReSkate.dll SHA-256:',hashlib.sha256(data).hexdigest())
if __name__=='__main__':main()
