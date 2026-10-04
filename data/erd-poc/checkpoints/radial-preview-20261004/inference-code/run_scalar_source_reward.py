"""Learn nine shared scalar NN parameters from immutable-source Native rewards."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from geometry_world_model import digest
from shared_card_phase_policy import SharedCardPhasePolicy
from radial_leaf_policy import RadialLeafPolicy
from joint_reward_training import RewardHeadTrainer, head_vector
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved

KEYS, SCALES = ('wo', 'bo'), (1., 1.)
CODE = ('run_scalar_source_reward.py', 'shared_card_phase_policy.py', 'radial_leaf_policy.py',
    'joint_reward_training.py', 'joint_neural_ports.py', 'joint_grouped_routes.py',
    'run_anchor_pair_walk.py', 'run_anchor_pair_policy.py')


def run(args):
    assert 1 <= args.iterations <= 128 and 0 < args.seconds <= 30
    assert 0 < args.sigma <= .05 and 0 < args.rate <= .05
    binding = json.loads(args.source_binding.read_text())
    spec = binding['viewSources'][args.view]
    directory = Path(spec['directory'])
    inputs = {name:digest(directory / name) for name in INPUT_FILES}
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
        model = (SharedCardPhasePolicy(nodes, decoder, args.seed, args.span) if args.policy == 'phase'
            else RadialLeafPolicy(nodes, decoder, args.seed, args.max_step))
        np.savez_compressed(args.out / 'observations.npz', nodes=nodes)
        model.save(args.out / 'initial-model.npz', dict(kind=args.policy, trainedUpdates=0))
        zero, control_info = model.forward(nodes)
        assert not np.any(zero)
        control = native.request(wire(zero, 'MEASURE'))
        assert control['legal'] and control['visual'] == spec['expectedVisual']
        assert control['hard'] == control['individualHard'] == control['spacing'] == 0
        with (args.out / 'training.jsonl').open('x') as trace, (args.out / 'actions.jsonl').open('x') as actions:
            trainer = RewardHeadTrainer(model, nodes, native.request, wire, trace, args.seed, args.sigma,
                2, args.rate, KEYS, SCALES, cache_size=0)
            assert len(head_vector(model, KEYS, SCALES)) == 9
            train_started = time.monotonic()
            for step in range(1, args.iterations + 1):
                if time.monotonic() - train_started >= args.seconds:
                    break
                trainer.sigma = args.sigma * (1., 3., 8., 16.)[((step - 1) // 8) % 4]
                progress = trainer.step()
                if step % 4 == 0 and trainer.ever_updated:
                    action, info = model.forward(nodes)
                    checkpoint = args.out / f'model-step-{step:03}.npz'
                    model.save(checkpoint, dict(kind=args.policy, trainedUpdates=step))
                    result = native.request(wire(action))
                    attempts += 1
                    reasons[result['reason']] += 1
                    row = dict(step=step, checkpoint=str(checkpoint), checkpointSha256=digest(checkpoint),
                        wireSha256=hashlib.sha256(wire(action).encode()).hexdigest(), policyInfo=info,
                        result=result, trainedHeadChanged=True, rewardSummary=progress)
                    actions.write(json.dumps(row) + '\n')
                    actions.flush()
                    if result['accepted']:
                        accepted += 1
                        best, best_step = action.copy(), step
                        model.save(args.out / 'best-model.npz', dict(kind=args.policy, trainedUpdates=step))
                    print(json.dumps(dict(step=step, result=result, policyInfo=info)), flush=True)
            assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out / 'best-action.npy', best)
    stats = json.loads((args.out / 'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated'] == attempts and stats['acceptedActions'] == accepted
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    report = dict(kind='source-conditioned-shared-scalar-nn-native-reward-v1', policy=args.policy,
        view=args.view, seed=args.seed, sourceDirectory=str(directory), sourceInputs=inputs,
        sourceBinding=str(args.source_binding), sourceBindingSha256=digest(args.source_binding),
        environment=str(args.environment), environmentSha256=digest(args.environment),
        phaseSpan=args.span, maxStep=args.max_step, initialVisual=native.initial['visual'],
        initialIndividualVisual=native.initial['individualVisual'], zeroHeadControl=control,
        zeroHeadSourceIdentity=True, zeroHeadControlNeverSubmitted=True, zeroHeadControlInfo=control_info, final=stats,
        trainableParameters=9, parameterKeys=KEYS, parameterScales=SCALES, initialSigma=args.sigma,
        sigmaScheduleMultipliers=(1., 3., 8., 16.), sigmaStepsPerLevel=8, rate=args.rate,
        directionsPerUpdate=2, iterationsLimit=args.iterations, secondsLimit=args.seconds,
        updatesExecuted=trainer.iterations, newTrainingUpdates=trainer.iterations, rewardProbes=trainer.measurements,
        fullNativeRewardMeasurements=trainer.native_measurements, learnedHeadChanged=trainer.ever_updated,
        attempts=attempts, accepted=accepted, bestStep=best_step, reasons=dict(reasons),
        physicalOwners=len(decoder.positions), fullCanonicalEdges=len(decoder.provider.full_edges),
        encoderFixed=True, featureInputDtype='float32', originalCardDimensionsAndFrameRetained=True,
        fixedSourceDecodingBeforeMeasurement=True, learningScheduleIndependentOfAcceptance=True,
        inferenceUsesNoFutureMetrics=True, nativeCoordinateSearchOrRepairs=0, onlyUpdatedNeuralHeadsSubmitted=True,
        originalCardRelativePortsRetained=args.policy == 'radial', onlySingleRelationshipOwnersMoved=args.policy == 'radial',
        onlyEndpointPhasesChanged=args.policy == 'phase', fullNativeRewardScoring=True,
        trainingSha256=digest(args.out / 'training.jsonl'), actionsSha256=digest(args.out / 'actions.jsonl'),
        observationsSha256=digest(args.out / 'observations.npz'), initialModelSha256=digest(args.out / 'initial-model.npz'),
        codeSha256={name:digest(Path(__file__).parent / name) for name in CODE},
        wallSeconds=time.monotonic() - started, productLoadOrBrowserVerified=False, promoted=False)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key:report[key] for key in ('policy', 'view', 'updatesExecuted', 'rewardProbes', 'attempts',
        'accepted', 'bestStep', 'reasons', 'wallSeconds')} | dict(initialVisual=report['initialVisual'],
        finalVisual=stats['visual'])), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--policy', choices=('phase', 'radial'), required=True)
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview', 'individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=119107)
    parser.add_argument('--span', type=float, default=.005)
    parser.add_argument('--max-step', type=float, default=4096.)
    parser.add_argument('--sigma', type=float, default=.025)
    parser.add_argument('--rate', type=float, default=.012)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=30.)
    run(parser.parse_args())
