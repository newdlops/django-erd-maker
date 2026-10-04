import hashlib
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from joint_layout_proxy import JointPolicy
from run_joint_neural_layout import digest,action_text,verify_geometry

root=Path('.tmp/visualcross-ml-150-750-20261003/overview-joint1')
policy=json.loads((root/'learned.tsv.policy.json').read_text())
assert digest(root/'joint-observations.json')==policy['observationsSha256']
assert digest(root/'joint-batches.jsonl')==policy['actionsSha256']
observations=json.loads((root/'joint-observations.json').read_text())
features=np.array([row['features'] for row in observations['nodes']])
proposals=[json.loads(line) for line in (root/'joint-batches.jsonl').read_text().splitlines()]
for proposal in proposals:
    assert digest(proposal['checkpoint'])==proposal['checkpointSha256']
    frozen=JointPolicy.load(proposal['checkpoint'])
    assert hashlib.sha256(action_text(frozen.forward(features)[0]).encode()).hexdigest()==proposal['actionSha256']
checkpoint=Path(policy['winningCheckpoint']);assert digest(checkpoint)==policy['checkpointSha256']
with np.load(checkpoint) as weights:report=json.loads(str(weights['metadata']))
for name,value in report['inputHashes'].items():assert digest(root/name)==value
verify_geometry(root,root/'learned.tsv',JointPolicy.load(checkpoint).forward(features)[0])
report.update(frozenNetworkBatchesReplayed=len(proposals),frozenWinningGeometryReplayed=True,
              unchangedSourceVerified=False,bestCheckpoint=str(checkpoint),bestCheckpointSha256=digest(checkpoint),
              iterations=max(p['iteration'] for p in proposals),neuralChecksPassed=True,neuralSeconds=None,
              browserVerified=False,recoveredAfterMemoryGuard=True,auditMemoryFailurePeakMiB=262.3,
              history=[{'iteration':p['iteration'],'geometry':p['result']} for p in proposals],
              proxyHistoryUnavailableAfterRecovery=True,
              verifiedOutputHashes={name:digest(root/name) for name in ['learned.tsv','learned.tsv.individual','learned.tsv.routes.tsv',
                'learned.tsv.individual.routes.tsv','learned.tsv.stats.json','learned.tsv.policy.json']})
with (root/'joint-worker-result.json').open('x') as out:out.write(json.dumps(report,indent=2)+'\n')
print(json.dumps({'recovered':True,'frozenBatchesReplayed':len(proposals),'winningGeometryReplayed':True}))
