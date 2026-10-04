"""Validate the new canonical overview DAG and grouped loss integration."""
import hashlib,json,shutil,subprocess,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_separation_policy import SeparationDagPolicy
from joint_grouped_routes import GroupedRoutes,read_pairs
from joint_active_proxy import ActiveLayoutProxy
from run_joint_neural_layout import action_text

base=Path(__file__).parent;out=base/'separation-overview-validation';out.mkdir(exist_ok=False)
source=base/'separation-dag-validation2/overview';binary=base/'joint-batch-environment-v7'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'nativeBinarySha256':digest(binary),'view':'overview','fixturesOnly':True}
child=None
try:
    child=subprocess.Popen(list(map(str,[binary,'--directory',source,'--out',out/'unused','--overview-only','1'])),
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    initial=json.loads(child.stdout.readline());assert initial['visual']==289
    features=np.array([r['features'] for r in initial['nodes']]);provider=GroupedRoutes(source)
    ids=provider.physical_ids;by_id={k:i for i,k in enumerate(ids)}
    positions=read_pairs(source/'positions.tsv');sizes=read_pairs(source/'nodes.tsv')
    positions=np.array([positions[k] for k in ids]);sizes=np.array([sizes[k] for k in ids])
    assert (abs(provider.offsets)+provider.sizes/2<=sizes[provider.owner]/2+1e-7).all()
    edges=np.array([[by_id[r[1]],by_id[r[2]]] for r in (s.split('\t') for s in (source/'edges.tsv').read_text().splitlines())])
    model=SeparationDagPolicy(features,positions,sizes,2048,12691);rng=np.random.default_rng(12701)
    np.testing.assert_array_equal(model.forward(features)[0],0.)
    def spacing(p,s):
        for n in range(len(p)-1):
            gap=abs(p[n+1:]-p[n])-(s[n+1:]+s[n])/2
            assert ((gap[:,0]>=55.99)|(gap[:,1]>=41.99)).all()
    batches=[]
    for scale in [0.,.00001,.003,.015,.07]:
        model.p['wo']=rng.normal(0,scale,model.p['wo'].shape);action,_=model.forward(features)
        physical=positions+action;spacing(physical,sizes)
        spacing(physical[provider.owner]+provider.offsets,provider.sizes)
        assert (physical-sizes/2>=(positions-sizes/2).min(0)-1e-7).all()
        assert (physical+sizes/2<=(positions+sizes/2).max(0)+1e-7).all()
        child.stdin.write(action_text(action).replace('TRY ','MEASURE ',1)+'\n');child.stdin.flush()
        result=json.loads(child.stdout.readline());assert result['measureOnly'] and result['reason'] not in ('spacing','frame')
        batches.append(result)
    assert batches[0]['legal'] and batches[0]['visual']==365
    report['nativeNoncommittingBatches']=batches
    model.p['wo']=rng.normal(0,.002,model.p['wo'].shape)
    proxy=ActiveLayoutProxy(positions,sizes,edges,20.,provider);proxy.spacing_padding=0.
    action,cache=model.forward(features,False);loss,gradient=proxy.loss(positions+action,4.)
    grads=model.backward(cache,gradient);errors=[];steps=[1e-7,1e-8]
    for key in model.keys:
        for flat in np.argsort(abs(grads[key]).ravel())[-2:]:
            index=np.unravel_index(flat,model.p[key].shape);value=model.p[key][index];pair=[]
            for step in steps:
                model.p[key][index]=value+step;hi=proxy.loss(positions+model.forward(features,False)[0],4.)[0]['total']
                model.p[key][index]=value-step;lo=proxy.loss(positions+model.forward(features,False)[0],4.)[0]['total']
                model.p[key][index]=value;numeric=(hi-lo)/(2*step)
                pair.append(float(abs(grads[key][index]-numeric)))
            np.testing.assert_allclose(grads[key][index],numeric,rtol=3e-4,atol=.004,err_msg=f'{key} {index}')
            assert pair[1]<=pair[0]+.004
            errors.append(pair[1])
    checkpoint=out/'fixture-only.npz';model.save(checkpoint,{'fixtureOnly':True})
    np.testing.assert_array_equal(model.forward(features)[0],SeparationDagPolicy.load(checkpoint).forward(features)[0])
    child.stdin.write('MEASURE '+' '.join('0' for _ in range(action.size))+'\n');child.stdin.flush()
    assert json.loads(child.stdout.readline())==batches[0]
    child.stdin.write('QUIT\n');child.stdin.flush();assert child.wait(timeout=5)==0
    missing_grouped=subprocess.run([sys.executable,'scripts/erd-poc/run_joint_neural_layout.py',
        '--previous',str(base/'overview-bounded-trained2'),'--out',str(out/'must-not-exist'),
        '--environment',str(binary),'--payload','data/erd-poc/recovered/captain-2026-09-15-payload.json',
        '--view','overview','--separation-dag'],capture_output=True,text=True)
    assert missing_grouped.returncode==2 and 'grouped overview routes' in missing_grouped.stderr
    assert not (out/'must-not-exist').exists()
    report.update(continuousEndToEndDerivatives=len(errors),maximumAbsoluteError=max(errors),finiteDifferenceSteps=steps,
        frozenRoundtrip=True,sourceUnchanged=True,fullCardsContained=True,missingGroupedLossRejected=True,
        physicalNodes=len(positions),fullNodes=len(provider.owner),canonicalSourceVisual=365,originalSourceVisual=289,
        nodeQuantizationDerivative='straight-through estimator; derivatives checked only on continuous surrogate')
    (out/'sources').mkdir();report['implementationHashes']={}
    for name in ['joint_separation_policy.py','joint_grouped_routes.py','joint_layout_proxy.py','joint_active_proxy.py','run_joint_neural_layout.py']:
        p=Path('scripts/erd-poc')/name;shutil.copyfile(p,out/'sources'/name);report['implementationHashes'][name]=digest(p)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
except Exception as e:
    (out/'failure.json').write_text(json.dumps({'type':type(e).__name__,'message':str(e),'partialReport':report},indent=2)+'\n')
    raise
finally:
    if child is not None and child.poll() is None:child.kill();child.wait()
