#!/usr/bin/env python3
"""Learned event-delta ranking of a neural single-owner shortlist.

All future geometry scores are NN probabilities. Native receives the frozen
schedule only after every learned score has been recorded.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from geometry_world_model import WorldProxy, load as load_world, digest
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_single_owner_cycle_policy_v4 import prepare
from single_owner_cycle_model import load as load_actor, AMPLITUDES, action
from single_owner_cached_observer_v2 import CachedObserver

PRIOR_WEIGHT = .25


def source_geometry(observer, zero, view):
    pos, full_pos, physical, full, physical_edges = observer.decode(zero)
    if view == 'overview': return (pos, physical), observer.sizes, physical_edges
    return (full_pos, full), observer.full_sizes, observer.edges


def run(args):
    assert 1 <= args.budget <= args.shortlist <= 1024 and 0 < args.seconds <= 30
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    binding = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert str(args.directory) == binding['directory']
    inputs = {n: digest(args.directory/n) for n in INPUT_FILES}; assert inputs == binding['inputs']
    decoder = WalkDecoder(args.directory); observer = CachedObserver(args.directory, decoder)
    actor, actor_metadata = load_actor(args.actor); models, world_metadata = load_world(args.world, args.untrained)
    proxy = WorldProxy(models); zero = decoder.decode(np.arange(len(decoder.positions), dtype=np.int32))
    before, sizes, edges = source_geometry(observer, zero, args.view)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    best = None; accepted = attempts = improving = ties = 0; reasons = Counter(); wires = set()
    rows = []; predictions = []; best_visual = native.initial['visual']; ranking_seconds = None
    try:
        assert best_visual == binding['expectedVisual']
        baseline = native.request(wire(zero, 'MEASURE'))
        assert baseline['legal'] and baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        nodes = np.array([n['features'] for n in native.initial['nodes']])
        cycles, support, prior_scores, feature_hashes, error, allowed, noise, prior_policy, chosen, dedup = prepare(
            actor, decoder, nodes, args.view, args.seed, 1024, args.shortlist)
        rank_started = time.monotonic()
        for ci, ai in chosen:
            ci, ai = int(ci), int(ai); proposed = action(decoder, cycles[ci], AMPLITUDES[ai])
            after, _, _ = source_geometry(observer, proposed, args.view)
            prediction = proxy.delta(before, after, sizes, edges)
            score = -prediction['predictedDelta']+PRIOR_WEIGHT*float(prior_scores[ci, ai])
            predictions.append([prediction[k] for k in ['predictedDelta', 'predictedCrossDelta', 'predictedHitDelta', 'changedEdges', 'changedNodes']])
            rows.append({'shortlistIndex': len(rows), 'cycleIndex': ci, 'amplitudeIndex': ai,
                'cycle': cycles[ci].tolist(), 'amplitudes': AMPLITUDES[ai].tolist(),
                'priorScore': float(prior_scores[ci, ai]), 'worldPrediction': prediction, 'score': score,
                'wireSha256': hashlib.sha256(wire(proposed).encode()).hexdigest()})
            if time.monotonic()-rank_started >= args.seconds: break
        ranking_seconds = time.monotonic()-rank_started
        scores = np.array([r['score'] for r in rows]); assert np.isfinite(scores).all()
        order = np.argsort(-scores, kind='stable')
        np.savez_compressed(args.out/'observations.npz', nodes=nodes, cycles=cycles,
            prior_scores=prior_scores, allowed_indices=allowed, gumbel=noise,
            shortlist=chosen, predictions=np.array(predictions), world_scores=scores, order=order)
        with (args.out/'ranking.jsonl').open('x') as stream:
            for row in rows: stream.write(json.dumps(row)+'\n')
        # No future Native result exists at the point this order is frozen.
        ranking_hash = digest(args.out/'ranking.jsonl'); evaluation_started = time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for rank, index in enumerate(order[:args.budget]):
                if time.monotonic()-evaluation_started >= args.seconds: break
                row = rows[int(index)]; proposed = action(decoder, cycles[row['cycleIndex']], AMPLITUDES[row['amplitudeIndex']])
                command = wire(proposed); h = hashlib.sha256(command.encode()).hexdigest()
                assert h == row['wireSha256'] and h not in wires; wires.add(h)
                result = native.request(command); attempts += 1; reasons[result['reason']] += 1
                improving += int(result['legal'] and result['visual'] < baseline['visual'])
                ties += int(result['legal'] and result['visual'] == baseline['visual'])
                if result['accepted']: best = proposed.copy(); best_visual = result['visual']; accepted += 1
                stream.write(json.dumps({'rank': rank, **row, 'result': result})+'\n')
        assert digest(args.out/'ranking.jsonl') == ranking_hash
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory, args.out, decoder, best)
    if best is not None: np.save(args.out/'best-action.npy', best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'], stats['policyActionsEvaluated'], stats['acceptedActions']) == (best_visual, attempts, accepted)
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    report = {'kind': 'learned-pair-event-world-single-owner-policy-v1', 'view': args.view,
        'seed': args.seed, 'shortlistRequested': args.shortlist, 'shortlistScored': len(rows), 'budget': args.budget,
        'secondsLimitPerRankingOrEvaluation': args.seconds, 'priorWeight': PRIOR_WEIGHT,
        'sourceDirectory': str(args.directory), 'sourceBinding': str(args.source_binding),
        'sourceBindingSha256': digest(args.source_binding), 'sourceInputs': inputs,
        'actorCheckpoint': str(args.actor), 'actorSha256': digest(args.actor), 'actorMetadata': actor_metadata,
        'worldCheckpoint': str(args.world), 'worldSha256': digest(args.world), 'worldMetadata': world_metadata,
        'untrainedWorldControl': args.untrained, 'initialWorldParametersWithTrainedNormalization': args.untrained,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'support': support, 'priorPolicy': prior_policy, 'deduplication': dedup,
        'priorFeatureChunkHashes': feature_hashes, 'scaleFeatureMaxError': error,
        'initialVisual': baseline['visual'], 'initialIndividualVisual': baseline['individualVisual'],
        'attempts': attempts, 'accepted': accepted, 'improvingLegalActions': improving, 'legalTies': ties,
        'reasons': dict(reasons), 'final': stats, 'rankingSeconds': ranking_seconds,
        'wallSeconds': time.monotonic()-started, 'candidateCoordinatesChosenByModels': True,
        'worldInputContainsExactFutureEventBooleans': False, 'worldInputContainsExactFutureVisualScore': False,
        'futureNativeMeasurementsBeforeScheduleFrozen': 0, 'nativeSearchCalls': 0, 'coordinateRepairs': 0,
        'savedGeometryMatchesModelOutput': True, 'inferenceFeatureInputDtype': 'float32',
        'newTrainingUpdatesDuringInference': 0, 'rankingSha256': ranking_hash,
        'observationsSha256': digest(args.out/'observations.npz'), 'actionsSha256': digest(args.out/'actions.jsonl'),
        'codeSha256': {name: digest(Path(__file__).parent/name) for name in
            ['run_geometry_world_policy.py', 'geometry_world_model.py', 'train_geometry_world.py',
             'run_single_owner_cycle_policy_v4.py', 'single_owner_cycle_model.py',
             'single_owner_cached_observer_v2.py', 'run_anchor_pair_walk.py']}}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ['view', 'untrainedWorldControl', 'shortlistScored', 'attempts',
        'accepted', 'improvingLegalActions', 'reasons', 'rankingSeconds', 'wallSeconds']} | {'initialVisual': baseline['visual'],
        'finalVisual': best_visual}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--directory', type=Path, required=True); p.add_argument('--actor', type=Path, required=True)
    p.add_argument('--world', type=Path, required=True); p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--view', choices=['overview','individual'], required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=103509); p.add_argument('--shortlist', type=int, default=256)
    p.add_argument('--budget', type=int, default=64); p.add_argument('--seconds', type=float, default=30.)
    p.add_argument('--untrained', action='store_true'); run(p.parse_args())
