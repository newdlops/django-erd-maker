"""Train a source-cell neural owner selector and motion head from full rewards."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from source_star_cell_policy import SourceStarCellPolicy
from geometry_world_model import digest
from joint_reward_training import RewardHeadTrainer, head_vector
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved

KEYS, SCALES = ('wo', 'bo'), (1., 1.)
CODE = ('run_source_star_cell_reward.py', 'source_star_cell_policy.py',
    'compact_patch_neural_policy.py', 'compact_ordered_source_port_policy.py',
    'compact_source_port_policy.py', 'joint_separation_policy.py', 'joint_reward_training.py',
    'joint_neural_ports.py', 'joint_grouped_routes.py', 'run_anchor_pair_walk.py',
    'run_anchor_pair_policy.py')


def source_vocabulary(decoder, features, view, count):
    assert 1 <= count <= 16 and view in ('overview', 'individual')
    degree = np.bincount(decoder.provider.owner_edges.ravel(), minlength=len(decoder.positions))
    index = 4 if view == 'overview' else 6
    pressure = np.expm1(features[:, index]) + np.expm1(features[:, index+1])
    eligible = np.flatnonzero((degree >= 2) & (pressure > 0))
    active = eligible[np.lexsort((eligible, -pressure[eligible]))][:count].astype(np.int32)
    assert len(active)
    return active, dict(kind='immutable-source-multiple-relationship-conflict-vocabulary',
        owners=active.tolist(), incidentCounts=degree[active].tolist(),
        sourcePressure=pressure[active].tolist(), pressureFeatureIndices=[index,index+1],
        futureGeometryUsed=False, vocabularyEmitsNoCoordinates=True)


def run(args):
    assert 1 <= args.iterations <= 128 and 0 < args.seconds <= 30
    assert 0 < args.sigma <= .05 and 0 < args.rate <= .05
    binding = json.loads(args.source_binding.read_text())
    spec = binding['viewSources'][args.view]
    directory = Path(spec['directory'])
    inputs = {name: digest(directory / name) for name in INPUT_FILES}
    assert inputs == spec['inputs']
    assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
    args.out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    decoder = WalkDecoder(directory)
    native = Native(args.environment, directory, args.out / 'learned.tsv', args.view == 'overview', False)
    best = best_step = None
    accepted = attempts = 0
    reasons = Counter()
    try:
        assert native.initial['visual'] == spec['expectedVisual']
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        active, support = source_vocabulary(decoder, nodes, args.view, args.vocabulary)
        model = SourceStarCellPolicy(nodes, decoder, active, args.max_step, args.seed)
        np.savez_compressed(args.out / 'observations.npz', nodes=nodes, active=active)
        model.save(args.out / 'initial-model.npz', dict(kind='source-star-cell',trainedUpdates=0))
        zero, control_info = model.forward(nodes)
        assert not np.any(zero)
        control = native.request(wire(zero, 'MEASURE'))
        assert control['legal'] and control['visual'] == spec['expectedVisual']
        assert control['hard'] == control['individualHard'] == control['spacing'] == 0
        with (args.out / 'training.jsonl').open('x') as trace, (args.out / 'actions.jsonl').open('x') as actions:
            trainer = RewardHeadTrainer(model, nodes, native.request, wire, trace, args.seed, args.sigma,
                2, args.rate, KEYS, SCALES, cache_size=0)
            assert len(head_vector(model, KEYS, SCALES)) == 27
            train_started = time.monotonic()
            for step in range(1, args.iterations+1):
                if time.monotonic() - train_started >= args.seconds:
                    break
                trainer.sigma = args.sigma * (1., 3., 8., 16.)[((step-1) // 8) % 4]
                progress = trainer.step()
                if step % 4 == 0 and trainer.ever_updated:
                    action, info = model.forward(nodes)
                    checkpoint = args.out / f'model-step-{step:03}.npz'
                    model.save(checkpoint, dict(kind='source-star-cell-trained',trainedUpdates=step))
                    result = native.request(wire(action))
                    attempts += 1
                    reasons[result['reason']] += 1
                    row = dict(step=step, checkpoint=str(checkpoint), checkpointSha256=digest(checkpoint),
                        wireSha256=hashlib.sha256(wire(action).encode()).hexdigest(), policyInfo=info,
                        result=result, trainedHeadChanged=True,rewardSummary=progress)
                    actions.write(json.dumps(row)+'\n')
                    actions.flush()
                    if result['accepted']:
                        best, best_step = action.copy(), step
                        accepted += 1
                        model.save(args.out / 'best-model.npz', dict(kind='source-star-cell-best-trained',trainedUpdates=step))
                    print(json.dumps(dict(step=step,result=result,policyInfo=info)),flush=True)
            assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(directory,args.out,decoder,best)
    if best is not None:
        np.save(args.out / 'best-action.npy',best)
    stats = json.loads((args.out / 'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated'] == attempts and stats['acceptedActions'] == accepted
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    report = dict(kind='source-star-cell-neural-owner-and-motion-native-reward-v1',view=args.view,
        seed=args.seed,sourceDirectory=str(directory),sourceInputs=inputs,
        sourceBinding=str(args.source_binding),sourceBindingSha256=digest(args.source_binding),
        environment=str(args.environment),environmentSha256=digest(args.environment),
        vocabulary=args.vocabulary,support=support,maxStep=args.max_step,
        initialVisual=native.initial['visual'],initialIndividualVisual=native.initial['individualVisual'],
        zeroHeadControl=control,zeroHeadSourceIdentity=True,zeroHeadControlNeverSubmitted=True,
        zeroHeadControlInfo=control_info,final=stats,trainableParameters=27,parameterKeys=KEYS,parameterScales=SCALES,
        initialSigma=args.sigma,sigmaScheduleMultipliers=(1.,3.,8.,16.),sigmaStepsPerLevel=8,rate=args.rate,
        directionsPerUpdate=2,iterationsLimit=args.iterations,secondsLimit=args.seconds,
        updatesExecuted=trainer.iterations,newTrainingUpdates=trainer.iterations,rewardProbes=trainer.measurements,
        fullNativeRewardMeasurements=trainer.native_measurements,learnedHeadChanged=trainer.ever_updated,
        attempts=attempts,accepted=accepted,bestStep=best_step,reasons=dict(reasons),
        physicalOwners=len(decoder.positions),fullCanonicalEdges=len(decoder.provider.full_edges),
        encoderFixed=True,featureInputDtype='float32',originalCardDimensionsAndFrameRetained=True,
        singlePhysicalOwnerMovedPerOutput=True,sourceCellDecodingBeforeMeasurement=True,
        neuralOwnerSelection=True,neuralDirectionAndAmplitude=True,inferenceUsesNoFutureMetrics=True,
        nativeCoordinateSearchOrRepairs=0,onlyUpdatedNeuralHeadsSubmitted=True,
        originalCardRelativePortsRetained=True,fullNativeRewardScoring=True,
        trainingSha256=digest(args.out / 'training.jsonl'),actionsSha256=digest(args.out / 'actions.jsonl'),
        observationsSha256=digest(args.out / 'observations.npz'),initialModelSha256=digest(args.out / 'initial-model.npz'),
        codeSha256={name:digest(Path(__file__).parent / name) for name in CODE},
        wallSeconds=time.monotonic()-started,productLoadOrBrowserVerified=False,promoted=False)
    (args.out / 'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ('view','updatesExecuted','rewardProbes','attempts','accepted',
        'bestStep','reasons','wallSeconds')} | dict(initialVisual=report['initialVisual'],finalVisual=stats['visual'])),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding',type=Path,required=True)
    parser.add_argument('--environment',type=Path,required=True)
    parser.add_argument('--view',choices=('overview','individual'),required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=121107)
    parser.add_argument('--vocabulary',type=int,default=12)
    parser.add_argument('--max-step',type=float,default=2048.)
    parser.add_argument('--sigma',type=float,default=.05)
    parser.add_argument('--rate',type=float,default=.03)
    parser.add_argument('--iterations',type=int,default=64)
    parser.add_argument('--seconds',type=float,default=30.)
    run(parser.parse_args())
