"""Same trained weights, two training-only relaxations; hard outputs unchanged."""
import hashlib,json,shutil,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_relaxed_slot_policy import RelaxedSlotAnchorPolicy
from run_joint_neural_layout import action_text
base=Path(__file__).parent;out=base/'slot-relaxation-gap2';out.mkdir(exist_ok=False)
digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
source=Path('scripts/erd-poc/joint_relaxed_slot_policy.py')
shutil.copyfile(source,out/source.name);shutil.copyfile(__file__,out/Path(__file__).name)
report={'sourceSha256':digest(source),'newTraining':False,'newNativeMeasurements':0,
        'diagnosticBufferConversionOnly':True,'hardInferenceUnchanged':True,'views':{}}
for view in ['individual','overview']:
 directory=base/(view+'-relaxed-free1')
 row=json.loads((directory/'joint-batches.jsonl').read_text().splitlines()[-1])
 assert digest(row['checkpoint'])==row['checkpointSha256']
 model=RelaxedSlotAnchorPolicy.load(row['checkpoint']);features=np.load(directory/'joint-input-features.npy')
 hard=model.forward(features)[0]
 assert hashlib.sha256(action_text(hard).encode()).hexdigest()==row['actionSha256']
 results=[]
 for version in [1,2]:
  model.relaxed_slots=np.array(version,dtype=np.int32)
  np.testing.assert_array_equal(model.forward(features)[0],hard)
  for temperature in [.1,.05]:
   soft,cache=model.forward_relaxed(features,temperature)
   delta=soft[:len(features)]-hard[:len(features)]
   results.append({'version':version,'temperature':temperature,'maximumNodePositionGap':float(abs(delta).max()),
       'differentPhysicalCards':int(np.count_nonzero(np.any(abs(delta)>1e-6,axis=1))),**model.relaxation_report})
   del cache
 report['views'][view]={'checkpointSha256':row['checkpointSha256'],'results':results}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
