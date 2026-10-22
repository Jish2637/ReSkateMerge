"""Prepare verified GitHub release assets, then publish the authorized public channel.

Uses the current Git credential helper. Never writes or prints credentials.
Commands: prepare VERSION; make-public VERSION; publish VERSION.
"""
import hashlib
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[2]
REPO='Jish2637/ReSkateMerge'

def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()

def main():
    mode,version=sys.argv[1:3]
    if mode not in ('prepare','make-public','publish'):
        raise SystemExit('Expected prepare, make-public, or publish')
    if git('status','--porcelain'):
        raise SystemExit('Commit source changes first.')
    sha=git('rev-parse','HEAD')
    output=ROOT/'artifacts'
    tag='v'+version
    stage=output/('client-'+tag)
    metadata=json.loads((stage/'BUILD_INFO.json').read_text())
    assert metadata['source_commit']==sha and metadata['version']==version
    assert metadata['automatic_binary_updates'] and metadata['update_repository']==REPO
    manifest_path=output/('launcher-'+tag+'.json')
    manifest=json.loads(manifest_path.read_text())
    assets={f'ReSkateMerge-{tag}-Windows-x64.zip':output/f'ReSkateMerge-{tag}-Windows-x64.zip',
            f'ReSkateMerge-{tag}-Source.zip':output/f'ReSkateMerge-{tag}-Source.zip',
            'SHA256SUMS.txt':output/f'SHA256SUMS-{tag}.txt',
            'launcher.json':manifest_path,
            'ReSkateLauncher.exe':stage/'ReSkateLauncher.exe','ReSkate.dll':stage/'ReSkate.dll'}
    if not metadata.get('client_only',False):
        server_version=manifest['server']['version']
        server_stage=output/('server-v'+server_version)
        server_archive=output/f'ReSkateServer-v{server_version}-Windows-x64.zip'
        assets[server_archive.name]=server_archive;assets['ReSkateServer.exe']=server_stage/'ReSkateServer.exe'
        with zipfile.ZipFile(server_archive) as archive:
            assert archive.read('ReSkateServer.exe')==assets['ReSkateServer.exe'].read_bytes()
            assert json.loads(archive.read('BUILD_INFO.json'))['source_commit']==sha
            assert manifest['server']=={'version':server_version,'url':'asset:'+server_archive.name,'sha256':hashlib.sha256(server_archive.read_bytes()).hexdigest(),'size':server_archive.stat().st_size,'exe_sha256':hashlib.sha256(assets['ReSkateServer.exe'].read_bytes()).hexdigest()}
    with zipfile.ZipFile(next(path for name,path in assets.items() if name.endswith('.zip'))) as archive:
        assert json.loads(archive.read('BUILD_INFO.json'))==metadata
        for key,name in (('launcher','ReSkateLauncher.exe'),('runtime','ReSkate.dll')):
            data=assets[name].read_bytes()
            assert archive.read(name)==data
            assert manifest[key]=={'version':version,'url':'asset:'+name,'sha256':hashlib.sha256(data).hexdigest(),'size':len(data)}
    sums={line.split()[1]:line.split()[0] for line in assets['SHA256SUMS.txt'].read_text().splitlines()}
    for name,path in assets.items():
        if name!='SHA256SUMS.txt':
            assert sums[name]==hashlib.sha256(path.read_bytes()).hexdigest()

    env=dict(os.environ,GCM_INTERACTIVE='never',GIT_TERMINAL_PROMPT='0')
    credential=subprocess.run(['git','credential','fill'],input='protocol=https\nhost=github.com\n\n',text=True,capture_output=True,env=env,check=True)
    token=dict(line.split('=',1) for line in credential.stdout.splitlines() if '=' in line)['password']
    tls=ssl.create_default_context()
    if hasattr(ssl,'enum_certificates'):
        for store in ('ROOT','CA'):
            for cert,encoding,trust in ssl.enum_certificates(store):
                if encoding=='x509_asn' and (trust is True or ssl.Purpose.SERVER_AUTH.oid in trust):
                    tls.load_verify_locations(cadata=ssl.DER_cert_to_PEM_cert(cert))
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self,*args,**kwargs):
            return None
    opener=urllib.request.build_opener(NoRedirect,urllib.request.HTTPSHandler(context=tls))
    def request(path,body=None,method=None,binary=False):
        url=path if path.startswith('https://') else 'https://api.github.com'+path
        assert urllib.parse.urlsplit(url).hostname in ('api.github.com','uploads.github.com')
        data=body if binary else json.dumps(body).encode() if body is not None else None
        req=urllib.request.Request(url,data=data,method=method,headers={
            'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json',
            'User-Agent':'ReSkateMerge-release','Content-Type':'application/octet-stream' if binary else 'application/json'})
        try:
            with opener.open(req,timeout=120) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError(f'GitHub request failed: HTTP {error.code}') from None
    base='/repos/'+REPO
    repository=request(base)
    assert repository['default_branch']=='main' and request(base+'/commits/main')['sha']==sha
    if mode=='prepare':
        release=request(base+'/releases',{'tag_name':tag,'target_commitish':sha,'name':'ReSkateMerge '+tag,
            'body':(output/('RELEASE_NOTES-'+tag+'.md')).read_text(encoding='utf-8'),'draft':True,'prerelease':False})
        for name,path in assets.items():
            data=path.read_bytes()
            uploaded=request(release['upload_url'].split('{')[0]+'?name='+urllib.parse.quote(name),data,binary=True)
            assert uploaded['size']==len(data)
            assert uploaded.get('digest')=='sha256:'+hashlib.sha256(data).hexdigest()
            print('Verified uploaded asset:',name)
        (output/('prepared-release-'+tag+'.json')).write_text(json.dumps(release,indent=2))
        print('Prepared draft:',release['html_url'])
        return
    prepared=json.loads((output/('prepared-release-'+tag+'.json')).read_text())
    release=request(base+'/releases/'+str(prepared['id']))
    remote={asset['name']:asset for asset in release['assets']}
    for name,path in assets.items():
        assert remote[name]['size']==path.stat().st_size
        assert remote[name].get('digest')=='sha256:'+hashlib.sha256(path.read_bytes()).hexdigest()
    if mode=='make-public':
        audit=json.loads((output/'publication-audit.json').read_text())
        assert audit['source_commit']==sha and audit['confirmed_credentials']==0 and audit['unreviewed_findings']==0
        repository=request(base,{'private':False},method='PATCH')
        assert not repository['private']
        print('Repository is public:',repository['html_url'])
    else:
        assert not repository['private']
        release=request(base+'/releases/'+str(release['id']),{'draft':False,'prerelease':False,'make_latest':'true'},method='PATCH')
        assert not release['draft'] and not release['prerelease']
        assert request(base+'/commits/'+tag)['sha']==sha
        (output/('release-result-'+tag+'.json')).write_text(json.dumps(release,indent=2))
        print('Published public release:',release['html_url'])

if __name__=='__main__':
    main()
