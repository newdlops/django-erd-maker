#!/usr/bin/env python3
"""Observe model-selected single-owner trajectories; save only strict best states."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace
import numpy as np
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, digest, wire
from run_anchor_pair_walk import WalkDecoder, array_hash, verify_saved
from run_single_owner_cycle_policy_v2 import hashes as policy_hashes, prepare
from single_owner_cycle_model import ACTION_SCHEMA, MODEL_KIND, AMPLITUDES, action, load, motion_key


def hashes():
    return policy_hashes() | {n: digest(Path(__file__).parent/n) for n in ['run_single_owner_neural_walk.py','run_single_owner_neural_walk_v2.py']}


def cumulative_action(decoder, delta):
    assert np.isfinite(delta).all()
    phases, _ = decoder.endpoint_offsets(delta)
    return np.concatenate([delta, phases])


def run(args):
    assert 1 <= args.rounds <= 64 and 1 <= args.per_round <= 32 and 1 <= args.budget <= 1024
    assert 1 <= args.triples <= 256 and 0 < args.seconds <= 30
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    base = WalkDecoder(args.directory); model, metadata = load(args.checkpoint)
    inputs = {n: digest(args.directory/n) for n in INPUT_FILES}
    binding = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert (str(args.directory), inputs) == (binding['directory'], binding['inputs'])
    current = np.zeros_like(base.positions); best = None; warm = None; warm_report = None; warm_full = None
    if args.warm_stage:
        warm_report = json.loads((args.warm_stage/'report.json').read_text())
        assert warm_report['sourceInputs'] == inputs and warm_report['checkpointSha256'] == digest(args.checkpoint)
        assert warm_report['codeSha256'] == policy_hashes() and warm_report['view'] == args.view
        assert warm_report['accepted'] > 0
        row = [json.loads(line) for line in (args.warm_stage/'actions.jsonl').read_text().splitlines()
               if json.loads(line)['result']['accepted']][-1]
        warm = action(base, np.array(row['cycle']), np.array(row['amplitudes']))
        assert np.array_equal(warm, np.load(args.warm_stage/'best-action.npy'))
        assert hashlib.sha256(wire(warm).encode()).hexdigest() == row['wireSha256']
        full = Native(args.environment, args.directory, args.out/'warm-full-check.tsv', args.view == 'overview', False)
        try:
            warm_full = full.request(wire(warm, 'MEASURE'))
            assert all(warm_full[k] == row['result'][k] for k in
                ['legal', 'reason', 'visual', 'individualVisual', 'spacing', 'hard', 'individualHard'])
        finally: full.close()
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view == 'overview', True)
    state = native.initial; visual = state['visual']; individual = state['individualVisual']
    assert visual == binding['expectedVisual']
    best_visual = visual; warm_replays = warm_accepted = attempts = strict = admitted = observations = visited_skips = 0
    reasons = Counter(); records = []; visited = {array_hash(current)}
    try:
        if warm is not None:
            replay = native.request(wire(warm)); assert replay['accepted']; warm_replays = warm_accepted = 1
            current = warm[:len(base.positions)].copy(); best = warm.copy(); best_visual = replay['visual']
            visual = replay['visual']; individual = replay['individualVisual']; visited.add(array_hash(current))
            state = native.request(wire(warm, 'OBS')); observations += 1
            assert state['observed'] and state['visual'] == visual and state['individualVisual'] == individual
        with (args.out/'actions.jsonl').open('x') as stream:
            for round_id in range(args.rounds):
                if attempts >= args.budget or time.monotonic()-started >= args.seconds: break
                decoder = SimpleNamespace(positions=base.positions+current, sizes=base.sizes,
                    provider=base.provider, irrelevant_isolate=base.irrelevant_isolate)
                nodes = np.array([row['features'] for row in state['nodes']])
                cycles, support, scores, feature_hashes, error, allowed, noise, policy, chosen, dedup = prepare(
                    model, decoder, nodes, args.view, args.seed+round_id, args.triples, args.per_round)
                path = args.out/f'observation-{round_id:02}.npz'
                np.savez_compressed(path, node_features=nodes, current_delta=current, cycles=cycles,
                    scores=scores, allowed_indices=allowed, gumbel=noise, proposal_schedule=chosen)
                records.append({'round': round_id, 'file': path.name, 'sha256': digest(path),
                    'visual': visual, 'individualVisual': individual, 'support': support,
                    'featureChunkHashes': feature_hashes, 'scaleFeatureMaxError': error,
                    'policy': policy, 'deduplication': dedup})
                lookup = {int(v): i for i, v in enumerate(allowed)}
                for proposal_rank, (ci, ai) in enumerate(chosen):
                    if attempts >= args.budget or time.monotonic()-started >= args.seconds: break
                    ci, ai = int(ci), int(ai); key = motion_key(decoder, cycles[ci], AMPLITUDES[ai])
                    cents = np.rint(current*100).astype(np.int64); cents[key[0]] += np.array(key[1:])
                    delta = cents.astype(np.float64)/100
                    # Each decoded transition changes exactly the selected owner.
                    changed = np.flatnonzero(np.any(delta-current != 0, axis=1)); assert changed.tolist() == [key[0]]
                    geometry_key = array_hash(delta); ni = lookup[int(np.ravel_multi_index((ci, ai), scores.shape))]
                    row = {'round': round_id, 'proposalRank': proposal_rank, 'cycleIndex': ci, 'amplitudeIndex': ai,
                        'cycle': cycles[ci].tolist(), 'amplitudes': AMPLITUDES[ai].tolist(),
                        'score': float(scores[ci, ai]), 'gumbel': float(noise[ni, 0]),
                        'ownerDeltaCents': list(key), 'cumulativeDeltaHash': geometry_key}
                    if geometry_key in visited:
                        visited_skips += 1; stream.write(json.dumps(row | {'skipped': 'visited neural state'})+'\n'); continue
                    proposed = cumulative_action(base, delta); result = native.request(wire(proposed))
                    attempts += 1; reasons[result['reason']] += 1
                    accept_walk = result['legal'] and result['visual'] <= visual and (
                        args.view == 'overview' or result['individualVisual'] <= individual)
                    stream.write(json.dumps(row | {'wireSha256': hashlib.sha256(wire(proposed).encode()).hexdigest(),
                        'result': result, 'walkAdmitted': accept_walk})+'\n')
                    if result['accepted']: strict += 1; best = proposed.copy(); best_visual = result['visual']
                    if accept_walk:
                        current = delta; visited.add(geometry_key); admitted += 1
                        visual = result['visual']; individual = result['individualVisual']
                        state = native.request(wire(proposed, 'OBS')); observations += 1
                        assert state['observed'] and state['visual'] == visual and state['individualVisual'] == individual
                        break
                stream.flush()
        assert native.request('SAVE')['saved']
    finally: native.close()
    verify_saved(args.directory, args.out, base, best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated'] == attempts+warm_replays and stats['acceptedActions'] == strict+warm_accepted
    assert stats['visual'] == best_visual and stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    np.save(args.out/'walk-delta.npy', current)
    if best is not None: np.save(args.out/'best-action.npy', best)
    report = {'kind': 'single-owner-neural-neutral-walk-v2', 'sourceBinding': str(args.source_binding),
        'sourceBindingSha256': digest(args.source_binding), 'cumulativeCentMovesEncodedExactly': True, 'actionSchema': ACTION_SCHEMA, 'view': args.view,
        'sourceDirectory': str(args.directory), 'sourceInputs': inputs, 'checkpoint': str(args.checkpoint),
        'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'warmStage': str(args.warm_stage) if args.warm_stage else None,
        'warmStageReportSha256': digest(args.warm_stage/'report.json') if args.warm_stage else None,
        'warmFullScoring': warm_full, 'knownNeuralActionsReplayed': warm_replays, 'warmAcceptedActions': warm_accepted,
        'newNeuralProposals': attempts, 'newStrictImprovements': strict, 'admittedWalkStates': admitted,
        'visitedStateSkips': visited_skips, 'observations': observations, 'rounds': records,
        'seed': args.seed, 'roundsLimit': args.rounds, 'perRound': args.per_round, 'triples': args.triples,
        'budget': args.budget, 'secondsLimit': args.seconds, 'initialVisual': native.initial['visual'],
        'initialIndividualVisual': native.initial['individualVisual'], 'walkVisual': visual,
        'walkIndividualVisual': individual, 'final': stats, 'reasons': dict(reasons),
        'newTrainingUpdates': 0, 'trainedOnSingleOwnerMoves': metadata['kind'] == MODEL_KIND, 'neuralSelectionAtEveryStep': True,
        'nativeSearchCalls': 0, 'coordinateRepairs': 0, 'neutralStatesCannotReplaceSavedBest': True,
        'savedGeometryMatchesModelOutput': True, 'allHistoryWireExclusionClaimed': False,
        'wallSeconds': time.monotonic()-started, 'actionsSha256': digest(args.out/'actions.jsonl'),
        'walkDeltaSha256': digest(args.out/'walk-delta.npy'), 'codeSha256': hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ['view', 'newNeuralProposals', 'newStrictImprovements',
        'admittedWalkStates', 'roundsLimit', 'observations', 'initialVisual', 'walkVisual', 'wallSeconds']}
        | {'finalVisual': best_visual}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--directory', type=Path, required=True); p.add_argument('--checkpoint', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--source-binding', type=Path, required=True); p.add_argument('--warm-stage', type=Path); p.add_argument('--view', choices=['overview', 'individual'], required=True)
    p.add_argument('--seed', type=int, default=90709); p.add_argument('--rounds', type=int, default=64)
    p.add_argument('--per-round', type=int, default=32); p.add_argument('--triples', type=int, default=128)
    p.add_argument('--budget', type=int, default=1024); p.add_argument('--seconds', type=float, default=30); run(p.parse_args())
