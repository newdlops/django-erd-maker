"""Train whole-scene slot/anchor heads from full Native teacher rewards."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np

from compact_common_slot_policy import CommonSlotAnchorPolicy
from geometry_world_model import digest
from joint_reward_training import RewardHeadTrainer, head_vector
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved

KEYS = ('so', 'sb', 'wo', 'bo')
SCALES = (1., 1., 1., 1.)
SIGMAS = (1., 3., 8., 16.)
CODE = ('run_common_slot_reward.py', 'compact_common_slot_policy.py', 'joint_reward_training.py',
        'joint_neural_ports.py', 'joint_grouped_routes.py', 'run_anchor_pair_walk.py', 'run_anchor_pair_policy.py')


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
    best = None
    best_step = None
    attempts = accepted = 0
    reasons = Counter()
    initial_visual = native.initial['visual']
    try:
        assert initial_visual == spec['expectedVisual']
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        model = CommonSlotAnchorPolicy(nodes, decoder, args.seed)
        np.savez_compressed(args.out / 'observations.npz', nodes=nodes)
        model.save(args.out / 'initial-model.npz', dict(kind='common-slot-anchor-initial', trainedUpdates=0))
        zero, cache = model.forward(nodes)
        assert cache['movedOwners'] == 0 and not np.any(zero[:len(decoder.positions)])
        control = native.request(wire(zero, 'MEASURE'))
        assert control['legal'] and control['hard'] == control['individualHard'] == control['spacing'] == 0, control
        with (args.out / 'training.jsonl').open('x') as trace, (args.out / 'actions.jsonl').open('x') as actions:
            trainer = RewardHeadTrainer(model, nodes, native.request, wire, trace,
                args.seed, args.sigma, 2, args.rate, KEYS, SCALES, cache_size=0)
            assert len(head_vector(model, KEYS, SCALES)) == 27
            train_started = time.monotonic()
            for step in range(1, args.iterations + 1):
                if time.monotonic() - train_started >= args.seconds:
                    break
                trainer.sigma = args.sigma * SIGMAS[((step - 1) // 16) % len(SIGMAS)]
                progress = trainer.step()
                if step % 4 == 0 and trainer.ever_updated:
                    action, cache = model.forward(nodes)
                    checkpoint = args.out / f'model-step-{step:03}.npz'
                    model.save(checkpoint, dict(kind='common-slot-anchor-full-native-reward-trained', trainedUpdates=step))
                    result = native.request(wire(action))
                    attempts += 1
                    reasons[result['reason']] += 1
                    record = dict(step=step, checkpoint=str(checkpoint), checkpointSha256=digest(checkpoint),
                        wireSha256=hashlib.sha256(wire(action).encode()).hexdigest(), movedOwners=cache['movedOwners'],
                        result=result, trainedHeadChanged=True, rewardSummary=progress)
                    actions.write(json.dumps(record) + '\n')
                    actions.flush()
                    if result['accepted']:
                        accepted += 1
                        best = action.copy()
                        best_step = step
                        model.save(args.out / 'best-model.npz', dict(kind='common-slot-anchor-best-trained', trainedUpdates=step))
                        print(json.dumps(dict(view=args.view, step=step, accepted=True, visual=result['visual'],
                            individualVisual=result['individualVisual'], movedOwners=cache['movedOwners'])), flush=True)
            assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out / 'best-action.npy', best)
    stats = json.loads((args.out / 'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated'] == attempts and stats['acceptedActions'] == accepted
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    report = dict(kind='common-original-card-anchor-neural-slot-reward-learning-v1', view=args.view,
        seed=args.seed, sourceDirectory=str(directory), sourceInputs=inputs,
        sourceBinding=str(args.source_binding), sourceBindingSha256=digest(args.source_binding),
        environment=str(args.environment), environmentSha256=digest(args.environment),
        initialVisual=initial_visual, initialIndividualVisual=native.initial['individualVisual'],
        zeroHeadControl=control, zeroHeadControlNeverSubmitted=True, final=stats,
        trainableParameters=27, parameterKeys=KEYS, parameterScales=SCALES,
        initialSigma=args.sigma, sigmaScheduleMultipliers=SIGMAS, sigmaStepsPerLevel=16,
        rate=args.rate, directionsPerUpdate=2, iterationsLimit=args.iterations, secondsLimit=args.seconds,
        updatesExecuted=trainer.iterations, newTrainingUpdates=trainer.iterations,
        rewardProbes=trainer.measurements, fullNativeRewardMeasurements=trainer.native_measurements,
        learnedHeadChanged=trainer.ever_updated, attempts=attempts, accepted=accepted, bestStep=best_step,
        reasons=dict(reasons), physicalOwners=len(decoder.positions), fullCanonicalEdges=len(decoder.provider.full_edges),
        encoderFixed=True, featureInputDtype='float32', sourceSlotAndOriginalDimensionsFixed=True,
        oneInteriorAnchorPerOriginalCard=True, inferenceUsesNoFutureMetrics=True,
        nativeCoordinateSearchOrRepairs=0, onlyUpdatedNeuralHeadsSubmitted=True,
        checkpointMode='trained neural outputs; strict-best source retained when no accepted output',
        trainingSha256=digest(args.out / 'training.jsonl'), actionsSha256=digest(args.out / 'actions.jsonl'),
        observationsSha256=digest(args.out / 'observations.npz'), initialModelSha256=digest(args.out / 'initial-model.npz'),
        codeSha256={name: digest(Path(__file__).parent / name) for name in CODE},
        wallSeconds=time.monotonic() - started, browserVerified=False, promoted=False)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('view', 'updatesExecuted', 'rewardProbes', 'attempts',
        'accepted', 'bestStep', 'reasons', 'wallSeconds')} | dict(initialVisual=initial_visual,
        zeroHeadVisual=control['visual'], finalVisual=stats['visual'])), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview', 'individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=114107)
    parser.add_argument('--sigma', type=float, default=.005)
    parser.add_argument('--rate', type=float, default=.003)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=30.)
    run(parser.parse_args())
