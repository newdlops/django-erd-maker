"""Archive completed model experiments exclusively; preserve negative results."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil

source = Path('.tmp/visualcross-ml-150-750-20261003')
target = Path('data/erd-poc/experiments/independent-views-attached-20261003')
stage_names = ['individual-annealed1', 'overview-grouped1', 'overview-grouped2',
               'overview-grouped-ports1', 'overview-grouped-ports2',
               'overview-attached1', 'individual-attached1']
assert not target.exists(), 'preserve the existing immutable archive'
target.mkdir(parents=True)

def digest(path, compressed=False):
    value = hashlib.sha256()
    with (gzip.open(path, 'rb') if compressed else path.open('rb')) as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()

files = []
def copy(path, relative, plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed = not plain and (path.suffix in ['.jsonl', '.tsv']
                               or path.stat().st_size > 131072 and path.suffix in ['.json', '.log'])
    stored = Path(str(relative) + ('.gz' if compressed else ''))
    destination = target / stored
    destination.parent.mkdir(parents=True, exist_ok=True)
    sha = digest(path)
    with path.open('rb') as input, destination.open('xb') as output:
        if compressed:
            with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0, compresslevel=6) as archive:
                shutil.copyfileobj(input, archive, 1024 * 1024)
        else:
            shutil.copyfileobj(input, output, 1024 * 1024)
    if not compressed:
        destination.chmod(path.stat().st_mode & 0o777)
    assert digest(destination, compressed) == sha == digest(path)
    files.append({'source': str(path), 'stored': str(stored), 'sha256': sha,
                  'gzip': compressed, 'bytes': path.stat().st_size, 'restoredHashVerified': True})

stages = []
policy_actions = neural_batches = 0
for name in stage_names:
    directory = source / name
    report_path = directory / ('workflow.audit.json' if name.startswith('individual') else 'workflow.json')
    report = json.loads(report_path.read_text())
    stats = json.loads((directory / 'learned.tsv.stats.json').read_text())
    policy = json.loads((directory / 'learned.tsv.policy.json').read_text())
    joint = policy.get('modelKind') == 'coordinated-displacement-network'
    assert report.get('allChecksPassed') if joint else report['frozenActionReplay'] and report['productFileLoadVerified']
    if joint:
        neural_batches += stats['policyActionsEvaluated']
        for file_name, sha in report['implementationHashes'].items():
            assert digest(directory / ('source-' + file_name)) == sha
    else:
        policy_actions += stats['policyActionsEvaluated']
    stages.append({'name': name, 'jointNetwork': joint, 'visual': stats['visual'],
                   'individualVisual': stats['individualVisual'], 'overviewOnly': stats['overviewOnly'],
                   'evaluated': stats['policyActionsEvaluated'], 'candidateSha256': report['candidateSha256']})
    for path in sorted(directory.rglob('*')):
        if path.is_file():
            copy(path, path.relative_to(source))

for name in ['grouped-proxy-validation.json', 'attached-proxy-validation.json',
             'validate_grouped_proxy.py', 'validate_attached_proxy.py',
             'joint-batch-environment-v3', 'seal_attached_experiments.py']:
    copy(source / name, Path(name))
for name in ['joint_layout_proxy.py', 'joint_grouped_routes.py', 'run_joint_neural_layout.py',
             'ml_joint_batch_environment.cpp', 'ml_component_environment.cpp', 'learned_global_replay.py',
             'run_memory_bounded.py', 'run_learned_leaf_layout.py', 'learn_card_policy.py',
             'apply_learned_components.cjs', 'audit_leaf_card_connections.cjs',
             'audit_individual_policy_experiment.cjs', 'export_learned_components.cjs',
             'constrained_dual_node_geometry.h', 'constrained_boundary_sweep.h', 'constrained_scene.h']:
    copy(Path('scripts/erd-poc') / name, Path('code') / name)
for version in [10, 11]:
    name = f'leaf-component-policy-v{version}.npz'
    copy(Path('data/erd-poc/checkpoints') / name, Path('dependencies') / name)
copy(Path('.tmp/visualcross-ml-targets-20261003/port-environment-v4'), Path('dependencies/port-environment-v4'))
copy(source / 'joint-batch-environment-v2', Path('dependencies/joint-batch-environment-v2'))
for suffix in ['layout.json', 'audit.json', 'provenance.json']:
    name = 'captain-ml-independent-views.' + suffix
    copy(Path('data/erd-poc/candidates') / name, Path('current-best') / name, plain=True)

validation = {
    'lossTemperatureGradients': {'comparisons': 132, 'temperatures': [1, 8, 32], 'passed': True, 'peakMiB': 39.4},
    'groupedRouteGradients': {'comparisons': 56, 'baselineVisual': 364, 'nativeParity': True},
    'attachedRouteGradients': {'comparisons': 38, 'overviewVisual': 299, 'individualVisual': 1996,
                             'nativeParity': True, 'baselinePreserved': True, 'peakMiB': 91.6},
    'nativeV3': {'geometryComparisons': 384, 'legalBatches': 323, 'attachedOffsetsVerified': True,
                 'compilePeakMiB': 225.3, 'testPeakMiB': 2.7},
    'resourcePeaksMiB': dict(zip(stage_names, [189.2, 179.6, 177.8, 188.9, 190.1, 181.0, 197.8])),
    'serializedJobs': True, 'mathThreads': 1, 'guardLimitMiB': 256,
    'trainingExitedBeforeProductAudit': True, 'actualBrowserVerified': False,
    'allNewExperimentsUnpromoted': True,
    'remainingIssue': 'Attached neural proposals violate native hard geometry; current soft loss excludes adjacent crossings and own-card hits.'
}
(target / 'validation.json').write_text(json.dumps(validation, indent=2) + '\n')
current = json.loads((Path('data/erd-poc/candidates/captain-ml-independent-views.audit.json')).read_text())
assert current['candidateSha256'] == '623b5aa794afdb3233fa541c9f4afe21ec3512d60457b6f510c1156e62224fe9'
manifest = {'scope': 'annealed, grouped-route and attached-port neural experiments',
            'overviewTarget': 150, 'individualTarget': 750, 'overviewVisual': 299, 'individualVisual': 1996,
            'targetMet': False, 'newCandidatePromoted': False, 'currentBestSha256': current['candidateSha256'],
            'priorArchive': 'data/erd-poc/experiments/independent-views-joint-20261003/manifest.json',
            'priorArchiveSha256': '4b0fb4622af263fc7af95c433bc8edfb44889194835600925a0994df14b28fda',
            'policyActions': policy_actions, 'jointNeuralBatches': neural_batches, 'stages': stages,
            'proposalAuthority': 'frozen neural outputs; native geometry decodes, measures and accepts/rejects',
            'validation': 'validation.json', 'actualBrowserVerified': False, 'files': files}
(target / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'archive': str(target), 'manifestSha256': digest(target / 'manifest.json'),
                  'stages': len(stages), 'files': len(files), 'policyActions': policy_actions,
                  'jointNeuralBatches': neural_batches,
                  'storedBytes': sum(path.stat().st_size for path in target.rglob('*') if path.is_file())}))
