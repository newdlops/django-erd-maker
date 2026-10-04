import hashlib,json,shutil,subprocess,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_separation_policy import SeparationDagPolicy
from joint_neural_ports import JointPortPolicy
from joint_grouped_routes import read_pairs
from run_joint_neural_layout import action_text
base=Path(__file__).parent;out=base/'separation-node-validation';out.mkdir(exist_ok=False)
source=base/'separation-dag-validation2/individual';binary=base/'joint-batch-environment-v7'
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest();report={'nativeBinarySha256':digest(binary)}
child=subprocess.Popen(list(map(str,[binary,'--directory',source,'--out',out/'unused'])),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    initial=json.loads(child.stdout.readline());assert initial['visual']==1964
    features=np.array([row['features'] for row in initial['nodes']]);positions=read_pairs(source/'positions.tsv');sizes=read_pairs(source/'nodes.tsv')
    ids=list(sizes);positions=np.array([positions[key] for key in ids]);sizes=np.array([sizes[key] for key in ids])
    model=SeparationDagPolicy(features,positions,sizes,4096,12431);rng=np.random.default_rng(12473)
    np.testing.assert_array_equal(model.forward(features)[0],0.)
    batches=[]
    for scale in [0.,.00001,.003,.015,.07]:
        model.p['wo']=rng.normal(0,scale,model.p['wo'].shape);action,_=model.forward(features)
        physical=positions+action
        for n in range(len(positions)-1):
            gap=abs(physical[n+1:]-physical[n])-(sizes[n+1:]+sizes[n])/2
            assert ((gap[:,0]>=55.99)|(gap[:,1]>=41.99)).all()
        child.stdin.write(action_text(action).replace('TRY ','MEASURE ',1)+'\n');child.stdin.flush()
        result=json.loads(child.stdout.readline());assert result['measureOnly'] and result['reason'] not in ('spacing','frame')
        batches.append(result)
    report['nativeNoncommittingBatches']=batches
    model.p['wo']=rng.normal(0,.002,model.p['wo'].shape)
    action,cache=model.forward(features,False);weight=rng.normal(size=action.shape)
    gradient=model.backward(cache,weight);checked=0;errors=[]
    for key in model.keys:
        for flat in np.argsort(abs(gradient[key]).ravel())[-3:]:
            index=np.unravel_index(flat,model.p[key].shape);value=model.p[key][index];step=1e-8
            model.p[key][index]=value+step;hi=np.sum(model.forward(features,False)[0]*weight)
            model.p[key][index]=value-step;lo=np.sum(model.forward(features,False)[0]*weight)
            model.p[key][index]=value;numeric=(hi-lo)/(2*step)
            np.testing.assert_allclose(gradient[key][index],numeric,rtol=3e-4,atol=.004)
            errors.append(float(abs(gradient[key][index]-numeric)));checked+=1
    checkpoint=out/'fixture-only.npz';model.save(checkpoint,{'fixtureOnly':True})
    np.testing.assert_array_equal(model.forward(features)[0],SeparationDagPolicy.load(checkpoint).forward(features)[0])
    child.stdin.write('MEASURE '+' '.join('0' for _ in range(action.size))+'\n');child.stdin.flush()
    assert json.loads(child.stdout.readline())==batches[0]
    child.stdin.write('QUIT\n');child.stdin.flush();assert child.wait(timeout=5)==0
    report.update(surrogateNetworkDerivatives=checked,maximumAbsoluteError=max(errors),frozenRoundtrip=True,sourceUnchanged=True,
        physicalNodes=len(positions),zeroOutputIsCanonicalRepresentation=True,
        sourcePositionsSha256=digest(source/'positions.tsv'))
finally:
    if child.poll() is None:child.kill();child.wait()
replayed={}
for name in ['individual-separation-dag1','overview-separation-dag1','individual-rigid-branch1']:
    directory=base/name;features=np.load(directory/'joint-input-features.npy');count=0
    for line in (directory/'joint-batches.jsonl').open():
        row=json.loads(line);path=Path(row['checkpoint']);assert digest(path)==row['checkpointSha256']
        frozen=JointPortPolicy.load(path)
        assert hashlib.sha256(action_text(frozen.forward(features)[0]).encode()).hexdigest()==row['actionSha256']
        count+=1
    replayed[name]=count
report['priorFrozenBatchesReplayed']=replayed
sources=out/'sources';sources.mkdir();report['implementationHashes']={}
for name in ['joint_separation_policy.py','joint_neural_ports.py','joint_layout_proxy.py','joint_grouped_routes.py','run_joint_neural_layout.py']:
    path=Path('scripts/erd-poc')/name;shutil.copyfile(path,sources/name);report['implementationHashes'][name]=digest(path)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
