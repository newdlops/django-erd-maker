#!/usr/bin/env python3
"""Neural partial moves across card sizes, retaining all dimensions and native gates."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import cycle_features
from learned_global_replay import INPUT_FILES
from owner_amplitude_cycle_model import SCHEMA, AMPLITUDES, expand_actions, rank
from float32_input_owner_model import load
from mixed_size_owner_cycles import mixed_action as owner_action
from global_mixed_owner_support import support as vocabulary
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_fraction_conditioned_policy import sample_order
from run_signed_cycle_policy import hashes as parent_hashes
from signed_cycle_model import signed_action, expand_actions as uniform_features


def hashes():
    return parent_hashes() | {name: digest(Path(__file__).parent/name) for name in
        ['owner_amplitude_cycle_model.py', 'run_owner_amplitude_cycles.py', 'signed_trained_critic.py',
         'mixed_size_owner_cycles.py', 'run_mixed_size_owner_cycles.py',
         'float32_input_owner_model.py', 'run_float32_mixed_owner_cycles.py',
         'global_mixed_owner_support.py', 'run_global_mixed_owner_cycles.py']}


def compatibility(decoder, nodes, cycles, overview):
    from itertools import combinations
    selected = cycles[:16]; core, _ = cycle_features(nodes, decoder, selected, overview)
    same = np.array([word for group in decoder.groups if len(group) >= 3
                     for word in list(combinations(map(int, group[:6]), 3))][:16], dtype=np.int32)
    assert len(same) > 0
    feature_checks = wire_checks = 0
    for fraction in [.001, .02, -.005, -.1]:
        assert np.array_equal(expand_actions(core, nodes, decoder, selected, np.full((len(selected), 3), fraction)),
                              uniform_features(core, nodes, decoder, selected, np.full(len(selected), fraction)))
        feature_checks += len(selected)
        for cycle in same:
            assert wire(owner_action(decoder, cycle, np.full(3, fraction))) == wire(signed_action(decoder, cycle, fraction))
            wire_checks += 1
    return {'mixedWordsCommonAmplitudeFeatureChecks': feature_checks,
            'sameSizeCommonAmplitudeWireChecks': wire_checks, 'nativeCompatibilityProposals': 0}


def motion_check(decoder, cycle, amplitudes, action):
    p = decoder.positions[cycle]; raw_delta = amplitudes[:, None]*(np.roll(p, -1, axis=0)-p)
    quantized = action[:len(decoder.positions)]
    other = np.setdiff1d(np.arange(len(quantized)), cycle)
    assert not quantized[other].any()
    assert np.max(abs(quantized[cycle]-raw_delta)) <= .005+1e-8
    predicted_center = p.mean(0)+raw_delta.sum(0)/3
    assert np.max(abs((p+quantized[cycle]).mean(0)-predicted_center)) <= .005+1e-8
    shift = float(np.linalg.norm(predicted_center-p.mean(0)))
    moved = int(np.count_nonzero(np.any(quantized[cycle] != 0, axis=1)))
    assert moved >= 2
    return shift, moved


def run(args):
    assert 1 <= args.budget <= 1024 and 0 < args.seconds <= 30 and 1 <= args.triples <= 1024
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    model, metadata = load(args.checkpoint)
    inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert inputs == metadata['sourceInputsByView'][args.view]
    decoder = WalkDecoder(args.directory)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    best = None; best_visual = native.initial['visual']; attempts = accepted = ties = 0
    reasons = Counter(); patterns = Counter(); moved_counts = Counter(); shifted = 0; maximum_shift = 0.
    try:
        zero = native.request(wire(decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)), 'MEASURE'))
        assert zero['legal'] and zero['hard'] == zero['individualHard'] == zero['spacing'] == 0
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        cycles, support = vocabulary(decoder, nodes, args.support, args.seed, args.triples)
        parity = compatibility(decoder, nodes, cycles, args.view == 'overview')
        rank_started = time.monotonic()
        scores, feature_hashes, error = rank(model, decoder, nodes, cycles, args.view == 'overview')
        ranking_seconds = time.monotonic()-rank_started
        order, noise, policy = sample_order(scores, args.seed+1000, 1.)
        np.savez_compressed(args.out/'observations.npz', node_features=nodes, cycles=cycles,
                            amplitudes=AMPLITUDES, scores=scores, gumbel=noise)
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:args.budget]:
                if time.monotonic()-started >= args.seconds: break
                ci, ai = map(int, np.unravel_index(index, scores.shape)); cycle = cycles[ci]; amplitudes = AMPLITUDES[ai]
                action = owner_action(decoder, cycle, amplitudes); shift, moved = motion_check(decoder, cycle, amplitudes, action)
                command = wire(action); result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'amplitudeIndex': ai,
                    'cycle': cycle.tolist(), 'amplitudes': amplitudes.tolist(), 'score': float(scores[ci, ai]),
                    'gumbel': float(noise[ci, ai]), 'wireSha256': hashlib.sha256(command.encode()).hexdigest(),
                    'rawCentroidShiftL2': shift, 'movedOwners': moved, 'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1; patterns[str(ai)] += 1; moved_counts[str(moved)] += 1
                shifted += int(shift > 1e-8); maximum_shift = max(maximum_shift, shift)
                ties += int(result['legal'] and result['visual'] == native.initial['visual'])
                if result['accepted']:
                    accepted += 1; best = action.copy(); best_visual = result['visual']
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory, args.out, decoder, best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'], stats['policyActionsEvaluated'], stats['acceptedActions']) == (best_visual, attempts, accepted)
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    if best is not None: np.save(args.out/'best-action.npy', best)
    report = {'kind': 'consistent-input-precision-mixed-size-owner-amplitude-Captain-policy-v1', 'featureSchema': SCHEMA,
        'view': args.view, 'sourceDirectory': str(args.directory), 'sourceInputs': inputs,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'seed': args.seed, 'supportKind': args.support, 'triples': args.triples, 'support': support,
        'candidateAmplitudes': AMPLITUDES.tolist(), 'amplitudeScope': 'independent sign-or-zero patterns at a common magnitude, at least two nonzero owners; common scalar patterns excluded',
        'policy': policy, 'budget': args.budget, 'secondsLimit': args.seconds, 'attempts': attempts,
        'accepted': accepted, 'legalTies': ties, 'reasons': dict(reasons), 'proposedPatternCounts': dict(patterns),
        'movedOwnerCounts': dict(moved_counts), 'proposalsChangingRawTripletCentroid': shifted,
        'maximumRawCentroidShiftL2': maximum_shift, 'initialVisual': native.initial['visual'],
        'initialIndividualVisual': native.initial['individualVisual'], 'final': stats, 'rankingSeconds': ranking_seconds,
        'featureChunkHashes': feature_hashes, 'scaleFeatureMaxError': error, 'compatibility': parity,
        'wallSeconds': time.monotonic()-started, 'trainedOnPerOwnerAmplitudes': metadata['schema'] == SCHEMA,
        'trainedOnMixedSizeWords': bool(metadata.get('trainedOnMixedSizeWords', False)),
        'inferenceFeatureInputDtype': metadata.get('inferenceFeatureInputDtype', 'float64'),
        'featureChunkHashesRepresent': 'raw float64 observations before any declared input quantization',
        'newTrainingUpdatesDuringInference': 0, 'coordinateRepairs': 0, 'nativeSearchCalls': 0,
        'policyOrderFixedBeforeNativeMeasurement': True, 'savedGeometryMatchesModelOutput': True,
        'allHistoryWireExclusionClaimed': False, 'observationsSha256': digest(args.out/'observations.npz'),
        'actionsSha256': digest(args.out/'actions.jsonl'), 'codeSha256': hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ['view', 'attempts', 'accepted', 'legalTies', 'initialVisual',
        'reasons', 'proposalsChangingRawTripletCentroid', 'rankingSeconds', 'wallSeconds']} | {'finalVisual': best_visual}), flush=True)


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert report['codeSha256'] == hashes() and digest(report['checkpoint']) == report['checkpointSha256']
    assert digest(report['environment']) == report['environmentSha256']
    assert {n: digest(directory/n) for n in INPUT_FILES} == report['sourceInputs']
    assert digest(args.out/'observations.npz') == report['observationsSha256'] and digest(args.out/'actions.jsonl') == report['actionsSha256']
    model, metadata = load(Path(report['checkpoint'])); assert metadata == report['checkpointTraining']
    decoder = WalkDecoder(directory)
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        nodes = saved['node_features'].copy()
        cycles, support = vocabulary(decoder, nodes, report['supportKind'], report['seed'], report['triples'])
        assert support == report['support'] and np.array_equal(cycles, saved['cycles'])
        assert np.array_equal(AMPLITUDES, saved['amplitudes'])
        assert compatibility(decoder, nodes, cycles, report['view'] == 'overview') == report['compatibility']
        scores, feature_hashes, error = rank(model, decoder, nodes, cycles, report['view'] == 'overview')
        assert feature_hashes == report['featureChunkHashes'] and np.array_equal(scores, saved['scores'])
        assert error == report['scaleFeatureMaxError']
        order, noise, policy = sample_order(scores, report['seed']+1000, 1.)
        assert policy == report['policy'] and np.array_equal(noise, saved['gumbel'])
    attempts = accepted = ties = shifted = 0; best = None; reasons = Counter(); patterns = Counter(); moved_counts = Counter(); maximum_shift = 0.
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); ci, ai = map(int, np.unravel_index(order[attempts], scores.shape))
        assert row['rank'] == attempts and (row['cycleIndex'], row['amplitudeIndex']) == (ci, ai)
        assert row['cycle'] == cycles[ci].tolist() and row['amplitudes'] == AMPLITUDES[ai].tolist()
        assert row['score'] == scores[ci, ai] and row['gumbel'] == noise[ci, ai]
        proposed = owner_action(decoder, cycles[ci], AMPLITUDES[ai]); shift, moved = motion_check(decoder, cycles[ci], AMPLITUDES[ai], proposed)
        assert hashlib.sha256(wire(proposed).encode()).hexdigest() == row['wireSha256']
        assert row['rawCentroidShiftL2'] == shift and row['movedOwners'] == moved
        attempts += 1; reasons[row['result']['reason']] += 1; patterns[str(ai)] += 1; moved_counts[str(moved)] += 1
        shifted += int(shift > 1e-8); maximum_shift = max(maximum_shift, shift)
        ties += int(row['result']['legal'] and row['result']['visual'] == report['initialVisual'])
        if row['result']['accepted']: best = proposed; accepted += 1
    assert (attempts, accepted, ties, shifted) == (report['attempts'], report['accepted'], report['legalTies'], report['proposalsChangingRawTripletCentroid'])
    assert dict(reasons) == report['reasons'] and dict(patterns) == report['proposedPatternCounts'] and dict(moved_counts) == report['movedOwnerCounts']
    assert maximum_shift == report['maximumRawCentroidShiftL2']
    if best is not None: assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'actionsReplayed': attempts, 'accepted': accepted,
        'allScoresAndGumbelOrderReconstructed': True, 'commonAmplitudeFeatureAndWireCompatibility': True,
        'centroidShiftAndQuantizationBoundsVerified': True, 'savedGeometryReconstructed': True,
        'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['run', 'replay']); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--directory', type=Path); p.add_argument('--checkpoint', type=Path); p.add_argument('--environment', type=Path)
    p.add_argument('--view', choices=['individual', 'overview']); p.add_argument('--seed', type=int, default=88237)
    p.add_argument('--support', choices=['local', 'global'], default='local'); p.add_argument('--triples', type=int, default=1024)
    p.add_argument('--budget', type=int, default=1024); p.add_argument('--seconds', type=float, default=30)
    args = p.parse_args(); (run if args.mode == 'run' else replay)(args)
