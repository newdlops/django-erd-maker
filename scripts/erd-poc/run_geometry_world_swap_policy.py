#!/usr/bin/env python3
"""NN-selected geometry swaps; no exact future predicate enters ranking."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from geometry_world_model import load, digest
from geometry_world_multi_owner import WorldProxy
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_geometry_world_policy import source_geometry
from single_owner_cached_observer_v2 import CachedObserver


def run(args):
    assert 1 <= args.budget <= args.vocabulary <= 2048 and 0 < args.seconds <= 30
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    binding = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert str(args.directory) == binding['directory']
    inputs = {n: digest(args.directory/n) for n in INPUT_FILES}; assert inputs == binding['inputs']
    decoder = WalkDecoder(args.directory); observer = CachedObserver(args.directory, decoder)
    models, metadata = load(args.world, args.untrained); proxy = WorldProxy(models)
    zero = decoder.decode(np.arange(len(decoder.positions), dtype=np.int32))
    before, sizes, edges = source_geometry(observer, zero, args.view)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    best = None; accepted = attempts = improving = ties = 0; reasons = Counter(); rows = []; predictions = []
    wires = set(); best_visual = native.initial['visual']
    try:
        assert best_visual == binding['expectedVisual']
        baseline = native.request(wire(zero, 'MEASURE')); assert baseline['legal']
        assert baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        nodes = np.array([n['features'] for n in native.initial['nodes']])
        vocabulary = decoder.eligible_pairs(args.seed, args.vocabulary)
        active = np.any(nodes[:,4:6] > 0, axis=1)
        vocabulary = vocabulary[active[vocabulary].any(1)]
        rank_started = time.monotonic()
        for n, m in vocabulary:
            n, m = int(n), int(m); proposed = decoder.action(n, m)
            after, _, _ = source_geometry(observer, proposed, args.view)
            prediction = proxy.delta(before, after, sizes, edges)
            rows.append({'candidateIndex': len(rows), 'owners': [n,m], 'prediction': prediction,
                'score': -prediction['predictedDelta'], 'wireSha256': hashlib.sha256(wire(proposed).encode()).hexdigest()})
            predictions.append([prediction[k] for k in ['predictedDelta','predictedCrossDelta','predictedHitDelta','changedEdges','changedNodes']])
            if time.monotonic()-rank_started >= args.seconds: break
        assert rows; rank_seconds = time.monotonic()-rank_started
        scores = np.array([r['score'] for r in rows]); assert np.isfinite(scores).all()
        order = np.argsort(-scores, kind='stable')
        np.savez_compressed(args.out/'observations.npz', nodes=nodes, vocabulary=vocabulary,
            predictions=np.array(predictions), scores=scores, order=order)
        with (args.out/'ranking.jsonl').open('x') as stream:
            for row in rows: stream.write(json.dumps(row)+'\n')
        ranking_hash = digest(args.out/'ranking.jsonl'); evaluation_started = time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for rank, index in enumerate(order[:args.budget]):
                if time.monotonic()-evaluation_started >= args.seconds: break
                row = rows[int(index)]; proposed = decoder.action(*row['owners']); command = wire(proposed)
                h = hashlib.sha256(command.encode()).hexdigest(); assert h == row['wireSha256'] and h not in wires; wires.add(h)
                result = native.request(command); attempts += 1; reasons[result['reason']] += 1
                improving += int(result['legal'] and result['visual'] < baseline['visual'])
                ties += int(result['legal'] and result['visual'] == baseline['visual'])
                if result['accepted']: best = proposed.copy(); best_visual = result['visual']; accepted += 1
                stream.write(json.dumps({'rank': rank, **row, 'result': result})+'\n')
        assert digest(args.out/'ranking.jsonl') == ranking_hash; assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory, args.out, decoder, best)
    if best is not None: np.save(args.out/'best-action.npy', best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'], stats['policyActionsEvaluated'], stats['acceptedActions']) == (best_visual, attempts, accepted)
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    report = {'kind':'learned-geometric-event-world-global-equal-size-swap-v1', 'view':args.view,
        'seed':args.seed, 'vocabularyRequested':args.vocabulary, 'eligibleVocabulary':len(vocabulary), 'candidatesScored':len(rows),
        'budget':args.budget, 'secondsLimitPerRankingOrEvaluation':args.seconds,
        'sourceDirectory':str(args.directory), 'sourceBinding':str(args.source_binding),
        'sourceBindingSha256':digest(args.source_binding), 'sourceInputs':inputs,
        'worldCheckpoint':str(args.world), 'worldSha256':digest(args.world), 'worldMetadata':metadata,
        'untrainedWorldControl':args.untrained, 'environment':str(args.environment), 'environmentSha256':digest(args.environment),
        'initialVisual':baseline['visual'], 'initialIndividualVisual':baseline['individualVisual'],
        'attempts':attempts, 'accepted':accepted, 'improvingLegalActions':improving, 'legalTies':ties, 'reasons':dict(reasons),
        'final':stats, 'rankingSeconds':rank_seconds, 'wallSeconds':time.monotonic()-started,
        'coordinateProposalsChosenByNeuralPredictedEventDelta':True,
        'futureNativeMeasurementsBeforeScheduleFrozen':0, 'nativeSearchCalls':0, 'coordinateRepairs':0,
        'exactFuturePredicatesUsedDuringRanking':False, 'newTrainingUpdatesDuringInference':0,
        'savedGeometryMatchesModelOutput':True, 'inferenceFeatureInputDtype':'float32',
        'rankingSha256':ranking_hash, 'observationsSha256':digest(args.out/'observations.npz'),
        'actionsSha256':digest(args.out/'actions.jsonl'),
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in
            ['run_geometry_world_swap_policy.py','run_geometry_world_policy.py','geometry_world_model.py',
             'geometry_world_multi_owner.py','single_owner_cached_observer_v2.py','run_anchor_pair_walk.py','run_anchor_pair_policy.py']}}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','untrainedWorldControl','candidatesScored','attempts','accepted',
        'improvingLegalActions','reasons','rankingSeconds','wallSeconds']}|{'initialVisual':baseline['visual'],'finalVisual':best_visual}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--directory', type=Path, required=True); p.add_argument('--world', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True); p.add_argument('--view', choices=['overview','individual'], required=True)
    p.add_argument('--out', type=Path, required=True); p.add_argument('--untrained', action='store_true')
    p.add_argument('--seed', type=int, default=103907); p.add_argument('--vocabulary', type=int, default=1024)
    p.add_argument('--budget', type=int, default=128); p.add_argument('--seconds', type=float, default=30.)
    run(p.parse_args())
