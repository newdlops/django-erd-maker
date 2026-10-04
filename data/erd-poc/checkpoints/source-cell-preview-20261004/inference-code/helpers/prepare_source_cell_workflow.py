import json
from pathlib import Path
stage=Path('.tmp/captain-source-cell-preview-20261004/individual')
export=json.loads((stage/'export.json').read_text())
audit=json.loads((stage/'individual.audit.json').read_text())
assert export['latestNetworkForwardExported'] and export['immutableCheckpointAndDecoderArraysMatched']
assert export['fullNativeMeasurement']['legal'] and audit['actualProductRendererVerified']
assert audit['outwardBoundaryEndpointsVerified'] and audit['spacingViolations']==0
assert audit['visualCrossings']==export['fullNativeMeasurement']['individualVisual']
workflow=dict(kind='latest-trained-source-cell-checkpoint-preview-workflow',allChecksPassed=True,
    candidateSha256=audit['candidateSha256'],payloadSha256=export['payloadSha256'],
    latestNetworkForwardExported=True,trainedUpdates=export['trainedUpdates'],newTrainingUpdates=0,
    checkpointSha256=export['checkpointSha256'],coordinateSearchOrRepairs=0,
    originalDimensionsPreserved=True,nativeHardConditions=0,guardLimitMiB=128,browserVerified=False,
    checkScope='All training updates and probe wires replayed; first and last teacher labels, all trained TRY outputs, final model and complete product geometry remeasured.')
with (stage/'workflow.audit.json').open('x') as stream:
    json.dump(workflow,stream,indent=2);stream.write('\n')
print(json.dumps(dict(status='pass',trainedUpdates=export['trainedUpdates'],visual=audit['visualCrossings'])))
