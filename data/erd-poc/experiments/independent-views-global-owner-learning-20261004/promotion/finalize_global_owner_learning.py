"""Verify final canonical bytes, scope metadata, and bind post-promotion records."""
import hashlib
import json
import os
import shutil
from pathlib import Path


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def write_new(path, value):
    assert not path.exists(), str(path)
    with path.open('x') as output:
        json.dump(value, output, indent=2)
        output.write('\n')


root = Path('.tmp/visualcross-ml-150-750-20261004/single-owner-global-walk1')
seal = json.loads(Path('.tmp/global-owner-learning-archive-seal-receipt.json').read_text())
restore = json.loads(Path('.tmp/global-owner-learning-archive-independent-verification.json').read_text())
promotion = json.loads(Path('.tmp/global-owner-learning-promotion-receipt.json').read_text())
manifest = Path(seal['manifest'])
assert digest(manifest) == seal['manifestSha256'] == restore['manifestSha256']
assert restore['status'] == 'pass' and restore['mappingsRestoredAndLiveSourcesMatched'] == 474
assert promotion['promoted']
base = Path('data/erd-poc/candidates/captain-ml-independent-views')
layout = Path(str(base) + '.layout.json')
audit_path = Path(str(base) + '.audit.json')
provenance_path = Path(str(base) + '.provenance.json')
assert digest(layout) == promotion['candidateSha256'] == seal['candidateSha256']
assert digest(layout) == '4e741de39ca655c7ff042422b9407206427f89e02ecd56b7f4a9f36f6a5cd02e'
assert layout.read_bytes() == (root / 'combined-best1/candidate.layout.json').read_bytes()
audit = json.loads(audit_path.read_text())
provenance = json.loads(provenance_path.read_text())
for record in (audit, provenance):
    assert record['candidateSha256'] == digest(layout)
    assert (record['overviewVisual'], record['individualVisual']) == (285, 1963)
    assert (record['overviewTarget'], record['individualTarget']) == (150, 750)
    assert not record['browserVerified']
assert not audit['thresholdsMet'] and not provenance['targetMet']
assert provenance['stagesInLatestArchive'] == 6 and provenance['additionalActionsArchived'] == 4349
assert provenance['modelsTrainedInLatestArchive'] == 1
assert provenance['newNetworkTrainingUpdatesInLatestContinuation'] == 23488
assert provenance['selectedCheckpointUpdatesInLatestContinuation'] == 734
assert provenance['trainingRewardMeasurementsArchived'] == 2512
assert provenance['actualCaptainRewardMeasurementsUsedForTraining'] == 1991
assert provenance['syntheticRewardMeasurementsArchived'] == 0
assert provenance['newCaptainInferenceMeasurementsNotUsedForTraining'] == 4096
assert provenance['latestFrozenCriticCheckpointSha256'] == '7831d4b8679769a22e4eab8a344f846e892e5ebd52b89b68afafb8ef92c777f9'
assert provenance['individualCriticParentTraining']['checkpointSha256'] == '915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10'
assert provenance['matchedFrozenParentCheckpointSha256'] == 'ef92608e90a15f9d8b467f3a7ee964b900ea57d740da4503f83199d57e107df1'
assert provenance['matchedFrozenParentHasIdenticalWinner'] and not provenance['learningCausedCaptainGainEstablished']
assert provenance['resourceLimitMiB'] == 128 and not provenance['resourceLimitRaised']
assert provenance['productAuditPeakMiB'] == 111.6
before = json.loads((root / 'promotion-before' / layout.name).read_text())
after = json.loads(layout.read_text())
assert before['individualView'] == after['individualView']
old = {node['modelId']: node for node in before['nodes']}
new = {node['modelId']: node for node in after['nodes']}
assert old.keys() == new.keys() and len(new) == 1244
assert all(old[key]['size'] == node['size'] for key, node in new.items())
changed = [key for key, node in new.items() if old[key]['position'] != node['position']]
assert changed == ['db.ShareholdersMeetingDirectorAttendance']
assert before['engineMetadata']['leafBundles'] == after['engineMetadata']['leafBundles']
assert len(after['routedEdges']) == 1727
assert {edge['edgeId'] for edge in after['routedEdges']} == {edge['edgeId'] for edge in before['routedEdges']}
overview = json.loads((root / 'overview-product1/candidate.layout.json').read_text())
for key in ('nodes', 'routedEdges', 'engineMetadata'):
    assert after[key] == overview[key]
context = Path('context.md').read_text()
readme = Path('data/erd-poc/candidates/README.md').read_text()
assert 'promoted best 285 / 1,963' in context.splitlines()[2]
assert '개요 285 / 개별 보기 1,963' in readme.split('\n\n')[1]
assert seal['manifestSha256'] in context
supplement = manifest.parent / 'promotion'
supplement.mkdir(exist_ok=False)
previous_metadata = supplement / 'provenance-before-scope-reconciliation.json'
shutil.copyfile(provenance_path, previous_metadata)
assert digest(previous_metadata) == digest(provenance_path)
for suffix in ('Record', 'RecordSha256'):
    old_key = 'previousGlobalPolicyPromotionLatestPromotionMetadataReconciliation' + suffix
    new_key = 'previousTrainedOwnerMetadataReconciliation' + suffix
    assert old_key in provenance and new_key not in provenance
    provenance[new_key] = provenance.pop(old_key)
provenance['browserCheckAttemptedInLatestContinuation'] = False
provenance['browserFailureEvidenceScope'] = 'inherited previous supported-runtime bootstrap failure; no new browser rendering check'
audit['browserCheckAttemptedInLatestContinuation'] = False
audit['visualQaScope'] = 'product file loading and route functions verified; actual rendered browser not checked'
reconciliation = {
    'scope': 'metadata only; model-selected layout and base manifest unchanged',
    'candidateSha256': digest(layout), 'baseManifestSha256': digest(manifest),
    'provenanceBeforeSha256': digest(previous_metadata),
    'latestExecutedUpdates': 23488, 'latestSelectedAdaptationUpdates': 734,
    'rawCheckpointMarker': 14195, 'rawMarkerIsNotAllAncestorLifetimeUpdates': True,
    'rawInferenceReportsUnchanged': True,
    'effectiveInferenceDtypeEvidence': str(root / 'verification/matched-control.audit.json'),
    'effectiveInferenceDtypeEvidenceSha256': digest(root / 'verification/matched-control.audit.json'),
    'newBrowserCheckPerformed': False, 'reconcilerSha256': digest(__file__)
}
reconciliation_path = supplement / 'metadata-reconciliation.json'
write_new(reconciliation_path, reconciliation)
provenance['latestPromotionMetadataReconciliationRecord'] = str(reconciliation_path)
provenance['latestPromotionMetadataReconciliationRecordSha256'] = digest(reconciliation_path)
for path, record in ((audit_path, audit), (provenance_path, provenance)):
    temporary = path.with_name(path.name + '.reconciling')
    write_new(temporary, record)
    os.replace(temporary, path)
assert digest(layout) == seal['candidateSha256'] and digest(manifest) == seal['manifestSha256']
result = {
    'status': 'pass', 'candidateSha256': digest(layout), 'archiveManifestSha256': seal['manifestSha256'],
    'promotedBytesIdenticalToProductAuditedCandidate': True, 'overviewVisual': 285, 'individualVisual': 1963,
    'individualViewExactlyPreservedAsJsonData': True, 'overviewChangedModels': changed,
    'all1244SizesPreserved': True, 'all1727RelationshipIdsPreserved': True,
    'overviewGeometryExactlyMatchesVerifiedModelOutput': True,
    'newTrainingUpdates': 23488, 'selectedAdaptationUpdates': 734,
    'newTrainingCountersAndViewSpecificCriticLineageVerified': True,
    'neuralProposals': 4349, 'completedStages': 6,
    'resourceLimitMiB': 128, 'resourceLimitRaised': False, 'productAuditPeakMiB': 111.6,
    'browserVerified': False, 'targetMet': False, 'verifierSha256': digest(__file__)
}
output = Path('.tmp/global-owner-learning-final-state-verification.json')
write_new(output, result)
files = [
    layout, audit_path, provenance_path, Path('context.md'), Path('data/erd-poc/candidates/README.md'), output,
    Path('.tmp/global-owner-learning-archive-seal-receipt.json'),
    Path('.tmp/global-owner-learning-archive-independent-verification.json'),
    Path('.tmp/global-owner-learning-promotion-receipt.json'),
    Path('.tmp/global-owner-learning-archive-seal.guard.log'),
    Path('.tmp/global-owner-learning-archive-verification.guard.log'),
    Path('.tmp/global-owner-learning-promotion.guard.log'),
    Path('.tmp/trained-owner-browser-bootstrap.audit.json'), Path(__file__)
]
bindings = []
for source in files:
    destination = supplement / source.name
    assert not destination.exists(), str(destination)
    shutil.copyfile(source, destination)
    assert digest(source) == digest(destination)
    bindings.append({'source': str(source), 'archive': str(destination),
                     'sha256': digest(destination), 'bytes': destination.stat().st_size})
for source in (previous_metadata, reconciliation_path):
    bindings.append({'source': str(source), 'archive': str(source),
                     'sha256': digest(source), 'bytes': source.stat().st_size})
write_new(supplement / 'artifact-record.json', {
    'baseManifest': str(manifest), 'baseManifestSha256': seal['manifestSha256'],
    'baseArchiveIndependentRestoreStatus': 'pass',
    'supplementScope': 'post-promotion state, scoped metadata, documentation and receipts; base archive immutable',
    'finalVerification': result, 'files': bindings, 'allSupplementBytesCopyHashVerified': True
})
print(json.dumps(result), flush=True)
