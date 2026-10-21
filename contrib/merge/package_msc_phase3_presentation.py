"""Export the verified private MSC client/server pair and reproducible source changes."""
import argparse
import difflib
import hashlib
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--server-zip',type=Path,default=Path(r'G:\CodexCustom\MSC Reskate Server\Release\MSC-Phase1-Aliases\ReSkateServer-2.1.1-MSC-Phase1-test2-source.zip'))
    args=parser.parse_args()
    server=ROOT/'build/msc-phase2-server-source'
    output=ROOT/'artifacts/msc-phase3-test3-presentation-x64'
    reports=[(ROOT/'build/vs2026-x64/msc-presentation-tests.xml',19),(server/'build-phase2/msc-presentation-tests.xml',5)]
    for report,minimum in reports:
        suites=ET.parse(report).getroot()
        if int(suites.attrib.get('failures','0')) or int(suites.attrib.get('errors','0')) or int(suites.attrib.get('tests','0'))<minimum:
            raise SystemExit('Required MSC regression report did not pass: '+str(report))
        if any(case.find('skipped') is not None for case in suites.iter('testcase')):
            raise SystemExit('Required regression was skipped: '+str(report))
    binaries={'client/ReSkate.dll':ROOT/'build/vs2026-x64/Release/ReSkate.dll','server/ReSkateServer.exe':server/'build-phase2/Release/ReSkateServer.exe'}
    for name,source in binaries.items():
        data=source.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
        if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,pe+4)[0]!=0x8664:
            raise SystemExit('Expected Windows x64 binary: '+str(source))
        if b'msc-ui hello 1' not in data:raise SystemExit('MSC capability marker missing: '+str(source))
        dst=output/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dst)
    shutil.copy2(ROOT/'contrib/merge/MSC_PHASE3_PRESENTATION.md',output/'INSTALL-AND-TEST.md')
    verification=output/'verification';verification.mkdir(exist_ok=True)
    for i,(report,_) in enumerate(reports):shutil.copy2(report,verification/('client-tests.xml' if i==0 else 'server-tests.xml'))
    new_names=['Engine/Game/Multiplayer/msc_interface.h','Engine/Game/Multiplayer/msc_actions.h','Extension/Multiplayer/Test/msc_interface_cases.h','Server/msc_interface.cpp','Server/Test/msc_interface_host_cases.inc','Engine/Game/Multiplayer/msc_staff.h','Engine/Game/Multiplayer/msc_features.h','Server/msc_features.cpp','Server/Test/msc_features_host_cases.inc']
    patches=[]
    with zipfile.ZipFile(args.server_zip) as original,zipfile.ZipFile(output/'MSC-Phase3-Test3-Server-Source.zip','w',zipfile.ZIP_DEFLATED) as modified:
        names=set()
        for entry in original.infolist():
            if entry.is_dir():continue
            names.add(entry.filename);before=original.read(entry.filename);after=(server/entry.filename).read_bytes();modified.writestr(entry.filename,after)
            if before!=after:
                patches.extend(difflib.unified_diff(before.decode('utf-8',errors='replace').replace('\r\n','\n').splitlines(True),after.decode('utf-8',errors='replace').replace('\r\n','\n').splitlines(True),fromfile='a/'+entry.filename,tofile='b/'+entry.filename))
        for name in new_names:
            if name in names:continue
            data=(server/name).read_bytes();modified.writestr(name,data)
            patches.extend(difflib.unified_diff([],data.decode('utf-8').splitlines(True),fromfile='/dev/null',tofile='b/'+name))
    (output/'server-source.patch').write_text(''.join(patches),encoding='utf-8',newline='\n')
    patch=subprocess.check_output(['git','diff','--binary','HEAD'],cwd=ROOT).decode('utf-8')
    for name in subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=ROOT,text=True).splitlines():
        patch+=''.join(difflib.unified_diff([], (ROOT/name).read_text(encoding='utf-8').splitlines(True),fromfile='/dev/null',tofile='b/'+name))
    (output/'client-source.patch').write_text(patch,encoding='utf-8',newline='\n')
    base=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    (output/'SOURCE-BASES.txt').write_text('Client base: '+base+' plus client-source.patch\nServer base ZIP SHA-256: '+hashlib.sha256(args.server_zip.read_bytes()).hexdigest()+'\nServer base: '+str(args.server_zip)+'\n',encoding='utf-8')
    files=sorted(p for p in output.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt')
    (output/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(output).as_posix()+'\n' for p in files),encoding='ascii')
    print(output)
    for name in binaries:print(name,hashlib.sha256((output/name).read_bytes()).hexdigest())
if __name__=='__main__':main()
