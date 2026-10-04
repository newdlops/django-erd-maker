"""Full independent replay of the fixed large-body NN anchor controls."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from compact_component_anchor_policy import ComponentAnchorPatchPolicy
from run_source_star_cell_reward import source_vocabulary
from run_anchor_pair_walk import WalkDecoder
from run_anchor_pair_policy import Native,wire
from joint_reward_training import head_vector
from geometry_world_model import digest

root = Path('.tmp/visualcross-ml-150-750-20261004/source-star-cell1')
stage = root/'individual-common-anchors-controls1'
out = root/'common-anchors-validation1'
out.mkdir(exist_ok=False)
report = json.loads((stage/'report.json').read_text())
for path,sha in report['codeSha256'].items():
    assert digest(path)==sha
binding_path = Path('.tmp/visualcross-ml-150-750-20261004/geometry-world1/source-binding.json')
assert digest(binding_path)==report['sourceBindingSha256']
binding = json.loads(binding_path.read_text())
assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json')==binding['promotedCandidateSha256']
source = Path(report['sourceDirectory'])
for name,sha in report['sourceInputs'].items():
    assert digest(source/name)==sha
for path,key in [('observations.npz','observationsSha256'),('control-inputs.npz','controlInputSha256'),
        ('controls.jsonl','traceSha256')]:
    assert digest(stage/path)==report[key]
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
assert digest(environment)==report['environmentSha256']
decoder = WalkDecoder(source)
with np.load(stage/'observations.npz',allow_pickle=False) as saved:
    nodes,recorded_active = saved['nodes'].copy(),saved['active'].copy()
active,support = source_vocabulary(decoder,nodes,'individual',3)
np.testing.assert_array_equal(active,recorded_active)
assert support==report['support']
with np.load(stage/'control-inputs.npz',allow_pickle=False) as saved:
    features,heads = saved['features'].copy(),saved['heads'].copy()
rows = [json.loads(line) for line in (stage/'controls.jsonl').read_text().splitlines()]
assert len(rows)==15
native = Native(environment,source,out/'unused.tsv',False,False)
checked = controls = legal = 0
minimum_legal = None
maximum_displacement = 0.
try:
    np.testing.assert_array_equal(nodes,np.array([r['features'] for r in native.initial['nodes']]))
    for ordinal,node in enumerate(active):
        baseline_row = rows[ordinal*5]
        assert baseline_row['owner']==node
        model = ComponentAnchorPatchPolicy(nodes,decoder,[int(node)],2048.,report['seed'])
        checkpoint = stage/f'initial-owner-{node}.npz'
        assert digest(checkpoint)==baseline_row['checkpointSha256']
        fixed = dict(**model.p,active=model.active,embedding=model.embedding,w1=model.w1,w2=model.w2,
            mean=model.mean,scale=model.scale,**model.buffers)
        with np.load(checkpoint,allow_pickle=False) as saved:
            assert set(saved.files)==set(fixed)|{'metadata'}
            for key,value in fixed.items():
                np.testing.assert_array_equal(saved[key],value)
        zero,info = model.forward(nodes)
        assert info==baseline_row['info']
        assert native.request(wire(zero,'MEASURE'))==baseline_row['zeroBodyAnchorBaseline']
        checked += 1
        for offset,(axis,sign) in enumerate([(0,1.),(0,-1.),(1,1.),(1,-1.)]):
            row = rows[ordinal*5+offset+1]
            assert row['owner']==node and row['axis']==axis and row['sign']==sign
            assert row['index']==controls
            model.p['bo'][:] = 0.
            model.p['bo'][axis] = sign*3.
            action,info = model.forward(nodes)
            np.testing.assert_array_equal(head_vector(model,('wo','bo','aw','ab'),(1.,1.,1.,1.)),heads[controls])
            feature = np.r_[nodes[int(node)],action[int(node)]/2048.,np.tanh(model.p['bo']),
                float(info['affectedOwners'])/len(nodes),float(np.max(abs(action[len(nodes):])))]
            np.testing.assert_array_equal(feature,features[controls])
            command = wire(action,'MEASURE')
            assert hashlib.sha256(command.encode()).hexdigest()==row['wireSha256']
            result = native.request(command)
            assert result==row['result'] and info==row['info']
            displacement = float(abs(action[:len(nodes)]).max())
            assert displacement==row['maximumBodyDisplacementPixels']
            maximum_displacement = max(maximum_displacement,displacement)
            assert np.count_nonzero(np.any(action[:len(nodes)]!=0,axis=1))<=1
            # Both view boxes use the original dimensions, including every
            # original member card of a physical owner.
            p = decoder.provider
            physical = decoder.positions+action[:len(nodes)]
            full = decoder.positions[p.owner]+p.offsets+action[:len(nodes)][p.owner]
            for positions,sizes in [(physical,decoder.sizes),(full,p.sizes)]:
                area = np.prod((positions+sizes/2).max(0)-(positions-sizes/2).min(0))
                assert area<=1.5e9
            checked += 1
            controls += 1
            legal += int(result['legal'])
            if result['legal']:
                minimum_legal = result['visual'] if minimum_legal is None else min(minimum_legal,result['visual'])
finally:
    native.close()
assert checked==report['nativeMeasurements']==15
assert controls==report['controls']==12 and legal==report['legalControls']
assert minimum_legal==report['minimumLegalVisual']
proof = dict(kind='free-body-common-anchor-nn-controls-full-replay-v1',status='pass',
    controlsReplayed=controls,fullNativeMeasurements=checked,legalControls=legal,
    minimumLegalVisual=minimum_legal,sourceVisual=report['sourceVisual'],
    maximumBodyDisplacementPixels=maximum_displacement,
    allSourceEncoderAndDecoderBuffersReconstructed=True,allTeacherLabelsIndependentlyRemeasured=True,
    bothViewAreasAtMost1_5B=True,originalCardDimensionsRetained=True,
    coordinateSearchOrRepairs=0,nativeTryCalls=0,newGeometryRewardTrainingUpdates=0,
    noCandidateSelected=True,sourceCommonAnchorBasisChangesPortsEvenWithZeroBody=True,
    reportSha256=digest(stage/'report.json'),codeSha256=digest(__file__),promoted=False,browserVerified=False)
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps(proof),flush=True)
