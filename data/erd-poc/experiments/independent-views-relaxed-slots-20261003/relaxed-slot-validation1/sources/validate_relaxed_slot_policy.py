"""Finite-difference checks for the new training forward; hard outputs replayed."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time
import traceback
sys.path.insert(0,'scripts/erd-poc')
import numpy as np
from joint_relaxed_slot_policy import RelaxedSlotAnchorPolicy,relaxed_assignment,assignment_backward
from joint_slot_anchor_policy import SlotAnchorRayPolicy
from joint_neural_ports import PerimeterRoutes,JointPortPolicy
from joint_active_proxy import ActiveLayoutProxy
from learned_global_replay import pairs
from run_joint_neural_layout import action_text

base=Path(__file__).parent
out=base/'relaxed-slot-validation1';out.mkdir(exist_ok=False)
sources=out/'sources';sources.mkdir()
names=['joint_relaxed_slot_policy.py','joint_slot_anchor_policy.py','joint_slot_policy.py',
       'joint_anchor_ray_policy.py','joint_neural_ports.py','joint_layout_proxy.py','joint_grouped_routes.py',
       'joint_active_proxy.py','run_joint_neural_layout.py']
digest=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
hashes={name:digest(Path('scripts/erd-poc')/name) for name in names}
for name in names:shutil.copyfile(Path('scripts/erd-poc')/name,sources/name)
shutil.copyfile(__file__,sources/Path(__file__).name)
report={'allChecksPassed':False,'implementationHashes':hashes,'views':{},'assignmentChecks':[],
        'hardSortingDerivativeClaimed':False,'newNativeGeometryEvaluations':0}
started=time.monotonic()
try:
    rng=np.random.default_rng(13703)
    for count,temperature in [(3,.2),(7,.7),(13,2.)]:
        reference=np.linspace(-1,1,count);score=reference+rng.normal(0,.05,count)
        gradient=rng.normal(size=(count,count))
        probability,cache=relaxed_assignment(score,reference,temperature)
        analytic=assignment_backward(gradient,cache)
        np.testing.assert_allclose(probability.sum(1),1,atol=2e-14,rtol=0)
        maximum=0.
        for i in range(count):
            hi=score.copy();lo=score.copy();hi[i]+=1e-6;lo[i]-=1e-6
            numeric=np.sum((relaxed_assignment(hi,reference,temperature,False)[0]
                -relaxed_assignment(lo,reference,temperature,False)[0])*gradient)/(2e-6)
            np.testing.assert_allclose(analytic[i],numeric,rtol=3e-6,atol=3e-7)
            maximum=max(maximum,abs(analytic[i]-numeric))
        shifted=relaxed_assignment(score+13.25,reference,temperature,False)[0]
        np.testing.assert_allclose(shifted,probability,rtol=1e-12,atol=1e-12)
        report['assignmentChecks'].append({'size':count,'temperature':temperature,
            'finiteDifferences':count,'maximumAbsoluteError':maximum,'rowMassVerified':True,
            'commonScoreShiftInvariant':True,'columnMassError':float(abs(probability.sum(0)-1).max())})
    for view in ['individual','overview']:
        directory=base/(view+'-slot-cached1')
        workflow=json.loads((directory/('workflow.audit.json' if view=='individual' else 'workflow.json')).read_text())
        assert workflow['allChecksPassed'] and workflow['sourceVisual']==(1964 if view=='individual' else 289)
        for name,sha in workflow['inputHashes'].items():assert digest(directory/name)==sha
        features=np.load(directory/'joint-input-features.npy')
        positions=np.array(list(pairs(directory/'positions.tsv').values()))
        sizes=np.array(list(pairs(directory/'nodes.tsv').values()))
        provider=PerimeterRoutes(directory)
        model=RelaxedSlotAnchorPolicy(features,positions,sizes,100000.,13711,provider,.125)
        result={'sourceSha256':workflow['sourceSha256'],'sourceWorkflowSha256':digest(directory/('workflow.audit.json' if view=='individual' else 'workflow.json')),
                'zeroTemperatures':[],'actionDerivatives':[],'geometryDerivatives':[]}
        report['views'][view]=result
        for temperature in [.1,.5,2.]:
            action,cache=model.forward_relaxed(features,temperature)
            np.testing.assert_array_equal(action,0.)
            np.testing.assert_array_equal(model.forward(features)[0],0.)
            result['zeroTemperatures'].append(temperature)
            del cache
        model.p['wo']=rng.normal(0,.00015,model.p['wo'].shape)
        model.p['bo']=rng.normal(0,.0001,model.p['bo'].shape)
        temperature=.6
        action,cache=model.forward_relaxed(features,temperature)
        gnodes=rng.normal(0,.001,positions.shape);gports=rng.normal(size=(len(provider.full_edges),2))
        analytic=model.backward(cache,gnodes,gports);del cache
        action_gradient=np.concatenate([gnodes,gports])
        def linear_value():
            value,_=model.forward_relaxed(features,temperature)
            return float(np.sum(value*action_gradient))
        for key in model.keys:
            for flat in np.argsort(abs(analytic[key]).ravel())[-2:]:
                index=np.unravel_index(flat,model.p[key].shape);original=model.p[key][index]
                step=1e-8
                model.p[key][index]=original+step;hi=linear_value()
                model.p[key][index]=original-step;lo=linear_value()
                model.p[key][index]=original
                numeric=(hi-lo)/(2*step);expected=analytic[key][index]
                np.testing.assert_allclose(expected,numeric,rtol=3e-4,atol=2e-4)
                result['actionDerivatives'].append({'parameter':key,'index':list(map(int,index)),
                    'analytic':float(expected),'numeric':numeric,'step':step})
        # The hard path remains the existing equal-size slot/anchor decoder.
        plain=SlotAnchorRayPolicy(features,positions,sizes,100000.,13711,provider,.125)
        plain.p={key:value.copy() for key,value in model.p.items()}
        np.testing.assert_array_equal(plain.forward(features)[0],model.forward(features)[0])
        hard=model.forward(features)[0]
        checkpoint=out/(view+'.npz');model.save(checkpoint,{'validationOnly':True})
        for cls in [RelaxedSlotAnchorPolicy,JointPortPolicy]:
            loaded=cls.load(checkpoint)
            np.testing.assert_array_equal(loaded.forward(features)[0],hard)
            np.testing.assert_array_equal(loaded.forward_relaxed(features,temperature)[0],action)
        result['hardPathUnchanged']=True;result['frozenHardAndRelaxedRoundtripsExact']=True
        result['hardActionSha256']=hashlib.sha256(action_text(hard).encode()).hexdigest()
        old_record=json.loads((directory/'joint-batches.jsonl').read_text().splitlines()[-1])
        old=JointPortPolicy.load(old_record['checkpoint'])
        assert hashlib.sha256(action_text(old.forward(features)[0]).encode()).hexdigest()==old_record['actionSha256']
        result['legacySlotActionReplayed']=True
        ids=provider.physical_ids;by_id={key:i for i,key in enumerate(ids)}
        edges=np.array([[by_id[r[1]],by_id[r[2]]] for r in (line.split('\t') for line in (directory/'edges.tsv').read_text().splitlines())])
        proxy=ActiveLayoutProxy(positions,sizes,edges,route_provider=provider,chunk_size=4096)
        proxy.spacing_padding=0.
        def geometry_value(backward=False):
            value,network=model.forward_relaxed(features,temperature)
            placed=positions+value[:len(positions)]
            provider.set_actions(value[len(positions):],positions=placed)
            loss,gradient=proxy.loss(placed,.1)
            if backward:return loss,model.backward(network,gradient,provider.action_gradient())
            return loss['total']
        loss,analytic=geometry_value(True)
        result['relaxedGeometryLoss']=loss
        for key in ['w1','w2','wo']:
            index=np.unravel_index(np.argmax(abs(analytic[key])),model.p[key].shape)
            original=model.p[key][index];step=1e-9
            model.p[key][index]=original+step;hi=geometry_value()
            model.p[key][index]=original-step;lo=geometry_value()
            model.p[key][index]=original
            numeric=(hi-lo)/(2*step);expected=float(analytic[key][index])
            np.testing.assert_allclose(expected,numeric,rtol=1e-3,atol=.03)
            result['geometryDerivatives'].append({'parameter':key,'index':list(map(int,index)),
                'analytic':expected,'numeric':numeric,'step':step})
        result['allChecksPassed']=True
        print(json.dumps({'view':view,'actionDerivatives':len(result['actionDerivatives']),
            'fullLossDerivatives':len(result['geometryDerivatives']),'checks':'pass'}),flush=True)
    assert all(digest(Path('scripts/erd-poc')/name)==sha for name,sha in hashes.items())
    report['allChecksPassed']=True
except BaseException:
    report['traceback']=traceback.format_exc()
    raise
finally:
    report['seconds']=time.monotonic()-started
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
