"""Preserve pair-context implementation, rollouts, failures, and scope evidence."""
import collections
import gzip
import hashlib
import json
import shutil
from pathlib import Path

base = Path(__file__).parent
target = Path('data/erd-poc/experiments/independent-views-pair-contexts-20261003')
stable = Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
expected = 'da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7'


def read(path):
    return json.loads(path.read_text())


def digest(path, compressed=False):
    result = hashlib.sha256()
    with (gzip.open(path, 'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


assert not target.exists() and digest(stable) == expected
audit = read(stable.with_name('captain-ml-independent-views.audit.json'))
provenance = read(stable.with_name('captain-ml-independent-views.provenance.json'))
assert audit['candidateSha256'] == provenance['candidateSha256'] == expected
assert (audit['overviewVisual'], audit['individualVisual']) == (289, 1964)
validation = read(base / 'pair-context-validation2/report.json')
assert validation['allChecksPassed']
binary = base / 'component-environment-v27-o0'
assert digest(binary) == validation['nativeBinarySha256']
for name, sha in validation['implementationHashes'].items():
    assert digest(Path('scripts/erd-poc') / name) == sha
    assert digest(base / 'pair-context-validation2/sources' / name) == sha
prior_failure = Path(validation['unchangedCompletedChecksReusedFrom'])
assert digest(prior_failure) == validation['unchangedCompletedChecksReportSha256']
assert 'FileNotFoundError' in read(prior_failure)['traceback']
assert validation['legacyReplay']['actions'] == 20000
assert sum(row['globalDeltaComparisons'] for row in validation['nativeTests']) == 14424
coverage = read(base / 'pair-context-coverage1/report.json')
witness_check = read(base / 'pair-context-coverage1/rollout-witness-verification.json')
for view, fixed in [('individual', 1793), ('overview', 213)]:
    assert coverage['views'][view]['fixedVisualConflicts'] == fixed
    assert coverage['views'][view]['targetUnreachableWithThisActionSpaceAlone']
    assert digest(Path(coverage['views'][view]['witnessFile'])) == coverage['views'][view]['witnessSha256']
    assert witness_check['views'][view]['witnessGeometryUnchanged']

dependency_dir = Path('data/erd-poc/experiments/independent-views-bounded-policy-20261003')
dependency_manifest = dependency_dir / 'manifest.json'
assert digest(dependency_manifest) == 'b30a88360738baf1701fd29e59ef7449b6653a9496e2ecb9779b8937883ae154'
dependency_files = read(dependency_manifest)['files']
dependencies, stages = [], []
for view, peak in [('individual', 132.9), ('overview', 191.0)]:
    directory = base / (view + '-pair-context1')
    workflow = read(directory / ('workflow.audit.json' if view == 'individual' else 'workflow.json'))
    policy = read(directory / 'learned.tsv.policy.json')
    product = read(directory / ('individual.audit.json' if view == 'individual' else 'product.audit.json'))
    branch = read(directory / 'branch-map.json')
    assert digest(Path(workflow['source'])) == workflow['sourceSha256']
    assert digest(Path(product['candidate'])) == workflow['candidateSha256'] == product['candidateSha256']
    assert product['visualCrossings'] == (1964 if view == 'individual' else 289)
    assert branch['schema'] == 'source-bound-pair-contexts-v1'
    assert branch['contextIndexSchema'] == 'independent-context-index-v1'
    assert policy['branchMode'] == 'pair-cut' and policy['checkpointBranchMode'] == 'cut'
    assert policy['heuristicSearchCalls'] == 0 and not policy['untrainedControl']
    checkpoint = Path(policy['checkpoint'])
    assert digest(checkpoint) == policy['checkpointSha256']
    stored = next(row for row in dependency_files if row['sha256'] == policy['checkpointSha256'])
    assert digest(dependency_dir / stored['stored'], stored['gzip']) == policy['checkpointSha256']
    dependencies.append({'file': str(dependency_dir / stored['stored']), 'sha256': policy['checkpointSha256'],
        'archiveManifest': str(dependency_manifest), 'archiveManifestSha256': digest(dependency_manifest)})
    for suffix, field in [('.actions.jsonl', 'actionsSha256'), ('.observations.jsonl', 'observationsSha256')]:
        assert digest(directory / ('learned.tsv' + suffix)) == policy[field]
    for name, sha in branch['inputHashes'].items():
        assert digest(directory / name) == sha
    assert digest(directory / 'branches.tsv') == branch['branchMapSha256']
    outcomes = collections.Counter()
    for line in (directory / 'learned.tsv.actions.jsonl').open():
        row = json.loads(line)
        outcomes[row['result']['reason']] += 1
        assert not row['result']['accepted'] or row['result']['gain'] == 0
    assert sum(outcomes.values()) == policy['final']['policyActionsEvaluated']
    for key in ['hardConditions', 'individualHardConditions', 'spacing', 'overlap']:
        assert policy['final'][key] == 0
    if view == 'individual':
        assert workflow['allChecksPassed'] and product['models'] == 1244 and product['relations'] == 1727
        assert product['validBoundaryEndpoints'] == 3454 and product['bboxArea'] <= 1.5e9
        assert product['allSizesAndRelationsPreserved'] and product['outwardBoundaryEndpointsVerified']
        replay_log = (directory / 'workflow.log').read_text()
    else:
        assert workflow['frozenActionReplay'] and workflow['productFileLoadVerified']
        assert product['realModels'] == 1244 and product['canonicalRelationships'] == 1727
        assert product['canonicalCoverageExactlyOnce'] and product['bboxB'] <= 1.5
        replay_log = (directory / 'replay.stdout').read_text()
    replays = [json.loads(line) for line in replay_log.splitlines() if '"frozenCheckpointActionReplay": "pass"' in line]
    assert len(replays) == 1 and replays[0]['actions'] == sum(outcomes.values())
    stages.append({'view': view, 'actions': sum(outcomes.values()), 'acceptedNeutralActions': policy['final']['acceptedActions'],
        'outcomes': dict(outcomes), 'sourceVisual': policy['initial']['visual'], 'savedVisual': product['visualCrossings'],
        'candidateSha256': product['candidateSha256'], 'promoted': False, 'peakMiB': peak})
assert sum(row['actions'] for row in stages) == 3760

files = []
target.mkdir(parents=True)


def copy(path, relative, plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed = not plain and (path.suffix in ['.tsv', '.jsonl'] or
        path.stat().st_size > 131072 and path.suffix in ['.json', '.log', '.cpp'])
    stored = Path(str(relative) + ('.gz' if compressed else ''))
    destination = target / stored
    destination.parent.mkdir(parents=True, exist_ok=True)
    sha = digest(path)
    with path.open('rb') as src, destination.open('xb') as dst:
        if compressed:
            with gzip.GzipFile(filename='', mode='wb', fileobj=dst, mtime=0, compresslevel=1) as archive:
                shutil.copyfileobj(src, archive, 1024 * 1024)
        else:
            shutil.copyfileobj(src, dst, 1024 * 1024)
    if not compressed:
        destination.chmod(path.stat().st_mode & 0o777)
    assert sha == digest(path) == digest(destination, compressed)
    files.append({'source': str(path), 'stored': str(stored), 'sha256': sha, 'gzip': compressed,
                  'bytes': path.stat().st_size, 'restoredHashVerified': True})


for name in ['pair-context-validation1', 'pair-context-validation2', 'individual-pair-context1',
             'overview-pair-context1', 'pair-context-coverage1']:
    for path in sorted((base / name).rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            copy(path, Path(name) / path.relative_to(base / name))
for name in ['validate_pair_contexts.py', 'analyze_pair_context_coverage.py',
             'verify_pair_conflict_witnesses.py', 'seal_pair_context_experiments.py']:
    copy(base / name, Path(name))
copy(binary, Path('dependencies') / binary.name, plain=True)
for suffix in ['layout.json', 'audit.json', 'provenance.json']:
    path = stable.with_name('captain-ml-independent-views.' + suffix)
    copy(path, Path('retained-best') / path.name, plain=True)
prior = Path('data/erd-poc/experiments/independent-views-anchor-rays-20261003/manifest.json')
assert digest(prior) == '93edf0bd5700c25d909004bc578c2a237787364708636fd5b68fe4c531fb13e1'
manifest = {'scope': 'source-bound two-vertex separator action contexts and fixed conflict witnesses',
    'overviewTarget': 150, 'individualTarget': 750, 'overviewVisual': 289, 'individualVisual': 1964,
    'targetMet': False, 'promotedCandidateChanged': False, 'retainedCandidateSha256': expected,
    'newNetworkTrainingUpdates': 0, 'additionalPolicyActions': 3760, 'stages': stages,
    'proposalAuthority': 'existing frozen neural policies transferred from cut contexts; native geometry only decodes and measures',
    'contextIndexSchema': 'independent-context-index-v1', 'validation': validation,
    'coverageEvidence': coverage, 'rolloutWitnessVerification': witness_check,
    'resourcePolicy': {'serialized': True, 'mathThreads': 1, 'nice': 10, 'processGroupRssGuardMiB': 256,
        'hardCpuPercentageQuota': False, 'compileOptimization': '-O0', 'compilePeakMiB': 217.8,
        'validationAttemptPeaksMiB': [77.6, 63.2], 'coveragePeakMiB': 36.8,
        'failedCompile': {'optimization': '-O1', 'exitCode': 137, 'rssAtGuardStopMiB': 263.7,
                          'guardLimitIncreased': False, 'unchangedRetryPerformed': False}},
    'failedValidation': 'pair-context-validation1/failure.json',
    'failedValidationCause': 'fixture input directory lacked nodes.tsv; native checks and legacy replay had passed and were reused by source/binary hash',
    'nextResearch': 'Expand learned node/endpoint proposals to the fixed-conflict support outside these contexts; pair-context-only retraining cannot meet either target.',
    'globalTargetImpossibleProved': False, 'dependencies': dependencies,
    'priorArchive': str(prior), 'priorArchiveSha256': digest(prior),
    'actualBrowserVerified': False, 'files': files}
assert digest(stable) == expected
with (target / 'manifest.json').open('x') as stream:
    stream.write(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'archive': str(target), 'manifestSha256': digest(target / 'manifest.json'),
    'files': len(files), 'actions': 3760, 'storedBytes': sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}), flush=True)
