"""Source-scheduled NN controls teach a small learned selector, never a search."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from source_star_cell_policy import SourceStarCellPolicy
from run_source_star_cell_reward import source_vocabulary
from run_anchor_pair_walk import WalkDecoder,verify_saved
from run_anchor_pair_policy import Native,wire
from learn_pair_policy import Ranker,KEYS as RANK_KEYS
from joint_reward_training import head_vector,set_head_vector
from learned_global_replay import INPUT_FILES
from geometry_world_model import digest


def parameter_hash(parameters):
    h = hashlib.sha256()
    for key in RANK_KEYS:
        h.update(parameters[key].tobytes())
    return h.hexdigest()


def train_selector(features,gains,seed,path):
    model = Ranker(seed,features=70,hidden=24)
    model.mean = features.mean(0)
    model.scale = np.maximum(.2,features.std(0))
    initial = {key:value.copy() for key,value in model.p.items()}
    first = {key:np.zeros_like(value) for key,value in model.p.items()}
    second = {key:value.copy() for key,value in first.items()}
    weights = np.where(gains > 0,np.maximum(8.,np.minimum(32.,gains*8)),1.)
    with path.open('x') as log:
        for step in range(1,301):
            before = parameter_hash(model.p)
            loss,gradients = model.loss(features,gains,weights,True)
            norm = np.sqrt(sum(np.sum(value*value) for value in gradients.values()))
            for key in RANK_KEYS:
                gradient = gradients[key] * min(1.,5./max(norm,1e-12))
                first[key] = .9*first[key]+.1*gradient
                second[key] = .999*second[key]+.001*gradient*gradient
                model.p[key] -= .01*(first[key]/(1-.9**step))/(np.sqrt(second[key]/(1-.999**step))+1e-8)
            log.write(json.dumps(dict(step=step,beforeSha256=before,loss=loss,
                afterSha256=parameter_hash(model.p)))+'\n')
    return model,initial


parser = argparse.ArgumentParser()
parser.add_argument('--view',choices=('overview','individual'),required=True)
parser.add_argument('--out',type=Path,required=True)
parser.add_argument('--source-binding',type=Path)
parser.add_argument('--toy-directory',type=Path)
parser.add_argument('--environment',type=Path,default=Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment'))
parser.add_argument('--seed',type=int,default=123107)
args = parser.parse_args()
assert (args.source_binding is None) != (args.toy_directory is None)
spec = None
if args.source_binding:
    binding = json.loads(args.source_binding.read_text())
    spec = binding['viewSources'][args.view]
    directory = Path(spec['directory'])
    assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
else:
    directory = args.toy_directory
    assert directory.resolve().is_relative_to(Path('.tmp').resolve())
inputs = {name:digest(directory/name) for name in INPUT_FILES}
if spec:
    assert inputs == spec['inputs']
args.out.mkdir(parents=True,exist_ok=False)
decoder = WalkDecoder(directory)
native = Native(args.environment,directory,args.out/'learned.tsv',args.view=='overview',False)
teacher_results,teacher_heads,candidate_features = [],[],[]
phases = (0.,.25,.5,.75)
trained_updates = attempts = accepted = 0
selected_index = trained_result = None
best = None
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    baseline = native.initial['visual']
    if spec:
        assert baseline == spec['expectedVisual']
    active,support = source_vocabulary(decoder,nodes,args.view,12)
    model = SourceStarCellPolicy(nodes,decoder,active,2048.,args.seed)
    model.save(args.out/'initial-cell-model.npz',dict(kind='source-cell-teacher-encoder',trainedUpdates=0))
    np.savez_compressed(args.out/'observations.npz',nodes=nodes,active=active)
    assert not np.any(model.forward(nodes)[0])
    initial_control = native.request(wire(model.forward(nodes)[0],'MEASURE'))
    assert initial_control['legal'] and initial_control['visual']==baseline
    with (args.out/'teachers.jsonl').open('x') as log:
        for target in range(len(active)):
            # Calibrate source classification only. No future cost or coordinate
            # participates in this prototype selector's deterministic updates.
            selector = np.zeros(8)
            for step in range(256):
                logits = model.embedding@selector
                probability = np.exp(logits-logits.max())
                probability /= probability.sum()
                selector -= .5*(model.embedding.T@probability-model.embedding[target])
            assert int(np.argmax(model.embedding@selector)) == target
            for phase in phases:
                model.p = dict(wo=np.zeros((8,3)),bo=np.array([0.,3.,
                    -50. if phase==0 else np.arctanh(2*phase-1)]))
                model.p['wo'][:,0] = selector
                action,info = model.forward(nodes)
                assert info['selectedVocabularyIndex']==target
                assert not np.any(action[len(nodes):])
                # Inputs are fixed source observations and the NN's action.
                # Neither a future event count nor a future boolean is supplied.
                feature = np.r_[nodes[int(active[target])],np.sin(2*np.pi*phase),np.cos(2*np.pi*phase),
                    info['amplitude'],action[int(active[target])]/2048.,info['sourceCellCapacityPixels']/2048.]
                assert len(feature)==70
                candidate_features.append(feature)
                teacher_heads.append(head_vector(model,('wo','bo'),(1.,1.)))
                command = wire(action,'MEASURE')
                result = native.request(command)
                teacher_results.append(result)
                log.write(json.dumps(dict(index=len(teacher_results)-1,sourceTarget=target,
                    phase=phase,wireSha256=hashlib.sha256(command.encode()).hexdigest(),info=info,result=result))+'\n')
            log.flush()
            print(json.dumps(dict(view=args.view,teacherControls=len(teacher_results),
                minimumLegalVisual=min(x['visual'] for x in teacher_results if x['legal']),
                betterLegalControls=sum(x['legal'] and x['visual']<baseline for x in teacher_results))),flush=True)
    features = np.array(candidate_features)
    heads = np.array(teacher_heads)
    gains = np.array([baseline-result['visual'] if result['legal'] else -1000. for result in teacher_results])
    np.savez_compressed(args.out/'teacher-inputs.npz',features=features,heads=heads,gains=gains)
    if np.any(gains>0):
        ranker,initial = train_selector(features,gains,args.seed+1,args.out/'ranker-training.jsonl')
        trained_updates = 300
        scores = ranker.forward(features)[0]
        selected_index = int(np.argmax(scores))
        metadata = dict(kind='source-cell-native-label-trained-selector-v1',seed=args.seed+1,
            trainedUpdates=300,features=70,hidden=24,teacherInputSha256=digest(args.out/'teacher-inputs.npz'),
            sourceSpecificTraining=True,heldOutGeneralizationVerified=False,
            futureMetricsUsedInInference=False,rankingUsesOnlyTrainedNetwork=True)
        np.savez_compressed(args.out/'ranker.npz',**ranker.p,mean=ranker.mean,scale=ranker.scale,
            **{'initial__'+key:value for key,value in initial.items()},metadata=np.array(json.dumps(metadata)))
        np.save(args.out/'scores.npy',scores)
        set_head_vector(model,heads[selected_index],('wo','bo'),(1.,1.))
        action,info = model.forward(nodes)
        trained_result = native.request(wire(action))
        attempts = 1
        accepted = int(trained_result['accepted'])
        if accepted:
            best = action.copy()
            np.save(args.out/'best-action.npy',best)
        (args.out/'selected.json').write_text(json.dumps(dict(index=selected_index,
            score=float(scores[selected_index]),wireSha256=hashlib.sha256(wire(action).encode()).hexdigest(),
            info=info,result=trained_result),indent=2)+'\n')
    assert native.request('SAVE')['saved']
finally:
    native.close()
verify_saved(directory,args.out,decoder,best)
stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==accepted
assert stats['hardConditions']==stats['individualHardConditions']==stats['spacing']==stats['overlap']==0
report = dict(kind='source-cell-teacher-controls-and-trained-neural-selection-v1',view=args.view,
    seed=args.seed,sourceDirectory=str(directory),sourceInputs=inputs,support=support,
    sourceBindingSha256=digest(args.source_binding) if args.source_binding else None,
    environmentSha256=digest(args.environment),initialVisual=baseline,
    prototypeCalibrationUpdates=256*len(active),prototypeCalibrationUsesNoFutureLabels=True,
    teacherControlCount=len(teacher_results),legalTeacherControls=sum(x['legal'] for x in teacher_results),
    minimumLegalTeacherVisual=min(x['visual'] for x in teacher_results if x['legal']),
    betterLegalTeacherControls=int(np.count_nonzero(gains>0)),rankerTrainingUpdates=trained_updates,
    selectedIndex=selected_index,trainedResult=trained_result,attempts=attempts,accepted=accepted,final=stats,
    originalCardDimensionsAndEndpointsRetained=True,inferenceUsesNoFutureMetrics=True,
    teacherControlsNeverSubmittedForAcceptance=True,nativeCoordinateSearchOrRepairs=0,
    selectedByLearnedScoresOnly=True,sourceSpecificTraining=True,heldOutGeneralizationVerified=False,
    codeSha256={str(p):digest(p) for p in [Path(__file__),Path('scripts/erd-poc/source_star_cell_policy.py'),
        Path('scripts/erd-poc/run_source_star_cell_reward.py'),Path('scripts/erd-poc/learn_pair_policy.py')]},
    teacherInputSha256=digest(args.out/'teacher-inputs.npz'),teacherTraceSha256=digest(args.out/'teachers.jsonl'),
    observationsSha256=digest(args.out/'observations.npz'),promoted=False,browserVerified=False)
(args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({key:report[key] for key in ('view','initialVisual','teacherControlCount','legalTeacherControls',
    'minimumLegalTeacherVisual','betterLegalTeacherControls','rankerTrainingUpdates','selectedIndex','accepted')}
    | dict(finalVisual=stats['visual'])),flush=True)
