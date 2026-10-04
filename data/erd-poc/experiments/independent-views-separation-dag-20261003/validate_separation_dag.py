"""Direct geometry, surrogate-gradient, frozen-load, and native parity checks."""
import hashlib,json,shutil,subprocess,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_separation_policy import separation_graph,decode_separation,backward_separation,SeparationDagPortPolicy
from joint_neural_ports import JointPortPolicy,PerimeterRoutes
from joint_grouped_routes import read_pairs

base=Path(__file__).parent;out=base/'separation-dag-validation';out.mkdir(exist_ok=False)
binary=base/'joint-batch-environment-v7';digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
results={'nativeBinarySha256':digest(binary),'views':{}}
rng=np.random.default_rng(11781)
def spacing(positions,sizes):
    for i in range(len(positions)-1):
        gap=abs(positions[i+1:]-positions[i])-(sizes[i+1:]+sizes[i])/2
        assert ((gap[:,0]>=55.99)|(gap[:,1]>=41.99)).all(), ('spacing',i)
def frame(positions,sizes,delta):
    low=(positions-sizes/2).min(0);high=(positions+sizes/2).max(0)
    assert (positions+delta-sizes/2>=low-1e-7).all() and (positions+delta+sizes/2<=high+1e-7).all()

checked=0
for seed in range(12):
    points=np.array([[400*x,330*y] for y in range(4) for x in range(5)],dtype=float)
    points+=rng.uniform(-17,17,points.shape);sizes=rng.uniform(90,160,points.shape)
    graph=separation_graph(points,sizes,250)
    np.testing.assert_array_equal(decode_separation(np.zeros_like(points),graph)[0],0.)
    for k in range(16):
        fraction=rng.uniform(-1,1,points.shape)
        if k<2:fraction[:]=[-1.,1.][k]
        delta,_=decode_separation(fraction,graph);spacing(points+delta,sizes);frame(points,sizes,delta);checked+=1
    fraction=rng.uniform(-.9,.9,points.shape);weight=rng.normal(size=points.shape)
    _,cache=decode_separation(fraction,graph,False);grad=backward_separation(weight,cache,graph)
    for index in np.argsort(abs(grad).ravel())[-6:]:
        key=np.unravel_index(index,fraction.shape);step=1e-6
        hi=fraction.copy();lo=fraction.copy();hi[key]+=step;lo[key]-=step
        derivative=np.sum((decode_separation(hi,graph,False)[0]-decode_separation(lo,graph,False)[0])*weight)/(2*step)
        np.testing.assert_allclose(grad[key],derivative,rtol=1e-5,atol=1e-5)
# Opposite-sign half-ties across a nearly tight inequality.
points=np.array([[0.,0.],[256.011,0.]]);sizes=np.full((2,2),200.)
graph=separation_graph(points,sizes,1.)
delta,_=decode_separation(np.array([[.75,0.],[-1.,0.]]),graph)
spacing(points+delta,sizes);np.testing.assert_allclose(delta[:,0],[.02,0],rtol=0,atol=1e-12)
results['fixtures']={'geometryCases':checked,'continuousDecoderDerivatives':72,'halfTieConstraint':True,'zeroIdentity':True}
print(json.dumps(results['fixtures']),flush=True)

for view in ['individual','overview']:
    directory=out/view;directory.mkdir();previous=base/(view+'-bounded-trained2')
    if view=='individual':
        source=previous/'candidate.individual.layout.json';expected=1964
        for name,old in [('nodes.tsv','individual.nodes.tsv'),('edges.tsv','individual.edges.tsv'),
                         ('positions.tsv','learned.tsv.individual'),('routes.tsv','learned.tsv.individual.routes.tsv')]:
            for prefix in ['','individual.']:shutil.copyfile(previous/old,directory/(prefix+name))
        for name,target in [('nodes.tsv','components.tsv'),('edges.tsv','groups.tsv')]:
            ids=[line.split('\t')[0] for line in (directory/name).read_text().splitlines()]
            (directory/target).write_text(''.join(f'{node}\t{node}\n' for node in ids))
    else:
        source=previous/'candidate.layout.json';expected=289;payload='data/erd-poc/recovered/captain-2026-09-15-payload.json'
        for name,command in [('export',['node','scripts/erd-poc/probe_overview_boundary.cjs','export',source,payload,directory,'--max-routes','256','--with-individual']),
                             ('components',['node','scripts/erd-poc/export_learned_components.cjs',source,payload,directory])]:
            with (directory/(name+'.stdout')).open('x') as stdout,(directory/(name+'.stderr')).open('x') as stderr:
                subprocess.run(list(map(str,command)),check=True,stdout=stdout,stderr=stderr)
    with (directory/'native.stderr').open('x') as log:
        child=subprocess.Popen(list(map(str,[binary,'--directory',directory,'--out',directory/'unused',
            '--overview-only',int(view=='overview'),'--neural-perimeter-ports','1'])),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True)
        try:
            observation=child.stdout.readline();ready=json.loads(observation);assert ready['visual']==expected
            (directory/'joint-observations.json').write_text(observation)
            provider=PerimeterRoutes(directory);ids=provider.physical_ids
            positions=read_pairs(directory/'positions.tsv');sizes=read_pairs(directory/'nodes.tsv')
            positions=np.array([positions[key] for key in ids]);sizes=np.array([sizes[key] for key in ids])
            features=np.array([row['features'] for row in ready['nodes']])
            model=SeparationDagPortPolicy(features,positions,sizes,2048.,11931,provider,.125)
            action,_=model.forward(features);np.testing.assert_array_equal(action,0.)
            provider.set_actions(action[len(positions):],quantized=True,positions=positions)
            actual=provider.full_provider.forward(positions[provider.owner]+provider.offsets,provider.sizes)[0]
            np.testing.assert_allclose(actual,provider.original_ports,rtol=0,atol=1e-8)
            outcomes=[]
            for scale in [0.,.000001,.002,.01,.05,.2]:
                model.p['wo']=rng.normal(0,scale,model.p['wo'].shape);model.p['ewo']=rng.normal(0,scale,model.p['ewo'].shape)
                action,_=model.forward(features);delta=action[:len(positions)]
                spacing(positions+delta,sizes);frame(positions,sizes,delta)
                spacing((positions+delta)[provider.owner]+provider.offsets,provider.sizes)
                child.stdin.write('MEASURE '+' '.join(f'{v:.12g}' for v in action.ravel())+'\n');child.stdin.flush()
                reply=json.loads(child.stdout.readline());assert reply['measureOnly'] and reply['reason'] not in ('spacing','frame')
                if scale==0:assert reply['legal'] and reply['visual']==expected
                outcomes.append(reply)
            # Differentiate the continuous surrogate, not the discrete forward map.
            model.p['wo']=rng.normal(0,.002,model.p['wo'].shape);model.p['ewo']=rng.normal(0,.002,model.p['ewo'].shape)
            action,cache=model.forward(features,False);weights=rng.normal(size=action.shape)
            gradients=model.backward(cache,weights[:len(positions)],weights[len(positions):]);derivatives=0;max_error=0.
            for key in model.keys:
                for flat in np.argsort(abs(gradients[key]).ravel())[-3:]:
                    index=np.unravel_index(flat,model.p[key].shape);value=model.p[key][index];step=1e-6
                    model.p[key][index]=value+step;hi=np.sum(model.forward(features,False)[0]*weights)
                    model.p[key][index]=value-step;lo=np.sum(model.forward(features,False)[0]*weights)
                    model.p[key][index]=value;numeric=(hi-lo)/(2*step);analytic=gradients[key][index]
                    np.testing.assert_allclose(analytic,numeric,rtol=3e-4,atol=.004)
                    max_error=max(max_error,float(abs(analytic-numeric)));derivatives+=1
            checkpoint=directory/'fixture-only.npz';model.save(checkpoint,{'fixtureOnly':True,'sourceSha256':digest(source)})
            frozen=JointPortPolicy.load(checkpoint);assert isinstance(frozen,SeparationDagPortPolicy)
            np.testing.assert_array_equal(model.forward(features)[0],frozen.forward(features)[0])
            child.stdin.write('MEASURE '+' '.join('0' for _ in range(action.size))+'\n');child.stdin.flush()
            unchanged=json.loads(child.stdout.readline());assert unchanged['legal'] and unchanged['visual']==expected
            child.stdin.write('QUIT\n');child.stdin.flush();assert child.wait(timeout=5)==0
            results['views'][view]={'sourceSha256':digest(source),'sourceVisual':expected,'nodes':len(positions),
                'storedConstraints':sum(len(getattr(model,'dag_'+a+'_parents')) for a in ['x','y']),
                'zeroOutputEndpoints':len(provider.original_ports)*2,'fullCardsContained':True,
                'nativeNoncommittingBatches':outcomes,'surrogateNetworkDerivatives':derivatives,
                'maximumDerivativeAbsoluteError':max_error,'frozenRoundtrip':True,'sourceUnchanged':True}
            print(json.dumps({view:results['views'][view]}),flush=True)
        finally:
            if child.poll() is None:child.kill();child.wait()
sources=out/'sources';sources.mkdir();results['implementationHashes']={}
for name in ['joint_separation_policy.py','joint_neural_ports.py','joint_layout_proxy.py','joint_grouped_routes.py','run_joint_neural_layout.py']:
    path=Path('scripts/erd-poc')/name;shutil.copyfile(path,sources/name);results['implementationHashes'][name]=digest(path)
(out/'report.json').write_text(json.dumps(results,indent=2)+'\n')
