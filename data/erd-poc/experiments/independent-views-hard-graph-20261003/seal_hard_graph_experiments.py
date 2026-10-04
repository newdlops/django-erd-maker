"""Seal completed hard-loss/graph-input experiments without overwriting history."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil

source = Path('.tmp/visualcross-ml-150-750-20261003')
target = Path('data/erd-poc/experiments/independent-views-hard-graph-20261003')
names = ['individual-hard1', 'overview-hard1', 'individual-hard2', 'individual-graph1',
         'individual-graph2', 'individual-global1', 'individual-global2']
peaks = [201.2, 174.4, 212.9, 255.8, 194.1, 139.8, 139.9]
assert not target.exists(), 'archive exists; preserve it'
target.mkdir(parents=True)

def digest(path, compressed=False):
    value = hashlib.sha256()
    with (gzip.open(path, 'rb') if compressed else path.open('rb')) as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            value.update(chunk)
    return value.hexdigest()

records = []
def copy(path, relative, plain=False):
    assert path.is_file() and not path.is_symlink()
    compressed = not plain and (path.suffix in ['.tsv', '.jsonl']
                  or path.stat().st_size > 131072 and path.suffix in ['.json', '.log'])
    stored = Path(str(relative)+('.gz' if compressed else ''))
    destination = target/stored
    destination.parent.mkdir(parents=True, exist_ok=True)
    sha = digest(path)
    with path.open('rb') as input, destination.open('xb') as output:
        if compressed:
            with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0, compresslevel=6) as archive:
                shutil.copyfileobj(input, archive, 1024*1024)
        else:
            shutil.copyfileobj(input, output, 1024*1024)
    if not compressed:
        destination.chmod(path.stat().st_mode & 0o777)
    assert sha == digest(path) == digest(destination, compressed)
    records.append({'source': str(path), 'stored': str(stored), 'sha256': sha,
                    'gzip': compressed, 'bytes': path.stat().st_size, 'restoredHashVerified': True})

stages = []
batches = 0
for name, peak in zip(names, peaks):
    directory = source/name
    report = json.loads((directory/('workflow.json' if name.startswith('overview') else 'workflow.audit.json')).read_text())
    stats = json.loads((directory/'learned.tsv.stats.json').read_text())
    assert report['allChecksPassed'] and report['unchangedSourceVerified']
    assert report['bestCheckpoint'] is None
    assert stats['policyActionsEvaluated'] == report['frozenNetworkBatchesReplayed']
    for file_name, expected in report['implementationHashes'].items():
        assert digest(directory/('source-'+file_name)) == expected
    for file_name, expected in report['verifiedOutputHashes'].items():
        assert digest(directory/file_name) == expected
    batches += stats['policyActionsEvaluated']
    legal = [row['geometry']['visual'] for row in report['history'] if row['geometry']['legal']]
    stages.append({'name': name, 'overviewOnly': stats['overviewOnly'], 'retainedVisual': stats['visual'],
                   'frozenBatches': stats['policyActionsEvaluated'], 'legalBatches': len(legal),
                   'bestLegalProposedVisual': min(legal) if legal else None,
                   'peakMiB': peak, 'promoted': False, 'candidateSha256': report['candidateSha256']})
    for path in sorted(directory.rglob('*')):
        if path.is_file():
            copy(path, path.relative_to(source))

for name in ['hard-geometry-validation.json', 'validate_hard_geometry.py', 'seal_hard_graph_experiments.py']:
    copy(source/name, Path(name))
for name in ['run_joint_neural_layout.py', 'joint_layout_proxy.py', 'joint_grouped_routes.py',
             'joint_hard_geometry.py', 'joint_graph_features.py', 'joint_sampled_proxy.py',
             'ml_joint_batch_environment.cpp', 'ml_component_environment.cpp', 'learned_global_replay.py',
             'run_memory_bounded.py', 'apply_learned_components.cjs', 'audit_individual_policy_experiment.cjs',
             'audit_leaf_card_connections.cjs', 'export_learned_components.cjs',
             'constrained_dual_node_geometry.h', 'constrained_boundary_sweep.h', 'constrained_scene.h']:
    copy(Path('scripts/erd-poc')/name, Path('code')/name)
copy(source/'joint-batch-environment-v3', Path('dependencies/joint-batch-environment-v3'))
for suffix in ['layout.json', 'audit.json', 'provenance.json']:
    name = 'captain-ml-independent-views.'+suffix
    copy(Path('data/erd-poc/candidates')/name, Path('current-best')/name, plain=True)

validation = {'continuousGradientComparisons': {'baseThreeTemperatures': 132, 'hardGeometry': 40, 'sampledLoss': 24},
              'hardCountsOnFrozenFailedBatchesMatchNative': True,
              'hardBaselineLossZeroInBothViews': True, 'sampledFullPopulationParity': True,
              'graphFeaturesEdgeOrderInvariant': True, 'graphIsolatesZero': True,
              'graphFeaturesUseLayoutCoordinates': False,
              'sparseGraphFeatureFixtureEigenResidual': 6.661338147750939e-16,
              'largeGraphSparseFeaturesAreApproximate': True,
              'sparseLargeGraphEigenResidual': 0.006406101893179675,
              'quantizedLossUsesStraightThroughGradient': True,
              'boundaryContactLossImplemented': False, 'allNativeHardConditionsStillRequired': True,
              'sampledTrainingStillUsesExactFullNativeValidation': True,
              'fixedSourceFrameAndAreaPreserved': True,
              'serializedJobs': True, 'mathThreads': 1, 'guardLimitMiB': 256,
              'browserVerified': False, 'newCandidatePromoted': False}
hard = json.loads((source/'hard-geometry-validation.json').read_text())
for row in hard['views']:
    count = lambda part: 2*part['binaryAdjacentCrossings']+part['binaryOwnCardHits']
    assert count(row['active'].get('full', row['active'])) == row['nativeRejectedBatch']['individualHard']
    assert count(row['active'].get('projected', row['active'])) == row['nativeRejectedBatch']['hard']
(target/'validation.json').write_text(json.dumps(validation, indent=2)+'\n')
manifest = {'scope': 'hard-geometry loss, graph features, and sampled global neural training',
            'overviewTarget': 150, 'individualTarget': 750, 'overviewVisual': 299, 'individualVisual': 1996,
            'targetMet': False, 'newCandidatePromoted': False,
            'currentBestSha256': '623b5aa794afdb3233fa541c9f4afe21ec3512d60457b6f510c1156e62224fe9',
            'priorArchive': 'data/erd-poc/experiments/independent-views-attached-20261003/manifest.json',
            'priorArchiveSha256': 'fbc3202ef449e97b0d18a87791ae7aca2c7c25bcbd77221396c714fe338e0efa',
            'frozenNeuralBatches': batches, 'stages': stages, 'validation': 'validation.json',
            'proposalAuthority': 'trained frozen neural outputs; native geometry only decodes and measures',
            'actualBrowserVerified': False, 'files': records}
assert digest(target/'current-best/captain-ml-independent-views.layout.json') == manifest['currentBestSha256']
(target/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps({'archive': str(target), 'manifestSha256': digest(target/'manifest.json'),
                  'stages': len(stages), 'frozenNeuralBatches': batches, 'files': len(records),
                  'storedBytes': sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}))
