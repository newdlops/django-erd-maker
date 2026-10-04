#!/usr/bin/env python3
"""Frozen neural factor scores over uniform same-size three-card cycles.

The pair critic supplies an additive proxy, not a trained cycle reward model.
Both cycle directions share the same pair-factor score and are tested in stable
order. Native geometry only decodes, measures and accepts strict improvements.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from full_context_pair_model import load, SCHEMA, expand
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, pair_features, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved, array_hash


def cycles(decoder, features, seed, count):
    assert 1 <= count <= 4096
    groups = [group for group in decoder.groups if len(group) >= 3]
    sizes = np.array([len(group)*(len(group)-1)*(len(group)-2)//6 for group in groups], dtype=np.int64)
    cumulative = np.cumsum(sizes); total = int(cumulative[-1]); assert total >= count
    active = np.any(features[:, 4:6] > 0, axis=1); rng = np.random.default_rng(seed)
    seen = set(); rows = []; draws = rejected = duplicates = 0
    while len(rows) < count and draws < count*40:
        index = int(rng.integers(total)); group = groups[int(np.searchsorted(cumulative, index, side='right'))]
        triple = tuple(sorted(map(int, rng.choice(group, 3, replace=False)))); draws += 1
        if not active[list(triple)].any() or decoder.irrelevant_isolate[list(triple)].all():
            rejected += 1; continue
        if triple in seen:
            duplicates += 1; continue
        seen.add(triple); rows.append(triple)
    assert len(rows) == count
    triples = np.array(rows, dtype=np.int32)
    vocabulary = np.empty((count*2, 3), dtype=np.int32)
    vocabulary[::2] = triples; vocabulary[1::2] = triples[:, [0, 2, 1]]
    assert active[vocabulary].any(1).all() and len({tuple(row) for row in vocabulary.tolist()}) == len(vocabulary)
    return vocabulary, {'allEqualSizeUnorderedTriples': total, 'sampledUnorderedTriples': count,
        'bothCycleDirections': True, 'cycles': len(vocabulary), 'draws': draws,
        'rejectedNonconflictingOrAllIsolated': rejected, 'duplicateTripleDraws': duplicates,
        'support': 'uniform same-size triples conditioned on a direct physical-view conflict; exclude three isolated single cards',
        'coversIndirectPoolingOnlyConflicts': False}


def cycle_action(decoder, cycle):
    a, b, c = map(int, cycle); assert len({a, b, c}) == 3
    permutation = np.arange(len(decoder.positions), dtype=np.int32)
    permutation[[a, b, c]] = [b, c, a]
    action = decoder.decode(permutation)
    assert np.count_nonzero(np.any(action[:len(permutation)], axis=1)) == 3
    return action


def rank_cycles(model, decoder, features, vocabulary, overview, chunk=512):
    triples = vocabulary[::2]
    assert np.array_equal(vocabulary[1::2], triples[:, [0, 2, 1]])
    proxy = np.empty(len(triples)); hashes = []; max_error = 0
    for offset in range(0, len(triples), chunk):
        batch = triples[offset:offset+chunk]
        factors = np.stack([batch[:, [0, 1]], batch[:, [0, 2]], batch[:, [1, 2]]], axis=1).reshape(-1, 2)
        base, error = pair_features(features, decoder, factors, overview)
        full = expand(base, features, factors)
        predictions = model.forward(full)[0].reshape(len(batch), 3)
        proxy[offset:offset+len(batch)] = predictions.mean(1)
        hashes.append(array_hash(full)); max_error = max(max_error, error)
    scores = np.repeat(proxy, 2)
    assert np.isfinite(scores).all() and np.array_equal(scores[::2], scores[1::2])
    return scores, hashes, max_error


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_neural_cycle_policy.py',
        'full_context_pair_model.py', 'run_anchor_pair_policy.py', 'run_anchor_pair_walk.py',
        'learn_pair_policy.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def run(args):
    assert 1 <= args.budget <= 2048 and 0 < args.seconds <= 20
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    decoder = WalkDecoder(args.directory); model, metadata = load(args.checkpoint)
    inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert inputs == metadata['sourceInputsByView'][args.view]
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    attempts = accepted = 0; best = None; best_visual = native.initial['visual']; reasons = Counter()
    try:
        features = np.array([row['features'] for row in native.initial['nodes']])
        vocabulary, support = cycles(decoder, features, args.seed, args.triples)
        ranking_started = time.monotonic()
        scores, hashes, error = rank_cycles(model, decoder, features, vocabulary, args.view == 'overview')
        ranking_seconds = time.monotonic()-ranking_started; order = np.argsort(-scores, kind='stable')
        np.savez_compressed(args.out/'observations.npz', node_features=features, cycles=vocabulary, scores=scores)
        action_started = time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:args.budget]:
                if time.monotonic()-started >= args.seconds:
                    break
                cycle = vocabulary[index]; action = cycle_action(decoder, cycle); command = wire(action)
                result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'cycle': cycle.tolist(), 'score': float(scores[index]),
                    'wireSha256': hashlib.sha256(command.encode()).hexdigest(), 'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1
                if result['accepted']:
                    accepted += 1; best = action.copy(); best_visual = result['visual']
        action_seconds = time.monotonic()-action_started
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['policyActionsEvaluated'], stats['acceptedActions'], stats['visual']) == (attempts, accepted, best_visual)
    verify_saved(args.directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'frozen-neural-factor-three-cycle-policy-v1', 'schema': SCHEMA, 'view': args.view,
        'sourceDirectory': str(args.directory), 'sourceInputs': inputs, 'support': support,
        'seed': args.seed, 'triples': args.triples, 'budget': args.budget, 'secondsLimit': args.seconds,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'factorScore': 'mean of three frozen neural single-pair outcome scores; same proxy for both cycle directions',
        'criticTrainedOnCycleOutcomes': False, 'newTrainingUpdatesDuringInference': 0,
        'rankingSeconds': ranking_seconds, 'actionSeconds': action_seconds, 'wallSeconds': time.monotonic()-started,
        'attempts': attempts, 'accepted': accepted, 'reasons': dict(reasons),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'], 'final': stats,
        'featureChunkSize': 512, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
        'allSubmittedCyclesTouchDirectConflicts': True, 'cardsMovedPerProposal': 3,
        'modelNamesAsFeatures': False, 'absoluteCoordinatesAsFeatures': False,
        'coordinateRepairs': 0, 'heuristicSearchCalls': 0, 'savedGeometryMatchesModelOutput': True,
        'observationsSha256': digest(args.out/'observations.npz'), 'actionsSha256': digest(args.out/'actions.jsonl'),
        'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'support', 'attempts', 'accepted', 'initialVisual',
        'reasons', 'rankingSeconds', 'actionSeconds', 'wallSeconds']} | {'finalVisual': stats['visual']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert digest(report['checkpoint']) == report['checkpointSha256'] and code_hashes() == report['codeSha256']
    assert {name: digest(directory/name) for name in INPUT_FILES} == report['sourceInputs']
    assert digest(args.out/'observations.npz') == report['observationsSha256']
    assert digest(args.out/'actions.jsonl') == report['actionsSha256']
    decoder = WalkDecoder(directory); model, metadata = load(Path(report['checkpoint']))
    assert metadata == report['checkpointTraining']
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        features = saved['node_features'].copy()
        vocabulary, support = cycles(decoder, features, report['seed'], report['triples'])
        assert support == report['support'] and np.array_equal(vocabulary, saved['cycles'])
        scores, hashes, _ = rank_cycles(model, decoder, features, vocabulary, report['view'] == 'overview')
        assert np.array_equal(scores, saved['scores']) and hashes == report['featureChunkHashes']
    order = np.argsort(-scores, kind='stable'); attempts = accepted = 0; best = None
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); index = order[attempts]; cycle = vocabulary[index]
        assert row['rank'] == attempts and row['cycle'] == cycle.tolist() and row['score'] == scores[index]
        action = cycle_action(decoder, cycle)
        assert hashlib.sha256(wire(action).encode()).hexdigest() == row['wireSha256']
        if row['result']['accepted']:
            best = action; accepted += 1
        attempts += 1
    assert (attempts, accepted) == (report['attempts'], report['accepted'])
    if best is not None:
        assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'cycleNeuralScoresReplayed': len(vocabulary), 'modelActionsReplayed': attempts,
        'accepted': accepted, 'savedGeometryMatchesModelOutput': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['run', 'replay'])
    p.add_argument('--out', type=Path, required=True); p.add_argument('--directory', type=Path)
    p.add_argument('--environment', type=Path); p.add_argument('--checkpoint', type=Path)
    p.add_argument('--view', choices=['individual', 'overview']); p.add_argument('--seed', type=int, default=84021)
    p.add_argument('--triples', type=int, default=4096); p.add_argument('--budget', type=int, default=1024)
    p.add_argument('--seconds', type=float, default=20); args = p.parse_args(); (run if args.mode == 'run' else replay)(args)
