"""Install this verified local VSIX with bounded buffers and an atomic registry update."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import time
import zipfile

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(65536),b''):h.update(chunk)
    return h.hexdigest()

parser=argparse.ArgumentParser()
parser.add_argument('--apply',action='store_true')
args=parser.parse_args()
repo=Path(__file__).resolve().parents[4]
release=Path(__file__).resolve().parent
verified=json.loads((repo/'dist/verification.json').read_text())
archive=repo/'dist'/verified['vsix']
assert verified['vsix']=='django-erd-maker-0.0.1070-darwin-arm64.vsix'
assert digest(archive)==verified['sha256']
extension_id='newdlops.django-erd-maker'
extensions=Path('/Users/lky/.vscode/extensions')
registry=extensions/'extensions.json'
before=registry.read_bytes()
entries=json.loads(before)
matches=[i for i,row in enumerate(entries) if row['identifier']['id'].lower()==extension_id]
assert len(matches)==1
index=matches[0]
assert entries[index]['version']=='0.0.1069'
destination=extensions/(extension_id+'-0.0.1070')
assert not destination.exists()
with zipfile.ZipFile(archive) as package:
    names=package.namelist()
    assert len(names)==len(set(names))
    for item in package.infolist():
        parts=PurePosixPath(item.filename).parts
        assert not item.filename.startswith('/') and '..' not in parts and '\\' not in item.filename
        assert not stat.S_ISLNK(item.external_attr>>16)
        assert item.filename.startswith('extension/') or item.filename in ('extension.vsixmanifest','[Content_Types].xml')
    manifest=json.loads(package.read('extension/package.json'))
    assert manifest['publisher']+'.'+manifest['name']==extension_id
    assert manifest['version']=='0.0.1070'
    proof=dict(version=manifest['version'],vsix=str(archive.relative_to(repo)),vsixSha256=verified['sha256'],
        previousInstalledVersion=entries[index]['version'],extensionRoot=str(destination),
        packageFiles=len(names),registryEntryCount=len(entries),installMethod='verified-streaming-vsix-atomic-registry')
    if not args.apply:
        print(json.dumps(dict(status='ready',**proof)))
        raise SystemExit(0)
    staging=Path(tempfile.mkdtemp(prefix='.'+extension_id+'-0.0.1070-',dir=extensions))
    for item in package.infolist():
        if not item.filename.startswith('extension/') or item.is_dir():continue
        relative=PurePosixPath(item.filename).relative_to('extension')
        output=staging.joinpath(*relative.parts)
        output.parent.mkdir(parents=True,exist_ok=True)
        with package.open(item) as incoming,output.open('xb') as outgoing:
            shutil.copyfileobj(incoming,outgoing,length=65536)
        mode=(item.external_attr>>16)&0o777
        if mode:output.chmod(mode)
    source_manifest=json.loads((staging/'sources/manifest.json').read_text())
    assert source_manifest['version']==manifest['version']
    for group in ('runtimeFiles','assets','binaries'):
        for name,sha in source_manifest[group].items():assert digest(staging/name)==sha,name
    for name,sha in source_manifest['archives'].items():assert digest(staging/'sources'/name)==sha,name
    metadata=copy.deepcopy(entries[index].get('metadata',{}))
    metadata.update(installedTimestamp=int(time.time()*1000),source='vsix',targetPlatform='darwin-arm64',
        isPreReleaseVersion=True,pinned=True)
    manifest['__metadata']=metadata
    (staging/'package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    original_backup=repo/'.tmp/source-cell-1070-extension-registry-before.json'
    with original_backup.open('xb') as stream:stream.write(before)
    assert registry.read_bytes()==before,'Registry changed during extraction; no registry update was made'
    staging.rename(destination)
    after=copy.deepcopy(entries)
    updated=after[index]
    updated['version']='0.0.1070'
    updated['location']={'$mid':1,'path':str(destination),'scheme':'file'}
    updated['relativeLocation']=destination.name
    updated['metadata']=metadata
    assert all(old==new for i,(old,new) in enumerate(zip(entries,after)) if i!=index)
    assert len(after)==len(entries)
    descriptor,temporary=tempfile.mkstemp(prefix='.'+extension_id+'-registry-',dir=extensions)
    with os.fdopen(descriptor,'wb') as stream:
        stream.write(json.dumps(after,separators=(',',':')).encode())
        stream.flush();os.fsync(stream.fileno())
    os.chmod(temporary,stat.S_IMODE(registry.stat().st_mode))
    assert registry.read_bytes()==before,'Registry changed before activation; previous registration retained'
    os.replace(temporary,registry)
    registered=json.loads(registry.read_text())
    assert registered[index]['version']=='0.0.1070' and registered[index]['location']['path']==str(destination)
    assert (extensions/(extension_id+'-0.0.1069')/'package.json').is_file()
    proof.update(status='pass',installedIntoUserProfile=True,allPackagedHashesMatched=True,
        otherRegistryEntriesUnchanged=True,previousVersionPreserved=True,registryBackup=str(original_backup),
        browserVerified=False,activeWindowReloadVerified=False)
    (release/'registry-installation.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof))
