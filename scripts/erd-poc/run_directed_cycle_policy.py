#!/usr/bin/env python3
"""Model-ranked unmeasured cycles that move at least two conflicting owners."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import load, cycle_features, SCHEMA
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved, array_hash
from run_neural_cycle_policy import cycle_action


def coordinated_cycles(decoder, nodes, measured_cycles, seed, count):
    assert 1 <= count <= 4096
    active = np.any(nodes[:, 4:6] > 0, axis=1)
    choose2 = lambda n: n*(n-1)//2
    choose3 = lambda n: n*(n-1)*(n-2)//6
    groups = []; eligible_counts = []
    for group in decoder.groups:
        if len(group) < 3:
            continue
        pressured = group[active[group]]; other = group[~active[group]]
        two = choose2(len(pressured))*len(other); three = choose3(len(pressured))
        isolated = decoder.irrelevant_isolate[group]
        isolated_active = int(np.sum(isolated & active[group])); isolated_other = int(np.sum(isolated & ~active[group]))
        excluded = choose2(isolated_active)*isolated_other+choose3(isolated_active)
        eligible = two+three-excluded
        if eligible:
            groups.append((pressured, other, two, three)); eligible_counts.append(eligible)
    cumulative = np.cumsum(np.array(eligible_counts, dtype=np.int64)); total = int(cumulative[-1])
    seen = {tuple(sorted(map(int, cycle))) for cycle in measured_cycles}
    assert all(0 <= node < len(nodes) for triple in seen for node in triple)
    eligible_seen = {triple for triple in seen if active[list(triple)].sum() >= 2 and
                     not decoder.irrelevant_isolate[list(triple)].all()}
    assert all(np.array_equal(decoder.sizes[triple[0]], decoder.sizes[node]) for triple in seen for node in triple)
    remaining = total-len(eligible_seen); requested = min(count, remaining); assert requested > 0
    rng = np.random.default_rng(seed); chosen = set(); triples = []; draws = isolated_draws = excluded_seen = duplicate_draws = 0
    while len(triples) < requested and draws < requested*100:
        group_id = int(np.searchsorted(cumulative, int(rng.integers(total)), side='right'))
        pressured, other, two, three = groups[group_id]
        # Group weights exclude all-isolated triples; resample within this group
        # so each eligible global triple retains the same probability.
        for _ in range(10000):
            if int(rng.integers(two+three)) < two:
                triple = tuple(sorted([*map(int, rng.choice(pressured, 2, replace=False)), int(rng.choice(other))]))
            else:
                triple = tuple(sorted(map(int, rng.choice(pressured, 3, replace=False))))
            if not decoder.irrelevant_isolate[list(triple)].all():
                break
            isolated_draws += 1
        else:
            raise RuntimeError('bounded within-group cycle sampling failed')
        draws += 1
        if triple in eligible_seen:
            excluded_seen += 1; continue
        if triple in chosen:
            duplicate_draws += 1; continue
        chosen.add(triple); triples.append(triple)
    assert len(triples) == requested
    triples = np.array(triples, dtype=np.int32); vocabulary = np.empty((2*requested, 3), dtype=np.int32)
    vocabulary[::2] = triples; vocabulary[1::2] = triples[:, [0, 2, 1]]
    assert (active[vocabulary].sum(1) >= 2).all()
    assert not decoder.irrelevant_isolate[vocabulary].all(1).any()
    assert all(tuple(sorted(map(int, cycle))) not in seen for cycle in vocabulary)
    return vocabulary, {'eligibleUnorderedTriplesBeforeExclusion': total, 'excludedMeasuredEligibleTriples': len(eligible_seen),
        'remainingUnmeasuredEligibleTriples': remaining, 'sampledUnorderedTriples': requested,
        'cycles': len(vocabulary), 'draws': draws, 'rejectedAllIsolatedDraws': isolated_draws,
        'rejectedMeasuredDraws': excluded_seen, 'duplicateDraws': duplicate_draws,
        'minDirectlyConflictingOwners': 2, 'bothCycleDirections': True, 'previouslyMeasuredCyclesSubmitted': 0,
        'uniformConditionalTripleSampling': True, 'coversIndirectPoolingOnlyConflicts': False}


def measured(metadata, view):
    path = Path(metadata['datasetPath']); assert digest(path) == metadata['datasetSha256']
    with np.load(path, allow_pickle=False) as saved:
        return saved['cycles'][saved['view'] == view].copy()


def rank(model, decoder, nodes, vocabulary, overview, chunk=256):
    scores = np.empty(len(vocabulary)); hashes = []; maximum = 0
    for offset in range(0, len(vocabulary), chunk):
        selected = vocabulary[offset:offset+chunk]
        features, error = cycle_features(nodes, decoder, selected, overview)
        scores[offset:offset+len(selected)] = model.forward(features)[0]
        hashes.append(array_hash(features)); maximum = max(maximum, error)
    assert np.isfinite(scores).all()
    return scores, hashes, maximum


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_directed_cycle_policy.py',
        'directed_cycle_model.py', 'run_neural_cycle_policy.py', 'run_anchor_pair_policy.py',
        'run_anchor_pair_walk.py', 'full_context_pair_model.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def run(args):
    assert 1 <= args.budget <= 2048 and 0 < args.seconds <= 20
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    decoder = WalkDecoder(args.directory); model, metadata = load(args.checkpoint)
    inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert inputs == metadata['sourceInputsByView'][args.view]
    measured_cycles = measured(metadata, args.view)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    attempts = accepted = 0; best = None; best_visual = native.initial['visual']; reasons = Counter()
    try:
        nodes = np.array([row['features'] for row in native.initial['nodes']]); sample_started = time.monotonic()
        vocabulary, support = coordinated_cycles(decoder, nodes, measured_cycles, args.seed, args.triples)
        sampling_seconds = time.monotonic()-sample_started; rank_started = time.monotonic()
        scores, hashes, error = rank(model, decoder, nodes, vocabulary, args.view == 'overview')
        ranking_seconds = time.monotonic()-rank_started; order = np.argsort(-scores, kind='stable')
        np.savez_compressed(args.out/'observations.npz', node_features=nodes, cycles=vocabulary, scores=scores)
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
        action_seconds = time.monotonic()-action_started; assert native.request('SAVE')['saved']
    finally:
        native.close()
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['policyActionsEvaluated'], stats['acceptedActions'], stats['visual']) == (attempts, accepted, best_visual)
    verify_saved(args.directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'trained-directed-coordinated-cycle-policy-v1', 'schema': SCHEMA, 'view': args.view,
        'sourceDirectory': str(args.directory), 'sourceInputs': inputs, 'support': support,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'seed': args.seed, 'triples': args.triples, 'budget': args.budget, 'secondsLimit': args.seconds,
        'samplingSeconds': sampling_seconds, 'rankingSeconds': ranking_seconds, 'actionSeconds': action_seconds,
        'wallSeconds': time.monotonic()-started, 'attempts': attempts, 'accepted': accepted, 'reasons': dict(reasons),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'], 'final': stats,
        'featureChunkSize': 256, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
        'newTrainingUpdatesDuringInference': 0, 'modelNamesAsFeatures': False, 'absoluteCoordinatesAsFeatures': False,
        'coordinateRepairs': 0, 'heuristicSearchCalls': 0, 'savedGeometryMatchesModelOutput': True,
        'observationsSha256': digest(args.out/'observations.npz'), 'actionsSha256': digest(args.out/'actions.jsonl'),
        'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'support', 'attempts', 'accepted', 'initialVisual',
        'reasons', 'samplingSeconds', 'rankingSeconds', 'actionSeconds', 'wallSeconds']} | {'finalVisual': stats['visual']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert report['schema'] == SCHEMA and digest(report['checkpoint']) == report['checkpointSha256']
    assert code_hashes() == report['codeSha256']
    assert {name: digest(directory/name) for name in INPUT_FILES} == report['sourceInputs']
    assert digest(args.out/'observations.npz') == report['observationsSha256']
    assert digest(args.out/'actions.jsonl') == report['actionsSha256']
    decoder = WalkDecoder(directory); model, metadata = load(Path(report['checkpoint']))
    assert metadata == report['checkpointTraining']
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        nodes = saved['node_features'].copy()
        vocabulary, support = coordinated_cycles(decoder, nodes, measured(metadata, report['view']), report['seed'], report['triples'])
        assert support == report['support'] and np.array_equal(vocabulary, saved['cycles'])
        scores, hashes, _ = rank(model, decoder, nodes, vocabulary, report['view'] == 'overview')
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
    result = {'status': 'pass', 'allDirectedCycleScoresReplayed': len(vocabulary), 'modelActionsReplayed': attempts,
        'accepted': accepted, 'previouslyMeasuredCyclesSubmitted': 0,
        'allProposalsTouchAtLeastTwoDirectConflicts': True, 'savedGeometryMatchesModelOutput': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['run', 'replay'])
    p.add_argument('--out', type=Path, required=True); p.add_argument('--directory', type=Path)
    p.add_argument('--environment', type=Path); p.add_argument('--checkpoint', type=Path)
    p.add_argument('--view', choices=['individual', 'overview']); p.add_argument('--seed', type=int, default=84029)
    p.add_argument('--triples', type=int, default=4096); p.add_argument('--budget', type=int, default=1024)
    p.add_argument('--seconds', type=float, default=20); args = p.parse_args(); (run if args.mode == 'run' else replay)(args)
