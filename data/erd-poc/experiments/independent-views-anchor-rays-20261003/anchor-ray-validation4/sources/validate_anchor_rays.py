"""Check frozen interior anchors, coupled gradients, and strict native gates."""
import gc,hashlib,json,shutil,subprocess,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_anchor_ray_policy import AnchorRayPolicy,interior_anchors
from joint_neural_ports import JointPortPolicy,PerimeterRoutes,perimeter_points
from joint_grouped_routes import read_pairs
from joint_active_proxy import ActiveLayoutProxy
from joint_hard_geometry import FullHardGeometry
from run_joint_neural_layout import action_text

base=Path(__file__).parent;out=base/'anchor-ray-validation4';out.mkdir(exist_ok=False)
binary=base/'joint-batch-environment-v7';digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report={'nativeBinarySha256':digest(binary),'views':{},'fixturesOnly':True};child=None
try:
    rng=np.random.default_rng(12911)
    sizes=np.broadcast_to(np.array([120.,80.]),(96,2,2)).copy()
    boundary=perimeter_points(np.linspace(0,1,192,endpoint=False).reshape(96,2),sizes)[0]
    direction=rng.normal(size=(96,2));anchors,reach=interior_anchors(boundary,sizes,direction)
    assert (abs(anchors)<sizes/2).all()
    report['strictInteriorFixtureEndpoints']=192
    for view in ['individual','overview']:
        directory=base/'separation-dag-validation2'/view;child_out=out/view;child_out.mkdir()
        source=base/(view+'-bounded-trained2')/('candidate.individual.layout.json' if view=='individual' else 'candidate.layout.json')
        source_sha=digest(source)
        child=subprocess.Popen(list(map(str,[binary,'--directory',directory,'--out',child_out/'unused',
            '--overview-only',int(view=='overview'),'--neural-perimeter-ports','1'])),
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        observation=child.stdout.readline();initial=json.loads(observation);expected=1964 if view=='individual' else 289
        assert initial['visual']==expected;(child_out/'observations.json').write_text(observation)
        provider=PerimeterRoutes(directory);ids=provider.physical_ids;index={k:i for i,k in enumerate(ids)}
        pos=read_pairs(directory/'positions.tsv');size=read_pairs(directory/'nodes.tsv')
        positions=np.array([pos[k] for k in ids]);sizes=np.array([size[k] for k in ids])
        edges=np.array([[index[s],index[t]] for _,s,t in (l.split('\t') for l in (directory/'edges.tsv').read_text().splitlines())])
        features=np.array([r['features'] for r in initial['nodes']]);model=AnchorRayPolicy(features,positions,sizes,2048.,12931,provider,.125)
        zero,_=model.forward(features);np.testing.assert_array_equal(zero,0.)
        provider.set_actions(zero[len(positions):],quantized=True,positions=positions)
        original=provider.full_provider.forward(positions[provider.owner]+provider.offsets,provider.sizes)[0]
        np.testing.assert_allclose(original,provider.original_ports,atol=1e-8,rtol=0)
        batches=[];pool_comparisons=[]
        for scale in [0.,.00001,.0003,.003,.02,.1]:
            model.p['wo']=rng.normal(0,scale,model.p['wo'].shape);action,_=model.forward(features)
            child.stdin.write(action_text(action).replace('TRY ','MEASURE ',1)+'\n');child.stdin.flush()
            result=json.loads(child.stdout.readline());assert result['measureOnly'] and result['reason'] not in ('spacing','frame')
            batches.append(result)
            flattened=action[len(positions):].ravel()
            for group in np.flatnonzero(model.endpoint_counts>1):
                values=flattened[model.endpoint_groups==group]
                np.testing.assert_array_equal(values,values[0])
            if scale in [.00001,.0003]:
                direction=model.ray_base_direction+action[model.owner_edges[:,1]]-action[model.owner_edges[:,0]]
                raw=action.copy();raw[len(positions):]=np.mod(model.ray_phase(direction)[0]-model.ray_initial_phase+.5,1.)-.5
                child.stdin.write(action_text(raw).replace('TRY ','MEASURE ',1)+'\n');child.stdin.flush()
                unpooled=json.loads(child.stdout.readline());assert unpooled['measureOnly']
                pool_comparisons.append({'fixtureWeightStd':scale,'pooled':result,'unpooled':unpooled})
        assert batches[0]['legal'] and batches[0]['visual']==expected
        view_report={'sourceSha256':source_sha,'sourceVisual':expected,'zeroEndpointsPreserved':len(provider.original_ports)*2,
            'strictInteriorAnchors':int(model.anchor_offsets.size/2),'degenerateSourceRays':int(model.degenerate_source_rays),
            'nativeNoncommittingBatches':batches,'trainableEndpointHead':False}
        view_report.update(sharedEndpointGroups=int(np.count_nonzero(model.endpoint_counts>1)),
            sharedEndpointOutputsEqual=True,poolComparisons=pool_comparisons,anchorInset=float(model.anchor_inset))
        report['views'][view]=view_report
        model.p['wo']=rng.normal(0,.0003,model.p['wo'].shape)
        action,cache=model.forward(features,False);weight=rng.normal(size=action.shape)
        grads=model.backward(cache,weight[:len(positions)],weight[len(positions):])
        direct=[]
        for key in model.keys:
            for flat in np.argsort(abs(grads[key]).ravel())[-2:]:
                idx=np.unravel_index(flat,model.p[key].shape);value=model.p[key][idx];step=1e-8
                model.p[key][idx]=value+step;hi=np.sum(model.forward(features,False)[0]*weight)
                model.p[key][idx]=value-step;lo=np.sum(model.forward(features,False)[0]*weight)
                model.p[key][idx]=value;numeric=(hi-lo)/(2*step)
                np.testing.assert_allclose(grads[key][idx],numeric,rtol=3e-4,atol=.004,err_msg=f'{view} direct {key}{idx}')
                direct.append(float(abs(grads[key][idx]-numeric)))
        phase_weight=rng.normal(size=action[len(positions):].shape)
        phase_grads=model.backward(cache,np.zeros_like(positions),phase_weight);phase_errors=[]
        for key in model.keys:
            for flat in np.argsort(abs(phase_grads[key]).ravel())[-2:]:
                idx=np.unravel_index(flat,model.p[key].shape);value=model.p[key][idx];phase_checks=[]
                for step in [1e-7,1e-8]:
                    model.p[key][idx]=value+step;hi=np.sum(model.forward(features,False)[0][len(positions):]*phase_weight)
                    model.p[key][idx]=value-step;lo=np.sum(model.forward(features,False)[0][len(positions):]*phase_weight)
                    model.p[key][idx]=value;numeric=(hi-lo)/(2*step);phase_checks.append(float(numeric))
                np.testing.assert_allclose(phase_checks[0],phase_checks[1],rtol=3e-4,atol=2e-6)
                np.testing.assert_allclose(phase_grads[key][idx],numeric,rtol=3e-4,atol=2e-6,err_msg=f'{view} phase {key}{idx}')
                phase_errors.append(float(abs(phase_grads[key][idx]-numeric)))
        visual=ActiveLayoutProxy(positions,sizes,edges,20.,provider);visual.spacing_padding=0.
        hard=FullHardGeometry(provider,positions,sizes,edges,2048.,1000.)
        def loss():
            action,cache=model.forward(features,False);p=positions+action[:len(positions)]
            provider.set_actions(action[len(positions):],positions=p)
            values,g=visual.loss(p,4.);hv,hg=hard.loss(p)
            return values['total']+hv['total'],model.backward(cache,g+hg,provider.action_gradient())
        total,grads=loss();combined=[];view_report['fullLossAtDerivativeFixture']=total
        for key in model.keys:
            flat=np.argmax(abs(grads[key]));idx=np.unravel_index(flat,model.p[key].shape);value=model.p[key][idx];errors=[]
            probes=[]
            for step in [1e-5,1e-6]:
                model.p[key][idx]=value+step;hi=loss()[0]
                model.p[key][idx]=value-step;lo=loss()[0]
                model.p[key][idx]=value;numeric=(hi-lo)/(2*step);errors.append(float(abs(grads[key][idx]-numeric)))
                probes.append({'step':step,'numeric':float(numeric),'error':errors[-1]})
            with (child_out/'derivative-probes.jsonl').open('a') as log:
                log.write(json.dumps({'key':key,'index':list(map(int,idx)),'analytic':float(grads[key][idx]),'probes':probes})+'\n')
            np.testing.assert_allclose(grads[key][idx],numeric,rtol=5e-4,atol=.006,err_msg=f'{view} loss {key}{idx}')
            assert errors[1]<=errors[0]+.006,(view,key,idx,errors,grads[key][idx],numeric)
            combined.append(errors[1])
        checkpoint=child_out/'fixture-only.npz';model.save(checkpoint,{'fixtureOnly':True,'sourceSha256':source_sha})
        frozen=JointPortPolicy.load(checkpoint);assert type(frozen) is AnchorRayPolicy
        np.testing.assert_array_equal(frozen.forward(features)[0],model.forward(features)[0])
        child.stdin.write('MEASURE '+' '.join('0' for _ in range(zero.size))+'\n');child.stdin.flush()
        assert json.loads(child.stdout.readline())==batches[0]
        child.stdin.write('QUIT\n');child.stdin.flush();assert child.wait(timeout=5)==0
        assert digest(source)==source_sha
        view_report.update(directSurrogateDerivatives=len(direct),directMaximumAbsoluteError=max(direct),
            fullLossDerivatives=len(combined),fullLossMaximumAbsoluteError=max(combined),frozenRoundtrip=True,
            sourceUnchanged=True,fullLossIncludesVisualHardAndGroupedProjection=True,
            fullLossFiniteDifferenceSteps=[1e-5,1e-6],phaseOnlyFiniteDifferenceSteps=[1e-7,1e-8],
            phaseOnlyDerivatives=len(phase_errors),phaseOnlyMaximumAbsoluteError=max(phase_errors))
        print(json.dumps({view:view_report}),flush=True)
        del model,frozen,visual,hard,provider;gc.collect()
    replayed=0
    for name in ['individual-separation-dag1','overview-separation-dag1','individual-rigid-branch1']:
        d=base/name;row=json.loads((d/'joint-batches.jsonl').read_text().splitlines()[-1])
        cp=Path(row['checkpoint']);assert digest(cp)==row['checkpointSha256']
        action=JointPortPolicy.load(cp).forward(np.load(d/'joint-input-features.npy'))[0]
        assert hashlib.sha256(action_text(action).encode()).hexdigest()==row['actionSha256'];replayed+=1
    report['priorCheckpointDispatchReplays']=replayed
    report['gradientScope']='continuous surrogate; node cent quantization uses straight-through gradients'
    report['nativeHardValidityGuaranteedByDecoder']=False
    (out/'sources').mkdir();report['implementationHashes']={}
    for name in ['joint_anchor_ray_policy.py','joint_separation_policy.py','joint_neural_ports.py','joint_layout_proxy.py',
        'joint_active_proxy.py','joint_hard_geometry.py','run_joint_neural_layout.py']:
        p=Path('scripts/erd-poc')/name;shutil.copyfile(p,out/'sources'/name);report['implementationHashes'][name]=digest(p)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'allChecksPassed':True,'views':len(report['views']),'priorCheckpointDispatchReplays':replayed}),flush=True)
except Exception as e:
    (out/'failure.json').write_text(json.dumps({'type':type(e).__name__,'message':str(e),'partialReport':report},indent=2)+'\n')
    raise
finally:
    if child is not None and child.poll() is None:child.kill();child.wait()
