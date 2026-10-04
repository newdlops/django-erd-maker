"""Scope inherited metadata, confirm the promoted product, and pin final records."""
import hashlib
import json
import os
import shutil
from pathlib import Path

root = Path('.tmp/visualcross-ml-150-750-20261004/single-owner-trained-walk1')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as source:
        while block := source.read(65536):
            h.update(block)
    return h.hexdigest()


def write_new(path, value):
    path = Path(path)
    assert not path.exists(), str(path)
    with path.open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


seal = json.loads(Path('.tmp/trained-owner-archive-seal-receipt.json').read_text())
restore = json.loads(Path('.tmp/trained-owner-archive-independent-verification.json').read_text())
promotion = json.loads(Path('.tmp/trained-owner-promotion-receipt.json').read_text())
manifest = Path(seal['manifest'])
assert digest(manifest) == seal['manifestSha256'] == restore['manifestSha256']
assert restore['status'] == 'pass' and restore['mappingsRestoredAndLiveSourcesMatched'] == 466
assert promotion['promoted']
m = json.loads(manifest.read_text())
base = Path('data/erd-poc/candidates/captain-ml-independent-views')
layout = Path(str(base) + '.layout.json')
audit_path = Path(str(base) + '.audit.json')
prov_path = Path(str(base) + '.provenance.json')
assert digest(layout) == promotion['candidateSha256'] == seal['candidateSha256']
assert digest(layout) == '4c88250bd2dce3ffb85d640c35d11d952664b0358658a0f0b35b94cbccd28688'
assert layout.read_bytes() == (root / 'combined-best1/candidate.layout.json').read_bytes()
audit = json.loads(audit_path.read_text())
provenance = json.loads(prov_path.read_text())
for record in (audit, provenance):
    assert record['candidateSha256'] == digest(layout)
    assert (record['overviewVisual'], record['individualVisual']) == (286, 1963)
    assert (record['overviewTarget'], record['individualTarget']) == (150, 750)
    assert not record['browserVerified']
assert not audit['thresholdsMet'] and not provenance['targetMet']
assert provenance['archiveManifestSha256'] == seal['manifestSha256']
assert provenance['stagesInLatestArchive'] == 8 and provenance['additionalActionsArchived'] == 2771
for key in ('modelsTrainedInLatestArchive', 'sourceStagesUsedForLatestTraining',
            'newNetworkTrainingUpdatesInLatestContinuation', 'selectedCheckpointUpdatesInLatestContinuation',
            'trainingRewardMeasurementsArchived', 'syntheticRewardMeasurementsArchived',
            'actualCaptainRewardMeasurementsUsedForTraining'):
    assert provenance[key] == 0, key
assert provenance['newCaptainInferenceMeasurementsNotUsedForTraining'] == 2771
assert provenance['resourceLimitMiB'] == 128 and not provenance['resourceLimitRaised']
assert provenance['productAuditPeakMiB'] == 114.1
assert provenance['latestCriticTrainedOnSingleOwnerMoves']
assert not provenance['learningCausedCaptainGainEstablished']
assert provenance['overviewCriticParentTraining']['checkpointSha256'] == 'ef92608e90a15f9d8b467f3a7ee964b900ea57d740da4503f83199d57e107df1'
assert provenance['overviewCriticParentTraining']['newTrainingUpdatesExecuted'] == 20512
assert provenance['overviewCriticParentTraining']['selectedCheckpointUpdates'] == 13461
assert provenance['overviewCriticParentTraining']['newUpdatesInThisContinuation'] == 0
assert provenance['individualCriticParentTraining']['checkpointSha256'] == '915a84051eedea56c9695a479737706772eef6213ac8b986118ce952c37a7a10'
assert provenance['cachedModelInputStateNativeControls'] == 82
assert provenance['allCachedModelInputsNativeVerifiedExactly']
assert provenance['observerSourceSha256'] == digest('scripts/erd-poc/single_owner_cached_observer_v2.py')

before = json.loads((root / 'promotion-before' / layout.name).read_text())
after = json.loads(layout.read_text())
assert before['individualView'] == after['individualView']
old = {node['modelId']: node for node in before['nodes']}
new = {node['modelId']: node for node in after['nodes']}
assert old.keys() == new.keys() and len(new) == 1244
assert all(old[key]['size'] == node['size'] for key, node in new.items())
changed = [key for key, node in new.items() if old[key]['position'] != node['position']]
assert changed == ['db.MeetingDraftAgendaDirectorCompensationItem']
assert len(after['routedEdges']) == 1727
assert {edge['edgeId'] for edge in after['routedEdges']} == {edge['edgeId'] for edge in before['routedEdges']}
overview = json.loads((root / 'overview-product1/candidate.layout.json').read_text())
for key in ('nodes', 'routedEdges', 'engineMetadata'):
    assert after[key] == overview[key]
assert before['engineMetadata']['leafBundles'] == after['engineMetadata']['leafBundles']
for key in ('actualProductFileLoadVerified', 'completeIndividualGeometryPreserved',
            'browserRouteFunctionParity', 'roundTripViewSwitchVerified'):
    assert audit[key]

context = Path('context.md').read_text()
readme = Path('data/erd-poc/candidates/README.md').read_text()
assert 'promoted best 286 / 1,963' in context.splitlines()[2]
assert '개요 286 / 개별 보기 1,963' in readme.split('\n\n')[1]
assert seal['manifestSha256'] in context

supplement = manifest.parent / 'promotion'
supplement.mkdir(exist_ok=False)
prov_before = supplement / 'provenance-before-scope-reconciliation.json'
audit_before = supplement / 'audit-before-browser-attempt-record.json'
shutil.copyfile(prov_path, prov_before)
shutil.copyfile(audit_path, audit_before)
assert digest(prov_before) == digest(prov_path) and digest(audit_before) == digest(audit_path)

browser_record = {
    'skill': '/Users/lky/.codex/plugins/cache/openai-bundled/browser/26.721.41059/skills/control-in-app-browser/SKILL.md',
    'toolAvailable': True,
    'supportedRuntimeBootstrapAttempted': True,
    'bootstrapError': 'Importing module "node:process" is not allowed in node_repl',
    'runtimeInitialized': False,
    'browserSelected': False,
    'browserVerified': False,
    'renderedViewportsChecked': [],
    'screenshotsTaken': 0,
    'interactionsChecked': [],
    'alternativeAutomationSurfaceUsed': False
}
browser_path = Path('.tmp/trained-owner-browser-bootstrap.audit.json')
write_new(browser_path, browser_record)
diagnostic_source = Path('.tmp/visualcross-ml-150-750-20261004/single-owner1/cli-transport-latest.json')
diagnostic = json.loads(diagnostic_source.read_text())
assert diagnostic['surfaceConfirmedByUser'] == 'terminal Codex CLI'
assert not diagnostic['currentThreadTransportEvent']['reportedResponseBodyDecodeErrorMatched']
assert not diagnostic['memoryEstablishedAsCause']
cli_path = Path('.tmp/trained-owner-cli-reference-followup.json')
write_new(cli_path, {
    'surfaceConfirmedByUser': 'terminal Codex CLI',
    'existingRedactedDiagnostic': str(diagnostic_source),
    'existingRedactedDiagnosticSha256': digest(diagnostic_source),
    'existingDiagnosticEvent': diagnostic['currentThreadTransportEvent'],
    'newLogScanExecuted': False,
    'exactReportedDecodeErrorCauseEstablished': False,
    'memoryEstablishedAsCause': False,
    'cliSessionRestarted': False,
    'configurationAuthenticationOrCliChanged': False,
    'officialDiagnosticsSectionFetched': 'https://learn.chatgpt.com/docs/config-file/environment-variables#diagnostics',
    'officialResumeSectionFetched': 'https://learn.chatgpt.com/docs/developer-commands#codex-resume',
    'recoveryCommand': 'codex resume',
    'recoveryExecuted': False
})

inherited_matched = provenance.pop('matchedFrozenParentCheckpointSha256')
assert inherited_matched == provenance['individualCriticParentTraining']['checkpointSha256']
provenance['inheritedMatchedParentControl'] = {
    'checkpointSha256': inherited_matched,
    'sourceArchiveManifest': provenance['overviewCriticParentTraining']['sourceArchiveManifest'],
    'sourceArchiveManifestSha256': provenance['overviewCriticParentTraining']['sourceArchiveManifestSha256'],
    'scope': 'earlier matched frozen/trained 289-to-288 policies; not a current 288-to-286 matched-parent experiment'
}
provenance['trainingValidationEvidenceScope'] = 'inherited previous single-owner-learning optimizer replay; zero new training updates here'
provenance['neuralOwnerBatchInferenceProposalsArchived'] = 512
provenance['jointInteractionCriticTrainedInLatestArchive'] = False
provenance['coordinatedNetworkBatchesArchivedScope'] = 'optimizer batches only; excludes 512 multi-owner inference proposals'
provenance['newCaptainInferenceMeasurementsNotUsedForTrainingScope'] = 'Native TRY proposal attempts including hard, spacing, projection and frame rejections; not 2771 fully evaluated legal reward labels'
provenance['currentMatchedFrozenParentAblationExecuted'] = False
provenance['browserCheckAttempted'] = True
provenance['browserFailureEvidence'] = str(browser_path)
audit['browserCheckAttempted'] = True
audit['browserFailureEvidence'] = str(browser_path)

reconciliation = {
    'scope': 'metadata only; model-selected layout bytes and base manifest remain unchanged',
    'candidateSha256': digest(layout),
    'baseManifestSha256': digest(manifest),
    'provenanceBeforeSha256': digest(prov_before),
    'auditBeforeSha256': digest(audit_before),
    'inheritedMatchedParentScopedToPriorArchive': True,
    'latestTrainingUpdates': 0,
    'batchInferenceProposals': 512,
    'jointInteractionCriticTrained': False,
    'browserFailureRecord': str(browser_path),
    'browserFailureRecordSha256': digest(browser_path),
    'reconcilerSha256': digest(__file__)
}
reconciliation_path = supplement / 'metadata-reconciliation.json'
write_new(reconciliation_path, reconciliation)
provenance['latestPromotionMetadataReconciliationRecord'] = str(reconciliation_path)
provenance['latestPromotionMetadataReconciliationRecordSha256'] = digest(reconciliation_path)
for path, value in ((audit_path, audit), (prov_path, provenance)):
    temporary = path.with_name(path.name + '.reconciling')
    write_new(temporary, value)
    os.replace(temporary, path)
assert digest(layout) == promotion['candidateSha256']
assert digest(manifest) == seal['manifestSha256']

result = {
    'status': 'pass',
    'candidateSha256': digest(layout),
    'archiveManifestSha256': seal['manifestSha256'],
    'promotedBytesIdenticalToProductAuditedCandidate': True,
    'overviewVisual': 286,
    'individualVisual': 1963,
    'individualViewExactlyPreservedAsJsonData': True,
    'overviewChangedModels': changed,
    'all1244SizesPreserved': True,
    'all1727RelationshipIdsPreserved': True,
    'overviewCoordinatesAndRoutesExactlyMatchModelAppliedProduct': True,
    'newTrainingUpdates': 0,
    'inheritedTrainingAndMatchedControlScopeVerified': True,
    'neuralProposals': 2771,
    'completedStages': 8,
    'cachedModelInputStateNativeControls': 82,
    'allCachedModelInputsNativeVerifiedExactly': True,
    'resourceLimitMiB': 128,
    'resourceLimitRaised': False,
    'productAuditPeakMiB': 114.1,
    'browserCheckAttempted': True,
    'browserVerified': False,
    'targetMet': False,
    'verifierSha256': digest(__file__)
}
output = Path('.tmp/trained-owner-final-state-verification.json')
write_new(output, result)
files = [
    layout, audit_path, prov_path, Path('context.md'), Path('data/erd-poc/candidates/README.md'),
    output, browser_path, cli_path, diagnostic_source,
    Path('.tmp/trained-owner-archive-seal-receipt.json'),
    Path('.tmp/trained-owner-archive-independent-verification.json'),
    Path('.tmp/trained-owner-promotion-receipt.json'),
    Path('.tmp/trained-owner-archive-seal.guard.log'),
    Path('.tmp/trained-owner-archive-verification.guard.log'),
    Path('.tmp/trained-owner-promotion.guard.log'),
    Path('.tmp/promote_trained_owner_policies.py'), Path(__file__)
]
bindings = []
for source in files:
    destination = supplement / source.name
    assert not destination.exists(), str(destination)
    shutil.copyfile(source, destination)
    assert digest(source) == digest(destination)
    bindings.append({'source': str(source), 'archive': str(destination),
                     'sha256': digest(destination), 'bytes': destination.stat().st_size})
for source in (prov_before, audit_before, reconciliation_path):
    bindings.append({'source': str(source), 'archive': str(source),
                     'sha256': digest(source), 'bytes': source.stat().st_size})
write_new(supplement / 'artifact-record.json', {
    'baseManifest': str(manifest),
    'baseManifestSha256': seal['manifestSha256'],
    'baseArchiveIndependentRestoreStatus': 'pass',
    'supplementScope': 'post-promotion state, inherited metadata scope, documentation, receipts, Browser failure and guard telemetry; base manifest remains immutable',
    'finalVerification': result,
    'files': bindings,
    'allSupplementBytesCopyHashVerified': True
})
print(json.dumps(result), flush=True)
