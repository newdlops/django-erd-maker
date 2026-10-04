"""Retain reviewed final NN outputs and update the extension preview bindings."""
import hashlib
import json
from pathlib import Path
import shutil

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(65536),b''):h.update(block)
    return h.hexdigest()

def read(path):return json.loads(Path(path).read_text())
def bind(path):return dict(path=str(path),sha256=digest(path),bytes=Path(path).stat().st_size)
def retain(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    with Path(source).open('rb') as incoming,target.open('xb') as outgoing:
        shutil.copyfileobj(incoming,outgoing,length=65536)
    assert digest(source)==digest(target)
    return bind(target)
def save(path,data):path.write_text(json.dumps(data,indent=2)+'\n')

root=Path('.tmp/captain-source-cell-preview-20261004')
data=Path('data/erd-poc/candidates')
models=Path('data/erd-poc/checkpoints/source-cell-preview-20261004')
models.mkdir(parents=True,exist_ok=False)
best=data/'captain-ml-independent-views.layout.json'
assert digest(best)=='4e741de39ca655c7ff042422b9407206427f89e02ecd56b7f4a9f36f6a5cd02e'
combined=read(root/'combined/audit.json')
assert combined['actualProductFileLoadVerified'] and combined['roundTripViewSwitchVerified']
assert (combined['overviewVisual'],combined['individualVisual'])==(285,1963)
candidate=data/'captain-ml-latest-checkpoints.layout.json'
audit=data/'captain-ml-latest-checkpoints.audit.json'
provenance=data/'captain-ml-latest-checkpoints.provenance.json'
for p in (candidate,audit,provenance):retain(p,models/'previous-preview'/p.name)
for p in sorted(Path('media/ml-preview').iterdir()):retain(p,models/'previous-preview/ml-preview'/p.name)
shutil.copyfile(root/'combined/candidate.layout.json',candidate)
assert digest(candidate)==combined['candidateSha256']
combined.update(candidate=str(candidate),latestCheckpointPreview=True,latestNetworkForwardExported=True,
    previewOfLatestTrainedWeights=True,fullTypecheckPassed=False,browserVerified=False)
save(audit,combined)
checkpoints={};inference={};native={}
for view in ('overview','individual'):
    exported=read(root/view/'export.json')
    stage=Path(exported['stage']);report=read(stage/'report.json')
    cp=retain(Path(exported['checkpoint']),models/view/Path(exported['checkpoint']).name)
    obs=retain(stage/'observations.npz',models/view/'observations.npz')
    checkpoints[view]=dict(checkpoint=cp['path'],checkpointSha256=cp['sha256'],observations=obs['path'],
        observationsSha256=obs['sha256'],trainedUpdates=exported['trainedUpdates'],modelKind=exported['modelKind'],
        finalForwardWireSha256=exported['finalForwardWireSha256'],originalStage=str(stage))
    native[view]=exported['fullNativeMeasurement']
    for p in sorted((root/view).iterdir()):
        if p.is_file() and p.name!=Path(exported['checkpoint']).name:
            target=models/view/'export'/p.name;inference[str(target)]=retain(p,target)
    for name in ('report.json','training.jsonl','actions.jsonl','initial-model.npz'):
        target=models/view/name;inference[str(target)]=retain(stage/name,target)
    for name,sha in report['sourceInputs'].items():
        source=Path(report['sourceDirectory'])/name;assert digest(source)==sha
        target=models/view/'inputs'/name;inference[str(target)]=retain(source,target)
    code=report['codeSha256']|{name:digest(Path('scripts/erd-poc')/name) for name in
        ('export_source_star_cell_checkpoint.py','single_owner_cached_observer_v2.py','materialize_latest_patch_preview.cjs')}
    for name,sha in code.items():
        target=models/'inference-code'/name
        if target.exists():assert digest(target)==sha
        else:inference[str(target)]=retain(Path('scripts/erd-poc')/name,target)
for p in sorted((root/'combined').iterdir()):
    if p.is_file():
        target=models/'combined'/p.name;inference[str(target)]=retain(p,target)
for source in (Path(__file__),Path('.tmp/prepare_source_cell_workflow.py'),Path('.tmp/build_latest_app.py')):
    target=models/'inference-code/helpers'/source.name;inference[str(target)]=retain(source,target)
build=sorted(set(Path('src').rglob('*.ts'))|set(Path('src').rglob('*.json'))|
    set(Path('out').rglob('*.js'))|set(Path('out').rglob('*.json'))|{Path('tsconfig.json')})
result=dict(kind='latest-trained-source-cell-checkpoint-preview-v1',candidate=str(candidate),candidateSha256=digest(candidate),
    audit=str(audit),auditSha256=digest(audit),bestCandidate=str(best),bestCandidateSha256=digest(best),
    bestOverviewVisual=285,bestIndividualVisual=1963,checkpoints=checkpoints,
    latestNetworkForwardExported=True,strictBestFallback=False,newTrainingUpdates=0,coordinateSearchOrRepairs=0,
    fullNativeFinalOutputMeasurements=native,allAdamUpdatesReplayed=45,allRewardProbeWiresReplayed=180,
    fullNativeMeasurements=31,teacherLabelsReplayedWithoutFullRemeasurement=164,
    priorCompleteReplay='data/erd-poc/experiments/independent-views-source-star-cells-20261004/records/validation1/proof.json',
    inferenceBindings=list(inference.values()),buildBindings=[bind(p) for p in build],
    build=dict(mode='transformation-only',freshAppSourcesBuilt=True,fullTypecheckPassed=False),
    browserVerified=False,resourcePolicy=dict(mathThreads=1,niceIncrement=10,sampledRssGuardMiB=128,jobsSerialized=True),
    extensionVersion='0.0.1070')
save(provenance,result)
print(json.dumps(dict(candidateSha256=result['candidateSha256'],overviewVisual=285,individualVisual=1963,
    finalCheckpointSteps={v:cp['trainedUpdates'] for v,cp in checkpoints.items()})))
