"""Retain the completed source-ray experiment and its independent replay."""
import hashlib
import json
from pathlib import Path
import shutil

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(65536),b''):h.update(block)
    return h.hexdigest()

root=Path('.tmp/visualcross-ml-150-750-20261004')
family=root/'source-ray-clip1'
target=Path('data/erd-poc/experiments/independent-views-source-ray-clips-20261004')
target.mkdir(parents=True,exist_ok=False)
proof=json.loads((family/'release-validation1/proof.json').read_text())
assert proof['status']=='pass' and proof['allTeacherLabelsIndependentlyRemeasured']
pairs=[(p,Path('records')/p.relative_to(family)) for p in sorted(family.rglob('*')) if p.is_file()]
diagnostic=root/'source-ray-containment1'
pairs.extend((p,Path('diagnostics')/p.relative_to(diagnostic)) for p in sorted(diagnostic.rglob('*')) if p.is_file())
scripts=set()
for view in ('overview','individual'):
    report=json.loads((family/(view+'-learning1')/'report.json').read_text())
    for name,sha in report['codeSha256'].items():
        p=Path('scripts/erd-poc')/name
        assert digest(p)==sha
        scripts.add(p)
    for name,sha in report['sourceInputs'].items():
        p=Path(report['sourceDirectory'])/name
        assert digest(p)==sha
        pairs.append((p,Path('source-inputs')/view/name))
scripts.update(Path('.tmp')/name for name in ('inspect_source_ray_containment.py','test_source_ray_clip_policy.py',
    'test_source_ray_gain_model.py','verify_source_ray_release.py','retain_source_ray_release.py'))
scripts.add(Path('scripts/erd-poc/run_memory_bounded.py'))
for p in sorted(scripts):
    relative=Path('code/helpers')/p.name if p.parts[0]=='.tmp' else Path('code/repository')/p
    pairs.append((p,relative))
binding=root/'geometry-world1/source-binding.json'
pairs.append((binding,Path('source-binding.json')))
environment=Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
pairs.append((environment,Path('environment')))
files=[]
for source,relative in pairs:
    destination=target/relative
    destination.parent.mkdir(parents=True,exist_ok=True)
    sha=digest(source)
    with source.open('rb') as incoming,destination.open('xb') as outgoing:
        shutil.copyfileobj(incoming,outgoing,length=65536)
    if source==environment:destination.chmod(0o755)
    assert digest(destination)==sha
    files.append(dict(source=str(source),retained=str(relative),sha256=sha,bytes=destination.stat().st_size))
canonical=Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
assert digest(canonical)==json.loads(binding.read_text())['promotedCandidateSha256']
manifest=dict(kind='retained-source-ray-clipped-neural-controls-v1',
    actualGraphTeacherControls=65,actualGraphLegalControls=65,actualGraphBetterControls=0,
    trainedGainSelectorUpdates=0,accepted=0,toyVisualBefore=2,toyVisualAfter=1,
    validation=proof,canonicalBest=dict(overview=285,individual=1963,sha256=digest(canonical)),
    targets=dict(overview=150,individual=750),targetsMet=False,browserVerified=False,
    nativeCoordinateSearchOrRepairs=0,resourcePolicy=dict(sampledRssGuardMiB=128,mathThreads=1,
    niceIncrement=10,jobsSerialized=True,maximumCompletedReplayPeakMiB=53.3),
    retainedFiles=len(files),retainedBytes=sum(row['bytes'] for row in files),files=files)
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({key:manifest[key] for key in ('kind','retainedFiles','retainedBytes','actualGraphTeacherControls','targetsMet')}))
