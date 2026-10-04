#!/usr/bin/env python3
"""Model-selected single-owner moves with recorded outcomes and immutable source."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import cycle_features
from learned_global_replay import INPUT_FILES
from owner_amplitude_cycle_model import SCHEMA, action_features
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_fraction_conditioned_policy import sample_order
from run_global_mixed_owner_cycles import hashes as parent_hashes
from single_owner_cycle_model import ACTION_SCHEMA, MODEL_KIND, AMPLITUDES, action, allowed_indices, load, rank, schedule, vocabulary
from train_anchor_pair_outcomes import outcome_target


def hashes():
    return parent_hashes() | {name: digest(Path(__file__).parent/name) for name in
        ['single_owner_cycle_model.py', 'run_single_owner_cycle_policy.py', 'run_single_owner_cycle_policy_v2.py', 'train_anchor_pair_outcomes.py']}


def prepare(model, decoder, nodes, view, seed, triples, budget):
    cycles, support = vocabulary(decoder, nodes, seed, triples)
    scores, feature_hashes, error = rank(model, decoder, nodes, cycles, view == 'overview')
    allowed = allowed_indices(nodes, cycles)
    selected_order, noise, policy = sample_order(scores.ravel()[allowed, None], seed+1000, 1.)
    chosen, dedup = schedule(decoder, cycles, allowed[selected_order], budget)
    return cycles, support, scores, feature_hashes, error, allowed, noise, policy, chosen, dedup


def run(args):
    assert 1 <= args.budget <= 1024 and 1 <= args.triples <= 1024 and 0 < args.seconds <= 30
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    decoder = WalkDecoder(args.directory); model, metadata = load(args.checkpoint)
    inputs = {n: digest(args.directory/n) for n in INPUT_FILES}
    binding = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert str(args.directory) == binding['directory'] and inputs == binding['inputs']
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    best = None; attempts = accepted = improving = ties = 0; reasons = Counter(); wires = set()
    best_visual = native.initial['visual']; xs = []; ys = []; gains = []; actual_cycles = []; actual_amplitudes = []
    try:
        zero = native.request(wire(decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)), 'MEASURE'))
        assert zero['legal'] and zero['hard'] == zero['individualHard'] == zero['spacing'] == 0
        nodes = np.array([row['features'] for row in native.initial['nodes']]); rank_started = time.monotonic()
        cycles, support, scores, feature_hashes, error, allowed, noise, policy, chosen, dedup = prepare(
            model, decoder, nodes, args.view, args.seed, args.triples, args.budget)
        rank_seconds = time.monotonic()-rank_started
        # Feature construction is batched; geometry is still exclusively the NN's
        # fixed ordered selection, decoded from the unchanged source.
        features, _ = action_features(nodes, decoder, cycles[chosen[:, 0]], AMPLITUDES[chosen[:, 1]], args.view == 'overview')
        np.savez_compressed(args.out/'observations.npz', node_features=nodes, cycles=cycles, amplitudes=AMPLITUDES,
            scores=scores, allowed_indices=allowed, gumbel=noise, proposal_schedule=chosen)
        allowed_lookup = {int(v): i for i, v in enumerate(allowed)}
        with (args.out/'actions.jsonl').open('x') as stream:
            for ci, ai in chosen:
                if time.monotonic()-started >= args.seconds: break
                ci, ai = int(ci), int(ai); cycle = cycles[ci]; amplitude = AMPLITUDES[ai]
                proposed = action(decoder, cycle, amplitude); command = wire(proposed)
                h = hashlib.sha256(command.encode()).hexdigest(); assert h not in wires; wires.add(h)
                result = native.request(command); gain = float(native.initial['visual']-result['visual']) if result['legal'] else np.nan
                ni = allowed_lookup[int(np.ravel_multi_index((ci, ai), scores.shape))]
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'amplitudeIndex': ai,
                    'cycle': cycle.tolist(), 'amplitudes': amplitude.tolist(), 'score': float(scores[ci, ai]),
                    'gumbel': float(noise[ni, 0]), 'wireSha256': h, 'result': result})+'\n')
                xs.append(features[attempts].astype(np.float32)); ys.append(outcome_target(result, native.initial['visual']))
                gains.append(gain); actual_cycles.append(cycle); actual_amplitudes.append(amplitude)
                attempts += 1; reasons[result['reason']] += 1; improving += int(gain > 0)
                ties += int(result['legal'] and result['visual'] == native.initial['visual'])
                if result['accepted']: best = proposed.copy(); best_visual = result['visual']; accepted += 1
        assert native.request('SAVE')['saved']
    finally: native.close()
    verify_saved(args.directory, args.out, decoder, best)
    np.savez_compressed(args.out/'labels.npz', x=np.array(xs), y=np.array(ys), actual_gain=np.array(gains),
        cycles=np.array(actual_cycles), amplitudes=np.array(actual_amplitudes))
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'], stats['policyActionsEvaluated'], stats['acceptedActions']) == (best_visual, attempts, accepted)
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    if best is not None: np.save(args.out/'best-action.npy', best)
    report = {'kind': 'single-owner-partial-cycle-Captain-policy-v2', 'featureSchema': SCHEMA, 'actionSchema': ACTION_SCHEMA,
        'sourceBinding': str(args.source_binding), 'sourceBindingSha256': digest(args.source_binding), 'view': args.view, 'sourceDirectory': str(args.directory), 'sourceInputs': inputs,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment), 'seed': args.seed,
        'triples': args.triples, 'budget': args.budget, 'secondsLimit': args.seconds, 'support': support,
        'candidateAmplitudes': AMPLITUDES.tolist(), 'policy': policy,
        'policyProbabilityColumnMeaning': 'single flattened source-gated candidate pool',
        'onlyDirectlyConflictingOwnersMove': True, 'deduplication': dedup,
        'attempts': attempts, 'accepted': accepted, 'improvingLegalActions': improving, 'legalTies': ties, 'reasons': dict(reasons),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'], 'final': stats,
        'trainedOnSingleOwnerMoves': metadata['kind'] == MODEL_KIND, 'newTrainingUpdatesDuringInference': 0,
        'inferenceFeatureInputDtype': metadata.get('inferenceFeatureInputDtype', 'float64'),
        'scaleFeatureMaxError': error, 'featureChunkHashes': feature_hashes,
        'rankingAndScheduleSeconds': rank_seconds, 'wallSeconds': time.monotonic()-started,
        'sourceScoresUnchangedAcrossActions': True, 'policyOrderFixedBeforeNativeMeasurement': True,
        'uniqueWireHashesWithinStage': len(wires), 'allHistoryWireExclusionClaimed': False,
        'coordinateRepairs': 0, 'nativeSearchCalls': 0, 'savedGeometryMatchesModelOutput': True,
        'observationsSha256': digest(args.out/'observations.npz'), 'actionsSha256': digest(args.out/'actions.jsonl'),
        'labelsSha256': digest(args.out/'labels.npz'), 'codeSha256': hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ['view', 'attempts', 'accepted', 'improvingLegalActions', 'legalTies', 'reasons',
        'rankingAndScheduleSeconds', 'wallSeconds']} | {'initialVisual': report['initialVisual'], 'finalVisual': best_visual}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--directory', type=Path, required=True); p.add_argument('--checkpoint', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--view', choices=['overview', 'individual'], required=True); p.add_argument('--seed', type=int, default=96301)
    p.add_argument('--triples', type=int, default=1024); p.add_argument('--budget', type=int, default=1024)
    p.add_argument('--seconds', type=float, default=30); run(p.parse_args())
