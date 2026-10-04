"""Recheck frozen outputs against the actual product's outward contract."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,'scripts/erd-poc')
from joint_neural_ports import PerimeterRoutes,JointPortPolicy
from joint_grouped_routes import read_pairs
from run_joint_neural_layout import action_text

root=Path('.tmp/visualcross-ml-150-750-20261003')
reports=[]
for name in ['individual-node-port1','individual-node-port2']:
    directory=root/name;provider=PerimeterRoutes(directory)
    positions=np.array(list(read_pairs(directory/'positions.tsv').values()))
    features=np.load(directory/'joint-input-features.npy')
    results=[]
    for batch in map(json.loads,(directory/'joint-batches.jsonl').read_text().splitlines()):
        if not batch['result']['legal']:continue
        model=JointPortPolicy.load(batch['checkpoint']);action=model.forward(features)[0]
        command=action_text(action)
        assert hashlib.sha256(command.encode()).hexdigest()==batch['actionSha256']
        action=np.array(list(map(float,command.split()[1:]))).reshape(-1,2)
        delta=action[:len(positions)]
        moved=positions+np.copysign(np.floor(abs(delta)*100+.5),delta)/100
        provider.set_actions(action[len(positions):],quantized=True,positions=moved)
        centers=(moved[provider.owner]+provider.offsets)[provider.full_edges]
        ports=centers+provider.current_offsets;half=provider.endpoint_sizes/2
        bad=0
        for end in range(2):
            p,peer=ports[:,end],ports[:,1-end];low=centers[:,end]-half[:,end];high=centers[:,end]+half[:,end];eps=.011
            outward=((abs(p[:,0]-low[:,0])<=eps)&(peer[:,0]<=p[:,0]+eps)
                |(abs(p[:,0]-high[:,0])<=eps)&(peer[:,0]>=p[:,0]-eps)
                |(abs(p[:,1]-low[:,1])<=eps)&(peer[:,1]<=p[:,1]+eps)
                |(abs(p[:,1]-high[:,1])<=eps)&(peer[:,1]>=p[:,1]-eps))
            valid=outward&(p>=low-eps).all(1)&(p<=high+eps).all(1)&(np.linalg.norm(p-peer,axis=1)>eps)
            bad+=int((~valid).sum())
        results.append({'iteration':batch['iteration'],'visual':batch['result']['visual'],'outwardViolations':bad,
                        'checkpoint':batch['checkpoint'],'actionHashVerified':True})
    good=[r for r in results if r['outwardViolations']==0]
    summary={'stage':name,'oldLegalBatches':len(results),'outwardValidBatches':len(good),
             'best':min(good,key=lambda r:(r['visual'],-r['iteration'])) if good else None,'batches':results}
    reports.append(summary);print(json.dumps({k:v for k,v in summary.items() if k!='batches'}),flush=True)
(root/'node-port-outward-recheck.json').write_text(json.dumps(reports,indent=2)+'\n')
