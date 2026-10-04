#!/usr/bin/env python3
"""Collect neural partial moves on fresh mixed-size synthetic fixtures."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from learned_global_replay import INPUT_FILES
from owner_amplitude_cycle_model import SCHEMA, AMPLITUDES, action_features, rank, load
from mixed_size_owner_cycles import mixed_action as owner_action, support as mixed_support
from mixed_size_synthetic_fixture import fixture
from synthetic_cycle_curriculum import flatten_fixture
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_fraction_conditioned_policy import sample_order
from run_mixed_size_owner_cycles import hashes as policy_hashes, motion_check
from train_anchor_pair_outcomes import outcome_target


def code_hashes():
    return policy_hashes() | {name: digest(Path(__file__).parent/name) for name in
        ['synthetic_mixed_size_owner_curriculum.py', 'mixed_size_synthetic_fixture.py', 'synthetic_cycle_curriculum.py']}


def collect_stage(root, graph, source, view, checkpoint, environment):
    stage = root/f'graph-{graph:02}'/view; stage.mkdir(parents=True, exist_ok=False)
    model, metadata = load(checkpoint); decoder = WalkDecoder(source)
    assert metadata['kind'] == 'signed-synthetic-graph-cycle-critic-v1'
    native = Native(environment, source, stage/'learned.tsv', view == 'overview', True)
    best = None; attempts = improving = accepted = 0; reasons = Counter(); started = time.monotonic()
    best_visual = native.initial['visual']; xs = []; ys = []; words = []; amplitudes_seen = []; gains = []
    shifted = 0; moved_counts = Counter()
    try:
        baseline = native.request(wire(decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)), 'MEASURE'))
        assert baseline['legal'] and baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        assert baseline['visual'] == native.initial['visual']
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        vocabulary_seed = 86069+graph; sampling_seed = 88643+graph
        vocabulary, support = mixed_support(decoder, nodes, 'small-global', vocabulary_seed, 128)
        scores, hashes, error = rank(model, decoder, nodes, vocabulary, view == 'overview')
        order, noise, policy = sample_order(scores, sampling_seed, 4.)
        np.savez_compressed(stage/'observations.npz', node_features=nodes, cycles=vocabulary,
                            amplitudes=AMPLITUDES, scores=scores, gumbel=noise)
        with (stage/'actions.jsonl').open('x') as stream:
            for index in order[:64]:
                ci, ai = map(int, np.unravel_index(index, scores.shape)); cycle = vocabulary[ci]; amplitudes = AMPLITUDES[ai]
                action = owner_action(decoder, cycle, amplitudes); shift, moved = motion_check(decoder, cycle, amplitudes, action)
                command = wire(action); result = native.request(command)
                x, _ = action_features(nodes, decoder, cycle[None], amplitudes[None], view == 'overview')
                gain = float(native.initial['visual']-result['visual']) if result['legal'] else np.nan
                xs.append(x[0]); ys.append(outcome_target(result, native.initial['visual']))
                words.append(cycle.copy()); amplitudes_seen.append(amplitudes.copy()); gains.append(gain)
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'amplitudeIndex': ai,
                    'cycle': cycle.tolist(), 'amplitudes': amplitudes.tolist(), 'score': float(scores[ci, ai]),
                    'gumbel': float(noise[ci, ai]), 'wireSha256': hashlib.sha256(command.encode()).hexdigest(),
                    'rawCentroidShiftL2': shift, 'movedOwners': moved, 'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1; improving += int(gain > 0)
                shifted += int(shift > 1e-8); moved_counts[str(moved)] += 1
                if result['accepted']:
                    accepted += 1; best_visual = result['visual']; best = action.copy()
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(source, stage, decoder, best)
    np.savez_compressed(stage/'labels.npz', x=np.array(xs), y=np.array(ys), cycles=np.array(words),
                        amplitudes=np.array(amplitudes_seen), actual_gain=np.array(gains))
    stats = json.loads((stage/'learned.tsv.stats.json').read_text())
    assert stats['visual'] == best_visual and stats['policyActionsEvaluated'] == attempts == 64
    assert stats['acceptedActions'] == accepted and stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == 0
    report = {'kind': 'frozen-signed-critic-synthetic-mixed-size-owner-amplitude-outcomes-v1', 'featureSchema': SCHEMA,
        'graph': graph, 'view': view, 'sourceDirectory': str(source),
        'sourceInputs': {n: digest(source/n) for n in INPUT_FILES}, 'checkpoint': str(checkpoint),
        'checkpointSha256': digest(checkpoint), 'environment': str(environment), 'environmentSha256': digest(environment),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'],
        'initialHard': baseline['hard'], 'initialIndividualHard': baseline['individualHard'], 'final': stats,
        'attempts': attempts, 'improvingLegalActions': improving, 'accepted': accepted,
        'vocabularySeed': vocabulary_seed, 'samplingSeed': sampling_seed, 'triples': 128, 'support': support,
        'policy': policy, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
        'candidateAmplitudes': AMPLITUDES.tolist(), 'reasons': dict(reasons), 'wallSeconds': time.monotonic()-started,
        'proposalsChangingRawTripletCentroid': shifted, 'movedOwnerCounts': dict(moved_counts),
        'newTrainingUpdates': 0, 'trainedOnPerOwnerAmplitudes': False, 'trainedOnMixedSizeWords': False,
        'positionsOptimizedOrRepaired': False, 'nativeSearchCalls': 0, 'codeSha256': code_hashes(),
        'observationsSha256': digest(stage/'observations.npz'), 'actionsSha256': digest(stage/'actions.jsonl'),
        'labelsSha256': digest(stage/'labels.npz'), 'sourceScoresUnchangedAcrossActions': True}
    (stage/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'graph': graph, 'view': view, 'actions': attempts, 'improvingLegalActions': improving,
                      'initial': native.initial['visual'], 'final': best_visual}), flush=True)
    return report


def collect(args):
    assert 64 <= args.start < 96 and 1 <= args.graphs <= 32 and args.start+args.graphs <= 96
    args.root.mkdir(parents=True, exist_ok=True); reports = []; started = time.monotonic()
    for graph in range(args.start, args.start+args.graphs):
        source = args.root/f'graph-{graph:02}'/'input'; fixture(source, 89003+graph)
        flat = source.parent/'input-individual'; flatten_fixture(source, flat)
        for view, directory in [('overview', source), ('individual', flat)]:
            reports.append(collect_stage(args.root, graph, directory, view, args.checkpoint, args.environment))
    summary = {'start': args.start, 'graphs': args.graphs, 'stages': len(reports),
        'actions': sum(r['attempts'] for r in reports), 'improvingLegalActions': sum(r['improvingLegalActions'] for r in reports),
        'graphsWithImprovingActions': len({r['graph'] for r in reports if r['improvingLegalActions']}),
        'nonuniformOwnerAmplitudesOnly': True, 'mixedSizeWordsOnly': True,
        'sourceSizesAndGridCreatedBeforeAnyScoring': True, 'individualFlatteningHasNoMovement': True,
        'codeSha256': code_hashes(), 'wallSeconds': time.monotonic()-started}
    path = args.root/f'collection-{args.start:02}-{args.graphs:02}.json'; assert not path.exists()
    path.write_text(json.dumps(summary, indent=2)+'\n'); print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--root', type=Path, required=True)
    p.add_argument('--checkpoint', type=Path, required=True); p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--start', type=int, default=64); p.add_argument('--graphs', type=int, default=4); collect(p.parse_args())
