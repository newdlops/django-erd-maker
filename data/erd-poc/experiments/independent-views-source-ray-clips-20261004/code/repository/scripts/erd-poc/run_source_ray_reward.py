"""Full Native teacher labels train a source/action NN gain predictor."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from source_ray_clip_policy import SourceRayClipPolicy,containment_bounds
from source_ray_gain_model import train_gain_ranker
from learn_pair_policy import KEYS as RANK_KEYS
from joint_reward_training import head_vector,set_head_vector
from run_anchor_pair_walk import WalkDecoder,verify_saved
from run_anchor_pair_policy import Native,wire
from learned_global_replay import INPUT_FILES
from geometry_world_model import digest

HEAD_KEYS,HEAD_SCALES = ('wo','bo'),(1.,1.)
CODE = ('run_source_ray_reward.py','source_ray_clip_policy.py','source_ray_gain_model.py',
    'learn_pair_policy.py','learn_card_policy.py','compact_patch_neural_policy.py','joint_separation_policy.py',
    'joint_reward_training.py','joint_neural_ports.py','joint_grouped_routes.py',
    'run_anchor_pair_walk.py','run_anchor_pair_policy.py','learned_global_replay.py')


def source_vocabulary(decoder,nodes,view,max_step):
    degree = np.bincount(decoder.provider.owner_edges.ravel(),minlength=len(nodes))
    index = 4 if view=='overview' else 6
    pressure = np.expm1(nodes[:,index])+np.expm1(nodes[:,index+1])
    eligible = np.flatnonzero((degree>=2)&(pressure>0))
    lower,upper = containment_bounds(decoder,eligible,max_step)
    capacity = np.maximum(abs(lower),abs(upper)).max(1)
    useful = eligible[capacity>1.]
    active = useful[np.lexsort((useful,-pressure[useful]))].astype(np.int32)
    assert len(active)
    return active,dict(kind='all-movable-source-conflict-multiple-relationship-owners',owners=active.tolist(),
        sourcePressure=pressure[active].tolist(),incidentCounts=degree[active].tolist(),
        sourcePressureFeatureIndices=[index,index+1],minimumSourceCapacityPixels=1.,
        futureGeometryUsed=False,vocabularyContainsNoCoordinates=True)


def action_features(nodes,model,action,info):
    node = info['selectedOwner']
    return np.r_[nodes[node],np.tanh(model.p['bo'][1:3]),action[node]/2048.,
        info['maximumPhaseOffset'],info['sourceContainmentCapacityPixels']/2048.]


def run(args):
    assert 1<=args.owners<=64 and 1<=args.controls<=128 and 0<args.seconds<=30
    args.out.mkdir(parents=True,exist_ok=False)
    started = time.monotonic()
    binding = json.loads(args.source_binding.read_text())
    spec = binding['viewSources'][args.view]
    source = Path(spec['directory'])
    inputs = {name:digest(source/name) for name in INPUT_FILES}
    assert inputs==spec['inputs']
    assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json')==binding['promotedCandidateSha256']
    decoder = WalkDecoder(source)
    native = Native(args.environment,source,args.out/'learned.tsv',args.view=='overview',False)
    records,heads,features = [],[],[]
    trained_updates = attempts = accepted = skipped_zero = 0
    selected_index = selected_result = None
    best = None
    try:
        assert native.initial['visual']==spec['expectedVisual']
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        active,support = source_vocabulary(decoder,nodes,args.view,2048.)
        model = SourceRayClipPolicy(nodes,decoder,active,2048.,args.seed)
        model.save(args.out/'initial-geometry-model.npz',dict(kind='source-ray-neural-field',trainedUpdates=0))
        np.savez_compressed(args.out/'observations.npz',nodes=nodes,active=active)
        zero = model.forward(nodes)[0]
        assert not np.any(zero)
        control = native.request(wire(zero,'MEASURE'))
        assert control['legal'] and control['visual']==spec['expectedVisual']
        assert control['hard']==control['individualHard']==control['spacing']==0
        teaching_started = time.monotonic()
        with (args.out/'teachers.jsonl').open('x') as log:
            for target in range(min(args.owners,len(active))):
                for axis,sign in ((0,1.),(0,-1.),(1,1.),(1,-1.)):
                    if len(records)>=args.controls or time.monotonic()-teaching_started>=args.seconds:
                        break
                    model.p = dict(wo=np.zeros((8,3)),bo=np.zeros(3))
                    model.p['wo'][:,0] = model.embedding[target]
                    model.p['bo'][axis+1] = sign*3.
                    action,info = model.forward(nodes)
                    assert info['selectedVocabularyIndex']==target
                    if not np.any(action[:len(nodes)]):
                        skipped_zero += 1
                        continue
                    feature = action_features(nodes,model,action,info)
                    assert len(feature)==70
                    features.append(feature)
                    heads.append(head_vector(model,HEAD_KEYS,HEAD_SCALES))
                    command = wire(action,'MEASURE')
                    result = native.request(command)
                    row = dict(index=len(records),target=target,axis=axis,sign=sign,
                        wireSha256=hashlib.sha256(command.encode()).hexdigest(),info=info,result=result)
                    records.append(row)
                    log.write(json.dumps(row)+'\n')
                log.flush()
                legal = [row for row in records if row['result']['legal']]
                print(json.dumps(dict(view=args.view,teacherControls=len(records),
                    minimumLegalVisual=min((row['result']['visual'] for row in legal),default=None),
                    betterLegalControls=sum(row['result']['visual']<spec['expectedVisual'] for row in legal))),flush=True)
                if len(records)>=args.controls or time.monotonic()-teaching_started>=args.seconds:
                    break
        features = np.array(features)
        heads = np.array(heads)
        gains = np.array([spec['expectedVisual']-row['result']['visual'] if row['result']['legal'] else -1000. for row in records])
        np.savez_compressed(args.out/'teacher-inputs.npz',features=features,heads=heads,gains=gains)
        if np.any(gains>0):
            ranker,initial,history = train_gain_ranker(features,gains,args.seed+1)
            trained_updates = len(history)
            with (args.out/'training.jsonl').open('x') as log:
                for row in history:
                    log.write(json.dumps(row)+'\n')
            metadata = dict(kind='source-specific-ray-clipped-neural-gain-selector',trainedUpdates=trained_updates,
                seed=args.seed+1,features=70,hidden=24,gainScale=32,loss='mean squared normalized gain',
                teacherInputSha256=digest(args.out/'teacher-inputs.npz'),heldOutGeneralizationVerified=False,
                inferenceUsesNoFutureMetrics=True)
            np.savez_compressed(args.out/'gain-model.npz',**ranker.p,mean=ranker.mean,scale=ranker.scale,
                **{'initial__'+key:value for key,value in initial.items()},metadata=np.array(json.dumps(metadata)))
            scores = ranker.forward(features)[0]
            np.save(args.out/'scores.npy',scores)
            selected_index = int(np.argmax(scores))
            set_head_vector(model,heads[selected_index],HEAD_KEYS,HEAD_SCALES)
            action,info = model.forward(nodes)
            selected_result = native.request(wire(action))
            attempts = 1
            accepted = int(selected_result['accepted'])
            model.save(args.out/'selected-geometry-model.npz',dict(kind='neural-gain-selected-ray-clipped-output',
                trainedSelectorUpdates=trained_updates,geometryRewardHeadUpdates=0))
            np.save(args.out/'selected-action.npy',action)
            (args.out/'selected.json').write_text(json.dumps(dict(index=selected_index,score=float(scores[selected_index]),
                info=info,result=selected_result,wireSha256=hashlib.sha256(wire(action).encode()).hexdigest()),indent=2)+'\n')
            if accepted:
                best = action.copy()
                np.save(args.out/'best-action.npy',best)
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(source,args.out,decoder,best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==accepted
    assert stats['hardConditions']==stats['individualHardConditions']==stats['spacing']==stats['overlap']==0
    legal = [row for row in records if row['result']['legal']]
    report = dict(kind='neural-gain-selected-source-ray-shortening-v1',view=args.view,seed=args.seed,
        sourceDirectory=str(source),sourceInputs=inputs,sourceBinding=str(args.source_binding),
        sourceBindingSha256=digest(args.source_binding),environment=str(args.environment),environmentSha256=digest(args.environment),
        sourceVisual=spec['expectedVisual'],sourceIndividualVisual=native.initial['individualVisual'],support=support,
        ownerLimit=args.owners,controlLimit=args.controls,teacherSecondsLimit=args.seconds,
        teacherControls=len(records),skippedSourceIdentityControls=skipped_zero,legalTeacherControls=len(legal),
        minimumLegalTeacherVisual=min((row['result']['visual'] for row in legal),default=None),
        betterLegalTeacherControls=int(np.count_nonzero(gains>0)),reasons=dict(Counter(row['result']['reason'] for row in records)),
        neuralGainTrainingUpdates=trained_updates,selectedIndex=selected_index,selectedResult=selected_result,
        attempts=attempts,accepted=accepted,final=stats,zeroHeadSourceIdentity=True,sourceControl=control,
        selectorChoosesOnlyFromLearnedScores=True,teacherControlsNeverSubmittedForAcceptance=True,
        geometryHeadRewardUpdates=0,geometryDecoderNetworkParameters=27,
        trainedGainNetworkParameters=2329 if trained_updates else 0,
        noFutureMetricsInInference=True,nativeCoordinateSearchOrRepairs=0,fullNativeRewardScoring=True,
        originalCardDimensionsAndFrameRetained=True,physicalOwners=len(nodes),fullCanonicalEdges=len(decoder.provider.full_edges),
        noWholeComponentPortRemapping=True,heldOutGeneralizationVerified=False,
        codeSha256={name:digest(Path(__file__).parent/name) for name in CODE},
        observationsSha256=digest(args.out/'observations.npz'),initialModelSha256=digest(args.out/'initial-geometry-model.npz'),
        teacherInputSha256=digest(args.out/'teacher-inputs.npz'),teacherTraceSha256=digest(args.out/'teachers.jsonl'),
        wallSeconds=time.monotonic()-started,promoted=False,browserVerified=False)
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('view','sourceVisual','teacherControls','legalTeacherControls',
        'minimumLegalTeacherVisual','betterLegalTeacherControls','neuralGainTrainingUpdates','selectedIndex','accepted','wallSeconds')}
        | dict(finalVisual=stats['visual'])),flush=True)


if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--view',choices=('overview','individual'),required=True)
    parser.add_argument('--source-binding',type=Path,required=True)
    parser.add_argument('--environment',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=127507)
    parser.add_argument('--owners',type=int,default=24)
    parser.add_argument('--controls',type=int,default=64)
    parser.add_argument('--seconds',type=float,default=30.)
    run(parser.parse_args())
