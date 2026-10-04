"""Measure relaxation/inference disagreement without proposing coordinates."""
import hashlib,json,shutil,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_relaxed_slot_policy import RelaxedSlotAnchorPolicy,relaxed_assignment
from run_joint_neural_layout import action_text
base=Path(__file__).parent;out=base/'slot-relaxation-gap1';out.mkdir(exist_ok=False)
digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
source=Path('scripts/erd-poc/joint_relaxed_slot_policy.py')
shutil.copyfile(source,out/source.name);shutil.copyfile(__file__,out/Path(__file__).name)
report={'sourceSha256':digest(source),'positionsProposed':False,'newNativeMeasurements':0,'views':{},'fixture':[]}
reference=np.linspace(-1,1,7);score=np.array([0.,10.,11.,12.,13.,14.,15.])
for temperature in [.1,.05]:
 p,_=relaxed_assignment(score,reference,temperature,False)
 report['fixture'].append({'temperature':temperature,'argmaxSlots':p.argmax(1).tolist(),
     'hardSlots':np.argsort(np.argsort(score)).tolist(),'maxColumnMassError':float(abs(p.sum(0)-1).max()),
     'maximumHardAssignmentError':float(abs(p-np.eye(len(p))).max())})
for view in ['individual','overview']:
 directory=base/(view+'-relaxed-free1')
 records=[json.loads(line) for line in (directory/'joint-batches.jsonl').open()]
 row=records[-1];assert digest(row['checkpoint'])==row['checkpointSha256']
 model=RelaxedSlotAnchorPolicy.load(row['checkpoint']);features=np.load(directory/'joint-input-features.npy')
 hard=model.forward(features)[0];assert hashlib.sha256(action_text(hard).encode()).hexdigest()==row['actionSha256']
 checks=[]
 for temperature in [.1,.05]:
  soft,cache=model.forward_relaxed(features,temperature)
  delta=soft[:len(features)]-hard[:len(features)]
  checks.append({'temperature':temperature,'maximumNodePositionGap':float(abs(delta).max()),
      'differentPhysicalCards':int(np.count_nonzero(np.any(abs(delta)>1e-6,axis=1))),**model.relaxation_report})
  del cache
 report['views'][view]={'checkpoint':row['checkpoint'],'checkpointSha256':row['checkpointSha256'],
     'nativeHardOutputResult':row['result'],'checks':checks}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
