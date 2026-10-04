"""Compare smooth training scores with already validated frozen geometry."""
import hashlib,json,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_neural_ports import JointPortPolicy,PerimeterRoutes
from joint_grouped_routes import read_pairs
from joint_active_proxy import ActiveLayoutProxy
from run_joint_neural_layout import action_text

base=Path(__file__).parent;report={'views':{},'newCandidateCreated':False,'nativeMeasurementsReused':True}
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for view in ['individual','overview']:
    d=base/(view+'-anchor-ray1');workflow=json.loads((d/('workflow.audit.json' if view=='individual' else 'workflow.json')).read_text())
    provider=PerimeterRoutes(d);ids=provider.physical_ids;index={k:i for i,k in enumerate(ids)}
    pos=read_pairs(d/'positions.tsv');size=read_pairs(d/'nodes.tsv')
    p=np.array([pos[k] for k in ids]);s=np.array([size[k] for k in ids])
    edges=np.array([[index[a],index[b]] for _,a,b in (l.split('\t') for l in (d/'edges.tsv').read_text().splitlines())])
    features=np.load(d/'joint-input-features.npy');assert digest(d/'joint-input-features.npy')==workflow['featureMatrixSha256']
    visual=ActiveLayoutProxy(p,s,edges,20.,provider);visual.spacing_padding=0.
    cases=[{'kind':'source','nativeVisual':workflow['sourceVisual'],'action':np.zeros((len(p)+len(provider.full_edges),2))}]
    rows=[json.loads(l) for l in (d/'joint-batches.jsonl').open()]
    legal=sorted([r for r in rows if r['result']['legal']],key=lambda r:r['result']['visual'])
    for row in legal[:3]:
        cp=Path(row['checkpoint']);assert digest(cp)==row['checkpointSha256']
        action=JointPortPolicy.load(cp).forward(features)[0]
        assert hashlib.sha256(action_text(action).encode()).hexdigest()==row['actionSha256']
        cases.append({'kind':'frozen-legal','checkpoint':str(cp),'checkpointSha256':row['checkpointSha256'],
            'nativeVisual':row['result']['visual'],'action':action})
    values=[]
    for case in cases:
        action=case.pop('action');physical=p+action[:len(p)]
        offsets=np.array([[float(f'{v:.12g}') for v in r] for r in action[len(p):]])
        provider.set_actions(offsets,quantized=True,positions=physical)
        scores=[]
        for t in [1.,.25,.05,.01]:
            loss,_=visual.loss(physical,t);binary=loss['binaryCrossings']+loss['binaryCardHits']
            assert binary==case['nativeVisual'],(view,case,binary)
            soft=loss['softCrossingCount']+loss['softCardHitCount']
            scores.append({'temperature':t,'softVisual':soft,'binaryVisual':binary,'bias':soft-binary})
        values.append({**case,'scores':scores})
    report['views'][view]={'sourceSha256':workflow['sourceSha256'],'legalBatches':len(legal),'cases':values}
with (base/'anchor-temperature-calibration.json').open('x') as output:output.write(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
