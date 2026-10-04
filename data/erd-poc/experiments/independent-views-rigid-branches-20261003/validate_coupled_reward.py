"""Check coupled learning and replay pre-extension Captain reward traces."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import numpy as np

sys.path.insert(0,'scripts/erd-poc')
from joint_neural_ports import JointPortPolicy
from joint_reward_training import self_test,verify_trace,head_hash
from run_joint_neural_layout import action_text

root=Path(__file__).parent
log=io.StringIO()
with contextlib.redirect_stdout(log):
    self_test()
    self_test(coupled=True)
fixtures=list(map(json.loads,log.getvalue().splitlines()))
legacy=[]
for name in ['individual-patch-reward1','overview-patch-reward1']:
    directory=root/name
    report=json.loads((directory/'joint-worker-result.json').read_text())
    model=JointPortPolicy.load(directory/'reward-initial-policy.npz')
    features=np.load(directory/'joint-input-features.npy')
    proposals=list(map(json.loads,(directory/'joint-batches.jsonl').read_text().splitlines()))
    expected={row['iteration']:head_hash(JointPortPolicy.load(row['checkpoint']).p['wo']) for row in proposals}
    checked=verify_trace(model,features,map(json.loads,(directory/'reward-measurements.jsonl').read_text().splitlines()),action_text,expected)
    assert checked==report['rewardMeasurementsReplayed']
    final=JointPortPolicy.load(proposals[-1]['checkpoint'])
    for key in model.p:np.testing.assert_array_equal(model.p[key],final.p[key])
    legacy.append({'stage':name,'measurementsReplayed':checked,'adamUpdatesReplayed':report['rewardAdamUpdatesReplayed'],
                   'allWeightsMatchFinalCheckpoint':True})
names=['joint_reward_training.py','run_joint_neural_layout.py','joint_neural_ports.py']
snapshots=root/'coupled-reward-validation-sources'
snapshots.mkdir(exist_ok=False)
hashes={}
for name in names:
    path=Path('scripts/erd-poc')/name
    hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
    shutil.copyfile(path,snapshots/name)
result={'fixtures':fixtures,'legacyCaptainReplay':legacy,'allChecksPassed':True,'implementationHashes':hashes}
(root/'coupled-reward-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
