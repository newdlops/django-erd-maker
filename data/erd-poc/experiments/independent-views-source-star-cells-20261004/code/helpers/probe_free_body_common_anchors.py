"""Observe fixed NN large-body controls with closed component anchor outputs."""
import argparse
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
from learned_global_replay import INPUT_FILES
from geometry_world_model import digest

parser = argparse.ArgumentParser()
parser.add_argument('--view',choices=('overview','individual'),required=True)
parser.add_argument('--out',type=Path,required=True)
args = parser.parse_args()
binding_path = Path('.tmp/visualcross-ml-150-750-20261004/geometry-world1/source-binding.json')
binding = json.loads(binding_path.read_text())
assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
spec = binding['viewSources'][args.view]
source = Path(spec['directory'])
assert {name:digest(source/name) for name in INPUT_FILES}==spec['inputs']
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
args.out.mkdir(parents=True,exist_ok=False)
decoder = WalkDecoder(source)
native = Native(environment,source,args.out/'unused.tsv',args.view=='overview',False)
records,heads,features = [],[],[]
try:
    assert native.initial['visual']==spec['expectedVisual']
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    active,support = source_vocabulary(decoder,nodes,args.view,3)
    np.savez_compressed(args.out/'observations.npz',nodes=nodes,active=active)
    with (args.out/'controls.jsonl').open('x') as log:
        for node in active:
            model = ComponentAnchorPatchPolicy(nodes,decoder,[int(node)],2048.,125107)
            initial_file = args.out/f'initial-owner-{node}.npz'
            model.save(initial_file,dict(kind='single-source-owner-closed-anchors-control',trainedUpdates=0))
            zero,info = model.forward(nodes)
            initial = native.request(wire(zero,'MEASURE'))
            log.write(json.dumps(dict(owner=int(node),zeroBodyAnchorBaseline=initial,info=info,
                checkpointSha256=digest(initial_file)))+'\n')
            for axis in (0,1):
                for sign in (1.,-1.):
                    model.p['bo'][:] = 0.
                    model.p['bo'][axis] = sign*3.
                    action,info = model.forward(nodes)
                    assert np.count_nonzero(np.any(action[:len(nodes)]!=0,axis=1))<=1
                    head = head_vector(model,('wo','bo','aw','ab'),(1.,1.,1.,1.))
                    command = wire(action,'MEASURE')
                    result = native.request(command)
                    row = dict(index=len(records),owner=int(node),axis=axis,sign=sign,info=info,
                        maximumBodyDisplacementPixels=float(abs(action[:len(nodes)]).max()),
                        nonzeroEndpointPhases=int(np.count_nonzero(action[len(nodes):])),
                        result=result,wireSha256=hashlib.sha256(command.encode()).hexdigest())
                    records.append(row)
                    heads.append(head)
                    feature = np.r_[nodes[int(node)],action[int(node)]/2048.,np.tanh(model.p['bo']),
                        float(info['affectedOwners'])/len(nodes),float(np.max(abs(action[len(nodes):])))]
                    assert len(feature)==70
                    features.append(feature)
                    log.write(json.dumps(row)+'\n')
                    print(json.dumps(dict(view=args.view,owner=int(node),axis=axis,sign=sign,
                        maximumBodyDisplacementPixels=row['maximumBodyDisplacementPixels'],result=result)),flush=True)
            log.flush()
finally:
    native.close()
np.savez_compressed(args.out/'control-inputs.npz',features=np.array(features),heads=np.array(heads))
legal = [r for r in records if r['result']['legal']]
report = dict(kind='source-scheduled-single-owner-free-body-closed-anchor-nn-controls-v1',view=args.view,
    sourceDirectory=str(source),sourceInputs=spec['inputs'],sourceBindingSha256=digest(binding_path),
    environmentSha256=digest(environment),seed=125107,support=support,
    sourceVisual=spec['expectedVisual'],controls=len(records),legalControls=len(legal),
    minimumLegalVisual=min((r['result']['visual'] for r in legal),default=None),
    betterLegalControls=sum(r['result']['visual']<spec['expectedVisual'] for r in legal),
    maximumBodyDisplacementPixels=max(r['maximumBodyDisplacementPixels'] for r in records),
    parametersPerControl=36,nativeMeasurements=3+len(records),nativeTryCalls=0,
    newGeometryRewardTrainingUpdates=0,zeroHeadSourceIdentity=False,
    sourceCommonAnchorBasisChangesPortsEvenWithZeroBody=True,
    completeSourceComponentsRetainBothSidesOfAllAffectedStars=True,
    fixedSourceControlsSelectedBeforeMeasurement=True,controlsNeverSubmittedForAcceptance=True,
    noCandidateSelected=True,nativeCoordinateSearchOrRepairs=0,
    controlInputSha256=digest(args.out/'control-inputs.npz'),traceSha256=digest(args.out/'controls.jsonl'),
    observationsSha256=digest(args.out/'observations.npz'),
    codeSha256={str(path):digest(path) for path in [Path(__file__),Path('scripts/erd-poc/compact_component_anchor_policy.py'),
        Path('scripts/erd-poc/compact_source_port_policy.py'),Path('scripts/erd-poc/compact_patch_neural_policy.py'),
        Path('scripts/erd-poc/run_source_star_cell_reward.py')]},promoted=False,browserVerified=False)
(args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({key:report[key] for key in ('view','sourceVisual','controls','legalControls','minimumLegalVisual',
    'betterLegalControls','maximumBodyDisplacementPixels','nativeMeasurements')}),flush=True)
