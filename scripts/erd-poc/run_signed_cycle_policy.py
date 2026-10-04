#!/usr/bin/env python3
"""Frozen learned selection of new signed cycles on immutable Captain sources."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import cycle_features
from fractional_cycle_model import load, expand_actions as positive_features
from learned_global_replay import INPUT_FILES
from local_positive_cycle_support import local_cycles
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_directed_cycle_policy import coordinated_cycles
from run_fraction_conditioned_policy import sample_order, code_hashes as parent_hashes
from run_fractional_cycle_transfer import fractional_action
from signed_cycle_model import SCHEMA, FRACTIONS, expand_actions, rank, signed_action


def hashes():
    return parent_hashes() | {name: digest(Path(__file__).parent/name) for name in
        ['signed_cycle_model.py', 'run_signed_cycle_policy.py', 'local_positive_cycle_support.py']}


def vocabulary(decoder, nodes, kind, seed, count):
    # Older positive actions do not exclude new negative geometry. Exclusions
    # are deliberately empty and no all-history uniqueness claim is made.
    builder = local_cycles if kind == 'local' else coordinated_cycles
    cycles, support = builder(decoder, nodes, np.empty((0, 3), dtype=np.int32), seed, count)
    support = {k: v for k, v in support.items() if k != 'previouslyMeasuredCyclesSubmitted'}
    support['priorPositiveWordsMayBeReused'] = True
    support['nativeMetricBasedSupportSelection'] = False
    support['coordinatesProposedBySupportBuilder'] = False
    return cycles, support


def compatibility(decoder, nodes, cycles, overview):
    selected = cycles[:32]; core, _ = cycle_features(nodes, decoder, selected, overview)
    checks = 0
    for fraction in [.001, .02, 1.]:
        f = np.full(len(selected), fraction)
        assert np.array_equal(expand_actions(core, nodes, decoder, selected, f),
                              positive_features(core, nodes, decoder, selected, f))
        for cycle in selected:
            assert wire(signed_action(decoder, cycle, fraction)) == wire(fractional_action(decoder, cycle, fraction))
            checks += 1
    return {'positiveFeatureParityChecks': checks, 'positiveWireParityChecks': checks,
            'positiveNativeProposalsForCompatibility': 0}


def motion_check(decoder, cycle, fraction, action):
    p = decoder.positions[cycle]; d = action[:len(decoder.positions)][cycle]
    q = p + fraction*(np.roll(p, -1, axis=0)-p)
    center = p.mean(0); variance = float(np.sum((p-center)**2))
    expected = (1-3*fraction+3*fraction*fraction)*variance
    raw = float(np.sum((q-center)**2))
    assert abs(raw-expected) <= 2e-11*max(1., expected)
    assert raw > variance and np.max(abs((p+d).mean(0)-center)) <= .005+1e-8
    assert np.max(abs(p+d-q)) <= .005+1e-8
    return abs(raw-expected)/max(1., expected)


def run(args):
    assert 1 <= args.budget <= 1024 and 0 < args.seconds <= 20
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    model, metadata = load(args.checkpoint)
    assert metadata['kind'] == 'positive-synthetic-graph-cycle-critic-v1'
    inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert inputs == metadata['sourceInputsByView'][args.view]
    decoder = WalkDecoder(args.directory)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    attempts = accepted = 0; best = None; reasons = Counter(); best_visual = native.initial['visual']
    maximum_error = 0.; fraction_counts = Counter(); ties = 0
    try:
        zero = native.request(wire(decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)), 'MEASURE'))
        assert zero['legal'] and zero['hard'] == zero['individualHard'] == zero['spacing'] == 0
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        cycles, support = vocabulary(decoder, nodes, args.support, args.seed, args.triples)
        parity = compatibility(decoder, nodes, cycles, args.view == 'overview')
        scores, feature_hashes, error = rank(model, decoder, nodes, cycles, args.view == 'overview')
        order, noise, policy = sample_order(scores, args.seed+1000, 1.)
        np.savez_compressed(args.out/'observations.npz', node_features=nodes, cycles=cycles,
                            fractions=FRACTIONS, scores=scores, gumbel=noise)
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:args.budget]:
                if time.monotonic()-started >= args.seconds:
                    break
                ci, fi = map(int, np.unravel_index(index, scores.shape)); cycle = cycles[ci]; fraction = float(FRACTIONS[fi])
                action = signed_action(decoder, cycle, fraction)
                maximum_error = max(maximum_error, motion_check(decoder, cycle, fraction, action))
                command = wire(action); result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'fractionIndex': fi,
                    'cycle': cycle.tolist(), 'fraction': fraction, 'score': float(scores[ci, fi]),
                    'gumbel': float(noise[ci, fi]), 'wireSha256': hashlib.sha256(command.encode()).hexdigest(),
                    'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1; fraction_counts[str(fraction)] += 1
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
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'frozen-positive-critic-signed-cycle-transfer-v1', 'featureSchema': SCHEMA,
        'view': args.view, 'sourceDirectory': str(args.directory), 'sourceInputs': inputs,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'seed': args.seed, 'supportKind': args.support, 'triples': args.triples, 'support': support,
        'candidateFractions': FRACTIONS.tolist(), 'policy': policy, 'budget': args.budget, 'secondsLimit': args.seconds,
        'attempts': attempts, 'accepted': accepted, 'legalTies': ties, 'reasons': dict(reasons),
        'proposedFractionCounts': dict(fraction_counts), 'initialVisual': native.initial['visual'],
        'initialIndividualVisual': native.initial['individualVisual'], 'final': stats,
        'featureChunkHashes': feature_hashes, 'scaleFeatureMaxError': error, 'compatibility': parity,
        'negativeRawVarianceMaxRelativeError': maximum_error, 'negativeQuantizationBoundsChecked': attempts,
        'wallSeconds': time.monotonic()-started, 'trainedOnNegativeFractions': False,
        'newTrainingUpdatesDuringInference': 0, 'coordinateRepairs': 0, 'nativeSearchCalls': 0,
        'policyOrderFixedBeforeNativeMeasurement': True, 'savedGeometryMatchesModelOutput': True,
        'allHistoryWireExclusionClaimed': False, 'observationsSha256': digest(args.out/'observations.npz'),
        'actionsSha256': digest(args.out/'actions.jsonl'), 'codeSha256': hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'supportKind', 'attempts', 'accepted',
        'legalTies', 'initialVisual', 'reasons', 'wallSeconds']} | {'finalVisual': best_visual}), flush=True)


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert report['codeSha256'] == hashes() and digest(report['checkpoint']) == report['checkpointSha256']
    assert digest(report['environment']) == report['environmentSha256']
    assert {name: digest(directory/name) for name in INPUT_FILES} == report['sourceInputs']
    assert digest(args.out/'observations.npz') == report['observationsSha256']
    assert digest(args.out/'actions.jsonl') == report['actionsSha256']
    model, metadata = load(Path(report['checkpoint'])); assert metadata == report['checkpointTraining']
    decoder = WalkDecoder(directory)
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        nodes = saved['node_features'].copy()
        cycles, support = vocabulary(decoder, nodes, report['supportKind'], report['seed'], report['triples'])
        assert support == report['support'] and np.array_equal(cycles, saved['cycles'])
        assert np.array_equal(FRACTIONS, saved['fractions'])
        assert compatibility(decoder, nodes, cycles, report['view'] == 'overview') == report['compatibility']
        scores, feature_hashes, error = rank(model, decoder, nodes, cycles, report['view'] == 'overview')
        assert feature_hashes == report['featureChunkHashes'] and np.array_equal(scores, saved['scores'])
        assert error == report['scaleFeatureMaxError']
        order, noise, policy = sample_order(scores, report['seed']+1000, 1.)
        assert policy == report['policy'] and np.array_equal(noise, saved['gumbel'])
    attempts = accepted = ties = 0; best = None; reasons = Counter(); maximum_error = 0.; fractions = Counter()
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); ci, fi = map(int, np.unravel_index(order[attempts], scores.shape))
        assert row['rank'] == attempts and (row['cycleIndex'], row['fractionIndex']) == (ci, fi)
        assert row['cycle'] == cycles[ci].tolist() and row['fraction'] == FRACTIONS[fi]
        assert row['score'] == scores[ci, fi] and row['gumbel'] == noise[ci, fi]
        proposed = signed_action(decoder, cycles[ci], float(FRACTIONS[fi]))
        assert hashlib.sha256(wire(proposed).encode()).hexdigest() == row['wireSha256']
        maximum_error = max(maximum_error, motion_check(decoder, cycles[ci], float(FRACTIONS[fi]), proposed))
        attempts += 1; reasons[row['result']['reason']] += 1; fractions[str(row['fraction'])] += 1
        ties += int(row['result']['legal'] and row['result']['visual'] == report['initialVisual'])
        if row['result']['accepted']:
            best = proposed; accepted += 1
    assert (attempts, accepted, ties) == (report['attempts'], report['accepted'], report['legalTies'])
    assert dict(reasons) == report['reasons'] and dict(fractions) == report['proposedFractionCounts']
    assert maximum_error == report['negativeRawVarianceMaxRelativeError']
    if best is not None:
        assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'actionsReplayed': attempts, 'accepted': accepted,
        'allScoresAndGumbelOrderReconstructed': True, 'positiveFeaturesAndWiresCompatible': True,
        'negativeVarianceAndRoundingBoundsVerified': True, 'savedGeometryReconstructed': True,
        'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['run', 'replay']); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--directory', type=Path); p.add_argument('--checkpoint', type=Path); p.add_argument('--environment', type=Path)
    p.add_argument('--view', choices=['individual', 'overview']); p.add_argument('--seed', type=int, default=87217)
    p.add_argument('--support', choices=['local', 'global'], default='local'); p.add_argument('--triples', type=int, default=4096)
    p.add_argument('--budget', type=int, default=1024); p.add_argument('--seconds', type=float, default=20)
    args = p.parse_args(); (run if args.mode == 'run' else replay)(args)
