#!/usr/bin/env python3
"""Rank unmeasured conflict-touching pairs with the adapted frozen critic."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from full_context_pair_model import load, SCHEMA
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_full_context_pairs import rank, vocabulary


def remaining_pairs(decoder, features, metadata, view):
    path = Path(metadata['datasetPath'])
    assert digest(path) == metadata['datasetSha256']
    with np.load(path, allow_pickle=False) as measured:
        pairs = measured['pairs'][measured['view'] == view]
        assert (pairs[:, 0] < pairs[:, 1]).all()
        seen = {tuple(map(int, pair)) for pair in pairs}
    candidates, support = vocabulary(decoder, features)
    assert (candidates[:, 0] < candidates[:, 1]).all()
    keep = np.array([tuple(map(int, pair)) not in seen for pair in candidates])
    result = candidates[keep]
    assert len(result) > 0 and len(seen) == len(pairs)
    assert all(tuple(map(int, pair)) not in seen for pair in result)
    return result, support | {'measuredPairsInView': len(seen),
        'excludedMeasuredEligiblePairs': int(np.sum(~keep)), 'unmeasuredEligiblePairs': len(result),
        'repeatedMeasuredActions': 0}


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_active_pair_policy.py',
        'run_full_context_pairs.py', 'full_context_pair_model.py', 'run_anchor_pair_policy.py',
        'run_anchor_pair_walk.py', 'learn_pair_policy.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


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
        candidates, support = remaining_pairs(decoder, features, metadata, args.view)
        ranking_started = time.monotonic()
        scores, hashes, error = rank(model, features, decoder, candidates, args.view == 'overview')
        ranking_seconds = time.monotonic()-ranking_started
        order = np.argsort(-scores, kind='stable')
        np.savez_compressed(args.out/'observations.npz', node_features=features, pairs=candidates, scores=scores)
        action_started = time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:args.budget]:
                if time.monotonic()-started >= args.seconds:
                    break
                a, b = map(int, candidates[index]); action = decoder.action(a, b); command = wire(action)
                result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'source': a, 'target': b, 'score': float(scores[index]),
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
    report = {'kind': 'active-context-pressure-pair-policy-v1', 'schema': SCHEMA, 'view': args.view,
        'sourceDirectory': str(args.directory), 'sourceInputs': inputs,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'support': support, 'budget': args.budget, 'secondsLimit': args.seconds,
        'rankingSeconds': ranking_seconds, 'actionSeconds': action_seconds, 'wallSeconds': time.monotonic()-started,
        'attempts': attempts, 'accepted': accepted, 'reasons': dict(reasons),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'], 'final': stats,
        'featureChunkSize': 2048, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
        'newTrainingUpdatesDuringInference': 0, 'modelNamesAsFeatures': False, 'absoluteCoordinatesAsFeatures': False,
        'coordinateRepairs': 0, 'heuristicSearchCalls': 0, 'allSubmittedPairsTouchDirectConflicts': True,
        'allSubmittedPairsPreviouslyUnmeasured': True, 'neuralRankedAllUnmeasuredEligiblePairs': True,
        'savedGeometryMatchesModelOutput': True, 'causalAblationVerified': False,
        'observationsSha256': digest(args.out/'observations.npz'), 'actionsSha256': digest(args.out/'actions.jsonl'),
        'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'support', 'attempts', 'accepted', 'initialVisual',
        'reasons', 'rankingSeconds', 'actionSeconds', 'wallSeconds']} | {'finalVisual': stats['visual']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert report['schema'] == SCHEMA and digest(report['checkpoint']) == report['checkpointSha256']
    assert code_hashes() == report['codeSha256']
    assert {name: digest(directory/name) for name in INPUT_FILES} == report['sourceInputs']
    for name, key in [('observations.npz', 'observationsSha256'), ('actions.jsonl', 'actionsSha256')]:
        assert digest(args.out/name) == report[key]
    decoder = WalkDecoder(directory); model, metadata = load(Path(report['checkpoint']))
    assert metadata == report['checkpointTraining']
    assert report['sourceInputs'] == metadata['sourceInputsByView'][report['view']]
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        features = saved['node_features'].copy()
        candidates, support = remaining_pairs(decoder, features, metadata, report['view'])
        assert support == report['support'] and np.array_equal(candidates, saved['pairs'])
        scores, hashes, _ = rank(model, features, decoder, candidates, report['view'] == 'overview')
        assert np.array_equal(scores, saved['scores']) and hashes == report['featureChunkHashes']
    order = np.argsort(-scores, kind='stable'); attempts = accepted = 0; best = None
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); index = order[attempts]; a, b = map(int, candidates[index])
        assert row['rank'] == attempts and (a, b) == (row['source'], row['target'])
        assert row['score'] == scores[index]
        action = decoder.action(a, b)
        assert hashlib.sha256(wire(action).encode()).hexdigest() == row['wireSha256']
        if row['result']['accepted']:
            best = action; accepted += 1
        attempts += 1
    assert (attempts, accepted) == (report['attempts'], report['accepted'])
    if best is not None:
        assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'allUnmeasuredNeuralScoresReplayed': len(candidates),
        'modelRankedActionsReplayed': attempts, 'accepted': accepted, 'repeatedMeasuredActions': 0,
        'savedGeometryMatchesModelOutput': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['run', 'replay'])
    p.add_argument('--out', type=Path, required=True); p.add_argument('--directory', type=Path)
    p.add_argument('--checkpoint', type=Path); p.add_argument('--environment', type=Path)
    p.add_argument('--view', choices=['individual', 'overview']); p.add_argument('--budget', type=int, default=1024)
    p.add_argument('--seconds', type=float, default=20); args = p.parse_args(); (run if args.mode == 'run' else replay)(args)
