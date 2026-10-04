#!/usr/bin/env python3
"""Frozen action-conditioned neural sampling over unmeasured directed cycles."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from fractional_cycle_model import load, SCHEMA, expand_actions
from directed_cycle_model import cycle_features
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved, array_hash
from run_directed_cycle_policy import coordinated_cycles, measured
from run_fractional_cycle_transfer import fractional_action

FRACTIONS = np.array([.001, .005, .02, .1, .4, 1.])


def rank(model, decoder, nodes, cycles, overview, chunk=256):
    scores = np.empty((len(cycles), len(FRACTIONS))); hashes = []; maximum = 0
    for offset in range(0, len(cycles), chunk):
        selected = cycles[offset:offset+chunk]
        core, error = cycle_features(nodes, decoder, selected, overview); maximum = max(maximum, error)
        chunk_hashes = []
        for column, fraction in enumerate(FRACTIONS):
            features = expand_actions(core, nodes, decoder, selected, np.full(len(selected), fraction))
            scores[offset:offset+len(selected), column] = model.forward(features)[0]
            chunk_hashes.append(array_hash(features))
        hashes.append(chunk_hashes)
    assert np.isfinite(scores).all()
    return scores, hashes, maximum


def sample_order(scores, seed, temperature):
    assert 0 < temperature <= 8 and scores.ndim == 2 and np.isfinite(scores).all()
    noise = np.random.default_rng(seed).gumbel(size=scores.shape)
    logits = scores/temperature
    order = np.argsort(-(logits+noise).ravel(), kind='stable')
    weights = np.exp(logits-logits.max()); probabilities = weights/weights.sum()
    nonzero = probabilities[probabilities > 0]
    return order, noise, {'sampling': 'Gumbel top-k without replacement from frozen neural Boltzmann logits',
        'temperature': temperature, 'gumbelSeed': seed,
        'fractionProbabilityMass': probabilities.sum(0).tolist(),
        'entropy': float(-np.sum(nonzero*np.log(nonzero))),
        'effectiveCandidateCount': float(1/np.sum(probabilities*probabilities))}


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_fraction_conditioned_policy.py',
        'fractional_cycle_model.py', 'directed_cycle_model.py', 'run_directed_cycle_policy.py',
        'run_fractional_cycle_transfer.py', 'run_anchor_pair_policy.py', 'run_anchor_pair_walk.py',
        'full_context_pair_model.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def run(args):
    assert 1 <= args.budget <= 2048 and 0 < args.seconds <= 20 and 0 < args.temperature <= 8
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    decoder = WalkDecoder(args.directory); model, metadata = load(args.checkpoint)
    inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert inputs == metadata['sourceInputsByView'][args.view]
    measured_cycles = measured(metadata, args.view)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    attempts = accepted = 0; best = None; best_visual = native.initial['visual']; reasons = Counter()
    try:
        nodes = np.array([row['features'] for row in native.initial['nodes']]); sample_started = time.monotonic()
        cycles, support = coordinated_cycles(decoder, nodes, measured_cycles, args.vocabulary_seed, args.triples)
        sampling_seconds = time.monotonic()-sample_started; rank_started = time.monotonic()
        scores, hashes, error = rank(model, decoder, nodes, cycles, args.view == 'overview')
        ranking_seconds = time.monotonic()-rank_started
        order, noise, policy = sample_order(scores, args.sampling_seed, args.temperature)
        np.savez_compressed(args.out/'observations.npz', node_features=nodes, cycles=cycles,
            fractions=FRACTIONS, scores=scores, gumbel=noise)
        action_started = time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:args.budget]:
                if time.monotonic()-started >= args.seconds:
                    break
                ci, fi = map(int, np.unravel_index(index, scores.shape)); fraction = float(FRACTIONS[fi])
                cycle = cycles[ci]; action = fractional_action(decoder, cycle, fraction); command = wire(action)
                result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'fractionIndex': fi,
                    'cycle': cycle.tolist(), 'fraction': fraction, 'score': float(scores[ci, fi]),
                    'gumbel': float(noise[ci, fi]), 'wireSha256': hashlib.sha256(command.encode()).hexdigest(),
                    'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1
                if result['accepted']:
                    accepted += 1; best = action.copy(); best_visual = result['visual']
        action_seconds = time.monotonic()-action_started; assert native.request('SAVE')['saved']
    finally:
        native.close()
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['policyActionsEvaluated'], stats['acceptedActions'], stats['visual']) == (attempts, accepted, best_visual)
    verify_saved(args.directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'trained-fraction-conditioned-stochastic-cycle-policy-v1', 'schema': SCHEMA, 'view': args.view,
        'sourceDirectory': str(args.directory), 'sourceInputs': inputs, 'support': support,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'vocabularySeed': args.vocabulary_seed, 'samplingSeed': args.sampling_seed,
        'triples': args.triples, 'candidateFractions': FRACTIONS.tolist(), 'unmeasuredFractionValues': [.4],
        'policy': policy, 'budget': args.budget, 'secondsLimit': args.seconds,
        'samplingSeconds': sampling_seconds, 'rankingSeconds': ranking_seconds, 'actionSeconds': action_seconds,
        'wallSeconds': time.monotonic()-started, 'attempts': attempts, 'accepted': accepted, 'reasons': dict(reasons),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'], 'final': stats,
        'featureChunkSize': 256, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
        'newTrainingUpdatesDuringInference': 0, 'modelNamesAsFeatures': False, 'absoluteCoordinatesAsFeatures': False,
        'coordinateRepairs': 0, 'heuristicSearchCalls': 0, 'allWordsAbsentFromCompleteMeasuredDataset': True,
        'policyOrderFixedBeforeNativeMeasurement': True, 'savedGeometryMatchesModelOutput': True,
        'causalSuperiorityVerified': False, 'observationsSha256': digest(args.out/'observations.npz'),
        'actionsSha256': digest(args.out/'actions.jsonl'), 'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'attempts', 'accepted', 'reasons',
        'samplingSeconds', 'rankingSeconds', 'actionSeconds', 'wallSeconds']} | {'temperature': args.temperature, 'finalVisual': stats['visual']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert report['schema'] == SCHEMA and digest(report['checkpoint']) == report['checkpointSha256']
    assert digest(report['environment']) == report['environmentSha256'] and code_hashes() == report['codeSha256']
    assert {name: digest(directory/name) for name in INPUT_FILES} == report['sourceInputs']
    assert digest(args.out/'observations.npz') == report['observationsSha256']
    assert digest(args.out/'actions.jsonl') == report['actionsSha256']
    decoder = WalkDecoder(directory); model, metadata = load(Path(report['checkpoint']))
    assert metadata == report['checkpointTraining']
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        nodes = saved['node_features'].copy()
        cycles, support = coordinated_cycles(decoder, nodes, measured(metadata, report['view']), report['vocabularySeed'], report['triples'])
        assert support == report['support'] and np.array_equal(cycles, saved['cycles'])
        assert np.array_equal(FRACTIONS, saved['fractions'])
        scores, hashes, _ = rank(model, decoder, nodes, cycles, report['view'] == 'overview')
        assert np.array_equal(scores, saved['scores']) and hashes == report['featureChunkHashes']
        order, noise, policy = sample_order(scores, report['samplingSeed'], report['policy']['temperature'])
        assert np.array_equal(noise, saved['gumbel']) and policy == report['policy']
    attempts = accepted = 0; best = None
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); ci, fi = map(int, np.unravel_index(order[attempts], scores.shape))
        assert row['rank'] == attempts and (row['cycleIndex'], row['fractionIndex']) == (ci, fi)
        assert row['cycle'] == cycles[ci].tolist() and row['fraction'] == FRACTIONS[fi]
        assert row['score'] == scores[ci, fi] and row['gumbel'] == noise[ci, fi]
        action = fractional_action(decoder, cycles[ci], FRACTIONS[fi])
        assert hashlib.sha256(wire(action).encode()).hexdigest() == row['wireSha256']
        if row['result']['accepted']:
            best = action; accepted += 1
        attempts += 1
    assert (attempts, accepted) == (report['attempts'], report['accepted'])
    if best is not None:
        assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'allNeuralActionScoresReplayed': scores.size,
        'gumbelPolicyOrderReproduced': True, 'modelActionsReplayed': attempts, 'accepted': accepted,
        'measuredTriplesExcludedIncludingAllFractionsAndDirections': True,
        'savedGeometryMatchesModelOutput': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('command', choices=['run', 'replay'])
    parser.add_argument('--out', type=Path, required=True); parser.add_argument('--directory', type=Path)
    parser.add_argument('--environment', type=Path); parser.add_argument('--checkpoint', type=Path)
    parser.add_argument('--view', choices=['individual', 'overview']); parser.add_argument('--triples', type=int, default=4096)
    parser.add_argument('--budget', type=int, default=1024); parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--vocabulary-seed', type=int, default=84053); parser.add_argument('--sampling-seed', type=int, default=84059)
    parser.add_argument('--temperature', type=float, default=1.)
    args = parser.parse_args(); (run if args.command == 'run' else replay)(args)
