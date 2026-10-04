"""Read-only step-size diagnosis for the two failed derivative comparisons."""
import hashlib,json,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_anchor_ray_policy import AnchorRayPolicy
from joint_neural_ports import PerimeterRoutes
from joint_grouped_routes import read_pairs
from joint_active_proxy import ActiveLayoutProxy
from joint_hard_geometry import FullHardGeometry

base=Path(__file__).parent;directory=base/'separation-dag-validation2/individual'
provider=PerimeterRoutes(directory);ids=provider.physical_ids;index={k:i for i,k in enumerate(ids)}
pos=read_pairs(directory/'positions.tsv');size=read_pairs(directory/'nodes.tsv')
p=np.array([pos[k] for k in ids]);s=np.array([size[k] for k in ids])
edges=np.array([[index[a],index[b]] for _,a,b in (l.split('\t') for l in (directory/'edges.tsv').read_text().splitlines())])
observations=json.loads((base/'anchor-ray-validation2/individual/observations.json').read_text())
features=np.array([r['features'] for r in observations['nodes']])
model=AnchorRayPolicy(features,p,s,2048.,12931,provider,.125)
rng=np.random.default_rng(12911);rng.normal(size=(96,2))
for scale in [0.,.00001,.0003,.003,.02,.1]:model.p['wo']=rng.normal(0,scale,model.p['wo'].shape)
model.p['wo']=rng.normal(0,.0003,model.p['wo'].shape)
action,cache=model.forward(features,False);rng.normal(size=action.shape)
phase_weight=rng.normal(size=action[len(p):].shape)
phase_grads=model.backward(cache,np.zeros_like(p),phase_weight)
visual=ActiveLayoutProxy(p,s,edges,20.,provider);visual.spacing_padding=0.
hard=FullHardGeometry(provider,p,s,edges,2048.,1000.)
def full_loss():
    action,cache=model.forward(features,False);positions=p+action[:len(p)]
    provider.set_actions(action[len(p):],positions=positions)
    values,g=visual.loss(positions,4.);hv,hg=hard.loss(positions)
    return values['total']+hv['total'],model.backward(cache,g+hg,provider.action_gradient())
total,grads=full_loss();report={'fullLoss':total,'probes':[],'implementationSha256':hashlib.sha256(Path('scripts/erd-poc/joint_anchor_ray_policy.py').read_bytes()).hexdigest()}
for kind,key,index in [('phase','wo',(8,1)),('full','w1',(3,47)),('full','wo',(8,1))]:
    f=(lambda:np.sum(model.forward(features,False)[0][len(p):]*phase_weight)) if kind=='phase' else (lambda:full_loss()[0])
    analytic=float((phase_grads if kind=='phase' else grads)[key][index]);value=model.p[key][index]
    row={'kind':kind,'key':key,'index':list(index),'analytic':analytic,'steps':[]}
    for step in [1e-4,1e-5,1e-6,1e-7,1e-8,1e-9]:
        model.p[key][index]=value+step;hi=f()
        model.p[key][index]=value-step;lo=f()
        model.p[key][index]=value;numeric=float((hi-lo)/(2*step))
        row['steps'].append({'step':step,'numeric':numeric,'absoluteError':abs(numeric-analytic)})
    report['probes'].append(row)
with (base/'anchor-derivative-step-diagnosis.json').open('x') as f:f.write(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
