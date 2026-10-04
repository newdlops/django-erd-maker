"""Stream-copy the closed family and verify every retained byte."""
import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(65536),b''):
            h.update(block)
    return h.hexdigest()


root = Path('.tmp/visualcross-ml-150-750-20261004')
family = root/'source-star-cell1'
target = Path('data/erd-poc/experiments/independent-views-source-star-cells-20261004')
target.mkdir(parents=True,exist_ok=False)
proof = json.loads((family/'validation1/proof.json').read_text())
controls = json.loads((family/'controls-validation1/proof.json').read_text())
common = json.loads((family/'common-anchors-validation1/proof.json').read_text())
assert proof['status']==controls['status']==common['status']=='pass'
assert proof['allTeacherLabelsIndependentlyRemeasured']
assert controls['allTeacherLabelsIndependentlyRemeasured']
assert common['allTeacherLabelsIndependentlyRemeasured']
binding_path = root/'geometry-world1/source-binding.json'
binding = json.loads(binding_path.read_text())
canonical = Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
assert digest(canonical)==binding['promotedCandidateSha256']
pairs = []
for source in sorted(family.rglob('*')):
    if source.is_file():
        pairs.append((source,Path('records')/source.relative_to(family)))
for name in ('radial-conflict-capacity1','source-star-cell-capacity1'):
    for source in sorted((root/name).rglob('*')):
        if source.is_file():
            pairs.append((source,Path('diagnostics')/name/source.relative_to(root/name)))
scripts = set()
for path in family.rglob('report.json'):
    report = json.loads(path.read_text())
    for code in report.get('codeSha256',{}):
        candidate = Path(code)
        if not candidate.exists():
            candidate = Path('scripts/erd-poc')/code
        assert candidate.is_file(),code
        scripts.add(candidate.resolve())
scripts.update(Path('scripts/erd-poc')/name for name in ('learn_card_policy.py','learned_global_replay.py',
    'geometry_world_model.py','single_owner_cached_observer_v2.py','run_memory_bounded.py'))
scripts.update(Path('.tmp')/name for name in ('inspect_radial_conflict_capacity.py','inspect_source_star_cells.py',
    'test_source_star_cell_policy.py','verify_source_star_cell_learning.py','run_source_cell_control_learning.py',
    'verify_source_cell_controls.py','probe_free_body_common_anchors.py','verify_free_body_common_anchors.py'))
scripts.add(Path(__file__).resolve())
repo = Path('.').resolve()
for source in sorted(set(path.resolve() for path in scripts)):
    relative = source.relative_to(repo)
    prefix = Path('code/helpers') if relative.parts[0]=='.tmp' else Path('code/repository')
    remaining = Path(*relative.parts[1:]) if relative.parts[0]=='.tmp' else relative
    pairs.append((source,prefix/remaining))
pairs.append((binding_path,Path('source-binding.json')))
for view,spec in binding['viewSources'].items():
    for name,sha in spec['inputs'].items():
        source = Path(spec['directory'])/name
        assert digest(source)==sha
        pairs.append((source,Path('source-inputs')/view/name))
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
pairs.append((environment,Path('environment')))
records = []
destinations = set()
for source,relative in pairs:
    assert str(relative) not in destinations,str(relative)
    destinations.add(str(relative))
    destination = target/relative
    destination.parent.mkdir(parents=True,exist_ok=True)
    sha = digest(source)
    with source.open('rb') as incoming,destination.open('xb') as outgoing:
        shutil.copyfileobj(incoming,outgoing,length=65536)
    if source==environment:
        destination.chmod(0o755)
    assert digest(destination)==sha
    records.append(dict(source=str(source),retained=str(relative),sha256=sha,bytes=destination.stat().st_size))
manifest = dict(kind='retained-source-star-cell-nn-family-and-diagnostics-v1',
    activeTargets=dict(overview=150,individual=750),targetsMet=False,
    canonicalBest=dict(overview=285,individual=1963,sha256=digest(canonical)),
    installedExtensionVersion='0.0.1069',appPreviewUnchanged=True,
    training=dict(actualGraphUpdates=proof['totalUpdates'],actualGraphTeacherProbes=proof['totalTeacherProbes'],
        actualGraphAccepted=0,toySelectorUpdates=300,toyVisualBefore=2,toyVisualAfter=0),
    sourceCellControlTeachers=dict(actualGraphControls=96,toyControls=4,actualGraphBetterLegal=0),
    freeBodyCommonAnchorControls=dict(actualGraphControls=12,legal=12,minimumLegalIndividual=2254,
        maximumBodyDisplacementPixels=common['maximumBodyDisplacementPixels'],
        zeroHeadSourceIdentity=False,commonAnchorBasisChangesPortsBeforeBodyMotion=True),
    validation=dict(allTeacherLabelsIndependentlyRemeasured=True,
        completeTrainingFamilyFullNativeMeasurements=proof['totalFullNativeMeasurements'],
        completeControlAndToyFullNativeMeasurements=controls['totalFullNativeMeasurements'],
        completeCommonAnchorFullNativeMeasurements=common['fullNativeMeasurements'],
        totalFullNativeMeasurements=proof['totalFullNativeMeasurements']+controls['totalFullNativeMeasurements']+common['fullNativeMeasurements'],
        heldOutGeneralizationVerified=False,productLoadOrBrowserVerified=False),
    resourcePolicy=dict(sampledRssGuardMiB=128,mathThreads=1,nice=10,jobsSerialized=True,
        maximumCompletedVerificationPeakMiB=68.6),
    constraints=dict(nativeCoordinateSearchOrRepairs=0,completeOriginalModels=1244,
        completeCanonicalRelationships=1727,originalDimensionsRequired=True,
        hardOverlapSpacingMustBeZero=True,eachViewAreaAtMost=1.5e9),
    nextAction='Joint body/port NN proposals must preserve the source at zero and avoid the whole-component anchor remapping cost. Do not repeat unchanged radial or source-cell learning budgets.',
    sourceBindingSha256=digest(binding_path),environmentSha256=digest(environment),
    retainedFiles=len(records),retainedBytes=sum(row['bytes'] for row in records),files=records)
(target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
assert len([p for p in target.rglob('*') if p.is_file()])==len(records)+1
print(json.dumps({key:manifest[key] for key in ('kind','targetsMet','retainedFiles','retainedBytes','training','validation')},ensure_ascii=False))
