"""Learn shared card translations while retaining original card-relative ports."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np

from compact_source_port_policy import SourcePortPatchActor
from geometry_world_model import digest
from joint_reward_training import head_vector, set_head_vector, head_hash, adam_head
from run_compact_patch_reward import patch_support, objective, KEYS, SCALES
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_anchor_pair_policy import Native, wire
from learned_global_replay import INPUT_FILES

SIGMA_MULTIPLIERS = (1., 3., 8., 16.)
SIGMA_STEPS = 16


def run(args):
    assert 1 <= args.patch_size <= 16 and 1 <= args.iterations <= 128 and 0 < args.seconds <= 30
    args.out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    source = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    inputs = {name: digest(args.directory / name) for name in INPUT_FILES}
    assert str(args.directory) == source['directory'] and inputs == source['inputs']
    decoder = WalkDecoder(args.directory)
    native = Native(args.environment, args.directory, args.out / 'learned.tsv', args.view == 'overview', True)
    best = None
    best_step = None
    attempts = accepted = updates = probes = 0
    reasons = Counter()
    learned_changed = False
    best_visual = native.initial['visual']
    try:
        assert best_visual == source['expectedVisual']
        nodes = np.array([node['features'] for node in native.initial['nodes']])
        active, support = patch_support(decoder, nodes, args.patch_size, args.root_rank)
        model = SourcePortPatchActor(nodes, decoder, active, args.max_step, args.seed)
        np.savez_compressed(args.out / 'observations.npz', nodes=nodes, active=active)
        model.save(args.out / 'initial-model.npz', dict(kind='source-port-patch-initial', trainedUpdates=0))
        baseline = native.request(wire(model.forward(nodes)[0], 'MEASURE'))
        assert baseline['legal'] and baseline['visual'] == best_visual
        assert baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        first = np.zeros(18)
        second = first.copy()
        train_started = time.monotonic()
        with (args.out / 'training.jsonl').open('x') as trace, (args.out / 'actions.jsonl').open('x') as actions:
            for step in range(1, args.iterations + 1):
                if time.monotonic() - train_started >= args.seconds:
                    break
                # Fixed parameter-noise schedule; neither cost nor rejection selects it.
                sigma = args.sigma * SIGMA_MULTIPLIERS[((step - 1) // SIGMA_STEPS) % len(SIGMA_MULTIPLIERS)]
                base = head_vector(model, KEYS, SCALES)
                gradient = np.zeros_like(base)
                samples = []
                for direction in range(2):
                    seed = args.seed + step * 100 + direction
                    noise = np.random.default_rng(seed).normal(size=base.shape)
                    values = []
                    pair = []
                    for sign in (1., -1.):
                        set_head_vector(model, base + sign * sigma * noise, KEYS, SCALES)
                        command = wire(model.forward(nodes)[0], 'MEASURE')
                        result = native.request(command)
                        probes += 1
                        values.append(objective(result))
                        pair.append(dict(sign=sign, wireSha256=hashlib.sha256(command.encode()).hexdigest(), result=result))
                    gradient += (values[0] - values[1]) / (4 * sigma) * noise
                    samples.append(dict(seed=seed, samples=pair))
                head, first, second = adam_head(base, gradient, first, second, step, args.rate)
                set_head_vector(model, head, KEYS, SCALES)
                changed = not np.array_equal(base, head)
                learned_changed |= changed
                updates += 1
                row = dict(step=step, sigma=sigma, rate=args.rate, baseHeadSha256=head_hash(base), samples=samples,
                           gradientNorm=float(np.linalg.norm(gradient)), trainedHeadSha256=head_hash(head), headChanged=changed)
                trace.write(json.dumps(row) + '\n')
                trace.flush()
                if step % 4 == 0:
                    proposed = model.forward(nodes)[0]
                    result = native.request(wire(proposed))
                    attempts += 1
                    reasons[result['reason']] += 1
                    checkpoint = args.out / f'model-step-{step:03}.npz'
                    model.save(checkpoint, dict(kind='source-port-patch-native-reward-trained', trainedUpdates=step))
                    record = dict(step=step, checkpoint=str(checkpoint), checkpointSha256=digest(checkpoint),
                                  wireSha256=hashlib.sha256(wire(proposed).encode()).hexdigest(), result=result)
                    actions.write(json.dumps(record) + '\n')
                    actions.flush()
                    if result['accepted']:
                        best = proposed.copy()
                        best_step = step
                        best_visual = result['visual']
                        accepted += 1
            assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out / 'best-action.npy', best)
    stats = json.loads((args.out / 'learned.tsv.stats.json').read_text())
    assert (stats['visual'], stats['policyActionsEvaluated'], stats['acceptedActions']) == (best_visual, attempts, accepted)
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    files = ('run_compact_source_port_reward.py', 'compact_source_port_policy.py', 'compact_patch_neural_policy.py',
             'run_compact_patch_reward.py', 'joint_reward_training.py', 'joint_separation_policy.py',
             'run_anchor_pair_walk.py', 'run_anchor_pair_policy.py')
    report = dict(kind='compact-source-spacing-original-port-native-reward-learning-v1', view=args.view,
        seed=args.seed, sourceDirectory=str(args.directory), sourceBinding=str(args.source_binding),
        sourceBindingSha256=digest(args.source_binding), sourceInputs=inputs, environment=str(args.environment),
        environmentSha256=digest(args.environment), support=support, patchSize=args.patch_size, rootRank=args.root_rank,
        maxStep=args.max_step, initialSigma=args.sigma, rate=args.rate, directionsPerUpdate=2,
        sigmaScheduleMultipliers=SIGMA_MULTIPLIERS, sigmaScheduleStepsPerLevel=SIGMA_STEPS,
        trainableParameters=18, parameterKeys=KEYS, parameterScales=SCALES, encoderFixed=True,
        decoderBuffersImmutable=True, iterationsLimit=args.iterations, secondsLimit=args.seconds,
        updatesExecuted=updates, newTrainingUpdates=updates, rewardProbes=probes, learnedHeadChanged=learned_changed,
        attempts=attempts, accepted=accepted, bestStep=best_step, reasons=dict(reasons), initialVisual=baseline['visual'],
        initialIndividualVisual=baseline['individualVisual'], final=stats,
        nativeProbesOnlySupplyLearningLabels=True, cardCoordinatesAreTrainableParameters=False,
        nativeCoordinateSearchOrRepairs=0, allSavedGeometryFromTrainedNetworkForward=True,
        sourceFrameAndOriginalCardSizesRetained=True, endpointOffsetsAlwaysZero=True,
        learningScheduleIndependentOfNativeAcceptance=True, inferenceFeatureInputDtype='float32',
        trainingSha256=digest(args.out/'training.jsonl'), actionsSha256=digest(args.out/'actions.jsonl'),
        observationsSha256=digest(args.out/'observations.npz'), initialModelSha256=digest(args.out/'initial-model.npz'),
        codeSha256={name: digest(Path(__file__).parent/name) for name in files}, wallSeconds=time.monotonic()-started)
    (args.out/'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('view', 'support', 'updatesExecuted', 'rewardProbes',
          'attempts', 'accepted', 'bestStep', 'reasons', 'wallSeconds')} |
          dict(initialVisual=baseline['visual'], finalVisual=best_visual)), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--view', choices=['overview', 'individual'], required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=110103)
    p.add_argument('--patch-size', type=int, default=8)
    p.add_argument('--root-rank', type=int, default=0)
    p.add_argument('--max-step', type=float, default=256.)
    p.add_argument('--sigma', type=float, default=.025)
    p.add_argument('--rate', type=float, default=.005)
    p.add_argument('--iterations', type=int, default=64)
    p.add_argument('--seconds', type=float, default=30.)
    run(p.parse_args())
