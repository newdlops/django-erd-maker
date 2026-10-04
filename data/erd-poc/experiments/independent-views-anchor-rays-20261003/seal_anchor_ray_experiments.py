"""Archive completed anchor-ray experiments without changing promoted geometry."""
import collections
import gzip
import hashlib
import json
import shutil
from pathlib import Path

base = Path('.tmp/visualcross-ml-150-750-20261003')
target = Path('data/erd-poc/experiments/independent-views-anchor-rays-20261003')
stable = Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
expected = 'da2317d3d9fe72181587f543843bfdde3cc7030ca328e93c4f627ae21ec356e7'
stage_peaks = {
    'individual-anchor-ray1': 161.6, 'overview-anchor-ray1': 179.8,
    'individual-anchor-sharp1': 154.7, 'overview-anchor-sharp1': 181.1,
    'individual-anchor-depth1': 158.6, 'overview-anchor-depth1': 177.9,
}


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
assert provenance['targetMet'] is False

native = base / 'joint-batch-environment-v7'
dependency_dir = Path('data/erd-poc/experiments/independent-views-global-order-20261003')
dependency_manifest = dependency_dir / 'manifest.json'
assert digest(dependency_manifest) == 'b9fa65014a331faf993beda3ad6a8241400637bdc7df4072519a47ef2ea07e3b'
dependency = next(row for row in read(dependency_manifest)['files']
                  if row['stored'] == 'dependencies/joint-batch-environment-v7')
assert digest(native) == dependency['sha256'] == digest(
    dependency_dir / dependency['stored'], dependency['gzip'])
prior = Path('data/erd-poc/experiments/independent-views-separation-dag-20261003/manifest.json')
assert digest(prior) == '43fe872904c46c4f3e2330eaca3cfa837c3a6f0cb434539e77921b8e2e64ea7a'

validation = read(base / 'anchor-ray-validation5/report.json')
assert validation['nativeBinarySha256'] == digest(native)
assert validation['strictInteriorFixtureEndpoints'] == 192
assert validation['priorCheckpointDispatchReplays'] == 3
for name, sha in validation['implementationHashes'].items():
    assert digest(base / 'anchor-ray-validation5/sources' / name) == sha
for view in validation['views'].values():
    assert view['zeroEndpointsPreserved'] == view['strictInteriorAnchors'] == 3454
    assert view['sourceUnchanged'] and view['frozenRoundtrip']
    assert view['directSurrogateDerivatives'] + view['phaseOnlyDerivatives'] + view['fullLossDerivatives'] == 30
for number in range(1, 5):
    assert (base / f'anchor-ray-validation{number}/failure.json').is_file()
    assert (base / f'anchor-ray-validation{number}/failure-context.json').is_file()

calibration = read(base / 'anchor-temperature-calibration.json')
assert calibration['nativeMeasurementsReused'] and not calibration['newCandidateCreated']
for view in calibration['views'].values():
    for case in view['cases']:
        assert all(score['binaryVisual'] == case['nativeVisual'] for score in case['scores'])
        if 'checkpoint' in case:
            assert digest(Path(case['checkpoint'])) == case['checkpointSha256']
blocks = read(base / 'block-conflict-analysis1/report.json')
pairs = read(base / 'pair-separator-analysis1/report.json')
assert not blocks['positionsProposed'] and not pairs['positionsProposed']
assert blocks['graphFixturePassed'] and not blocks['crossingLowerBoundProved']
assert not pairs['allPairsExhaustive']
for view, expected_visual in [('individual', 1964), ('overview', 289)]:
    assert blocks['views'][view]['exactNativeCountMatches']
    assert blocks['views'][view]['sourceVisual'] == expected_visual
    assert pairs['views'][view]['sourceSha256'] == blocks['views'][view]['sourceSha256']

stages = []
for name, peak in stage_peaks.items():
    directory = base / name
    individual = name.startswith('individual')
    workflow = read(directory / ('workflow.audit.json' if individual else 'workflow.json'))
    product = read(directory / ('individual.audit.json' if individual else 'product.audit.json'))
    stats = read(directory / 'learned.tsv.stats.json')
    candidate = directory / ('candidate.individual.layout.json' if individual else 'candidate.layout.json')
    source_visual = 1964 if individual else 289
    assert digest(Path(workflow['source'])) == workflow['sourceSha256']
    assert digest(candidate) == product['candidateSha256'] == workflow['candidateSha256']
    assert workflow['allChecksPassed'] and workflow['neuralChecksPassed']
    assert workflow['anchorRays'] and workflow['separationDag']
    assert not workflow['trainableEndpointHead'] and workflow['bestCheckpoint'] is None
    assert workflow['unchangedSourceVerified'] and not workflow['frozenWinningGeometryReplayed']
    assert workflow['environmentSha256'] == digest(native)
    assert stats['visual'] == product['visualCrossings'] == workflow['sourceVisual'] == source_visual
    for field in ['hardConditions', 'individualHardConditions', 'spacing', 'overlap']:
        assert not stats[field]
    assert not product['spacingViolations']
    if individual:
        assert product['models'] == 1244 and product['relations'] == 1727
        assert product['validBoundaryEndpoints'] == 3454 and product['bboxArea'] <= 1.5e9
        assert product['allSizesAndRelationsPreserved'] and product['outwardBoundaryEndpointsVerified']
    else:
        assert product['realModels'] == 1244 and product['canonicalRelationships'] == 1727
        assert product['canonicalCoverageExactlyOnce'] and product['productCoordinatesPreserved']
        assert product['bboxB'] <= 1.5
    for filename, sha in workflow['implementationHashes'].items():
        assert digest(directory / ('source-' + filename)) == sha
    for filename, sha in {**workflow['inputHashes'], **workflow['verifiedOutputHashes']}.items():
        assert digest(directory / filename) == sha
    outcomes = collections.Counter()
    legal_visuals = []
    for line in (directory / 'joint-batches.jsonl').open():
        row = json.loads(line)
        assert digest(Path(row['checkpoint'])) == row['checkpointSha256']
        outcomes[row['result']['reason']] += 1
        assert not row['result']['accepted']
        if row['result']['legal']:
            legal_visuals.append(row['result']['visual'])
    count = sum(outcomes.values())
    assert count == workflow['frozenNetworkBatchesReplayed'] == stats['policyActionsEvaluated']
    assert not outcomes['spacing'] and not outcomes['frame']
    assert min(legal_visuals) >= source_visual
    stages.append({'name': name, 'updates': workflow['iterations'], 'frozenBatches': count,
                   'outcomes': dict(outcomes), 'minimumLegalVisual': min(legal_visuals),
                   'retainedVisual': source_visual, 'candidateSha256': digest(candidate),
                   'peakMiB': peak, 'promoted': False,
                   'temperatureStart': workflow['lossTemperatureStart'],
                   'temperatureEnd': workflow.get('lossTemperatureEnd', 1.0)})
assert sum(row['updates'] for row in stages) == 927
assert sum(row['frozenBatches'] for row in stages) == 242

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
    files.append({'source': str(path), 'stored': str(stored), 'sha256': sha,
                  'gzip': compressed, 'bytes': path.stat().st_size, 'restoredHashVerified': True})


directories = [*stage_peaks, *(f'anchor-ray-validation{i}' for i in range(1, 6)),
               'block-conflict-analysis1', 'pair-separator-analysis1']
for name in directories:
    for path in sorted((base / name).rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts:
            copy(path, Path(name) / path.relative_to(base / name))
for name in ['validate_anchor_rays.py', 'diagnose_anchor_derivatives.py',
             'calibrate_anchor_temperature.py', 'analyze_block_conflicts.py',
             'analyze_pair_separators.py', 'anchor-derivative-step-diagnosis.json',
             'anchor-temperature-calibration.json', 'seal_anchor_ray_experiments.py']:
    copy(base / name, Path(name))
for name in ['joint_anchor_ray_policy.py', 'joint_separation_policy.py',
             'joint_neural_ports.py', 'run_joint_neural_layout.py', 'run_memory_bounded.py']:
    copy(Path('scripts/erd-poc') / name, Path('current-code') / name)
for suffix in ['layout.json', 'audit.json', 'provenance.json']:
    path = stable.with_name('captain-ml-independent-views.' + suffix)
    copy(path, Path('retained-best') / path.name, plain=True)

manifest = {
    'scope': 'fixed interior anchor rays, loss calibration, and structural conflict attribution',
    'overviewTarget': 150, 'individualTarget': 750, 'overviewVisual': 289, 'individualVisual': 1964,
    'targetMet': False, 'promotedCandidateChanged': False, 'retainedCandidateSha256': expected,
    'newNetworkTrainingUpdates': 927, 'jointNeuralBatches': 242, 'additionalPolicyActions': 0,
    'acceptedNewCandidates': 0, 'stages': stages, 'validation': validation,
    'failedValidationsPreserved': [f'anchor-ray-validation{i}/failure.json' for i in range(1, 5)],
    'validationHistory': 'After attempt 1 the model added a 0.01 inset and coincident-endpoint pooling. Later step diagnostics separated phase/full-loss step sizes and replaced absolute monotonicity with scale-aware adjacent agreement; full-loss relative tolerance tightened to 1e-4.',
    'decoderLimit': 'Source pair order remains fixed; native hard validity is not guaranteed; continuous gradients use straight-through cent quantization.',
    'temperatureCalibration': calibration, 'blockAttribution': blocks, 'pairSeparatorAnalysis': pairs,
    'nextResearch': 'Explicit source-bound two-vertex separator contexts; no such learned decoder is implemented or validated in this archive.',
    'priorArchive': str(prior), 'priorArchiveSha256': digest(prior),
    'dependencies': [{'file': str(dependency_dir / dependency['stored']), 'sha256': dependency['sha256'],
                      'archiveManifest': str(dependency_manifest), 'archiveManifestSha256': digest(dependency_manifest)}],
    'nativeSourceProvenance': 'joint-batch-v7 build inputs are in the global-order archive; later component source snapshots were not used to build it.',
    'resourcePolicy': {'serialized': True, 'mathThreads': 1, 'nice': 10, 'processGroupRssGuardMiB': 256,
        'hardCpuPercentageQuota': False, 'recordedStagePeakMiB': stage_peaks,
        'recordedValidationPeakMiB': [126.5, 123.6, 87.9, 117.7, 136.5],
        'derivativeDiagnosisPeakMiB': 110.2, 'temperatureCalibrationPeakMiB': 85.9,
        'blockAnalysisPeakMiB': 36.7, 'pairAnalysisPeakMiB': 21.0},
    'actualBrowserVerified': False, 'files': files,
}
assert digest(stable) == expected
with (target / 'manifest.json').open('x') as stream:
    stream.write(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'archive': str(target), 'manifestSha256': digest(target / 'manifest.json'),
                  'stages': len(stages), 'newNetworkTrainingUpdates': 927, 'jointNeuralBatches': 242,
                  'files': len(files), 'storedBytes': sum(p.stat().st_size for p in target.rglob('*') if p.is_file())}), flush=True)
