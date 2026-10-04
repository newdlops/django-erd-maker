#!/usr/bin/env python3
"""Accumulate nonregressing neural actions on directly conflicting card owners.

Each state's policy order is fixed before measurements. Existing integer-score
admission of a tie serializes the chosen model output; no coordinates are
repaired. The promoted geometry is untouched until an independently audited
strict improvement is available.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import time
import numpy as np
from fractional_cycle_model import load, SCHEMA
from learned_global_replay import INPUT_FILES, pairs, routes
from run_anchor_pair_policy import wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved, array_hash
from export_anchor_pair_walk import SnapshotNative
from run_directed_cycle_policy import coordinated_cycles, measured
from run_fraction_conditioned_policy import rank, sample_order, FRACTIONS
from run_fractional_cycle_transfer import fractional_action
from joint_neural_ports import perimeter_points

STATIC_FILES = ['nodes.tsv', 'edges.tsv', 'individual.nodes.tsv', 'individual.edges.tsv', 'components.tsv', 'groups.tsv']
SAVED_INPUTS = {'positions.tsv': 'learned.tsv', 'routes.tsv': 'learned.tsv.routes.tsv',
    'individual.positions.tsv': 'learned.tsv.individual', 'individual.routes.tsv': 'learned.tsv.individual.routes.tsv'}


def geometry_signature(positions, full_positions, ports):
    # Micro-unit fingerprinting suppresses arithmetic ulps for cycle detection
    # only. The actual coordinates are passed to the native decoder unchanged.
    quantized = [np.rint(value*1e6).astype('<i8') for value in [positions, full_positions, ports]]
    return hashlib.sha256(b''.join(value.tobytes() for value in quantized)).hexdigest()


def source_signature(directory):
    values = [np.array(list(reader(directory/name).values())) for reader, name in
        [(pairs, 'positions.tsv'), (pairs, 'individual.positions.tsv'), (routes, 'individual.routes.tsv')]]
    return geometry_signature(*values)


def action_signature(directory, decoder, action):
    count = len(decoder.positions); decoded = np.fromstring(wire(action)[4:], sep=' ').reshape(-1, 2)
    delta = np.copysign(np.floor(abs(decoded[:count])*100+.5), decoded[:count])/100
    full = np.array(list(pairs(directory/'individual.positions.tsv').values()))+delta[decoder.provider.owner]
    provider = decoder.provider
    changed = perimeter_points(provider.phase+decoded[count:], provider.endpoint_sizes)[0]-provider.base_boundary
    ports = provider.original_ports+changed+delta[provider.owner_edges]
    ports = np.copysign(np.floor(abs(ports)*100+.5), ports)/100
    return geometry_signature(decoder.positions+delta, full, ports)


def make_next_source(directory, stage, out):
    out.mkdir(parents=True, exist_ok=False)
    for name in INPUT_FILES:
        shutil.copyfile(stage/SAVED_INPUTS[name] if name in SAVED_INPUTS else directory/name, out/name)
    for name in ['experiment.json', 'export.json']:
        if (directory/name).is_file():
            shutil.copyfile(directory/name, out/name)
    assert all(digest(directory/name) == digest(out/name) for name in STATIC_FILES)
    assert all(digest(stage/saved) == digest(out/name) for name, saved in SAVED_INPUTS.items())


def complete_measured(metadata, view, independent_root):
    parts = [measured(metadata, view)]; sources = []
    for name in ['sample1', 'sample4']:
        stage = independent_root/(view+'-'+name); report = json.loads((stage/'report.json').read_text())
        assert digest(stage/'actions.jsonl') == report['actionsSha256']
        parts.append(np.array([json.loads(line)['cycle'] for line in (stage/'actions.jsonl').read_text().splitlines()], dtype=np.int32))
        sources.append({'report': str(stage/'report.json'), 'reportSha256': digest(stage/'report.json'),
            'actionsSha256': report['actionsSha256']})
    return np.concatenate(parts), sources


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_fraction_cycle_walk.py',
        'run_fraction_conditioned_policy.py', 'fractional_cycle_model.py', 'directed_cycle_model.py',
        'run_directed_cycle_policy.py', 'run_fractional_cycle_transfer.py', 'run_anchor_pair_policy.py',
        'run_anchor_pair_walk.py', 'export_anchor_pair_walk.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def run(args):
    assert 1 <= args.rounds <= 24 and 1 <= args.per_round <= 128 and 1 <= args.budget <= 2048
    assert 1 <= args.triples <= 4096 and 0 < args.seconds <= 60 and 0 < args.temperature <= 8
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    model, metadata = load(args.checkpoint)
    initial_inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert initial_inputs == metadata['sourceInputsByView'][args.view]
    measured_cycles, measured_sources = complete_measured(metadata, args.view, args.independent)
    initial_signature = source_signature(args.directory); visited = {initial_signature}; directory = args.directory
    attempts = considered = admitted = improved = 0; reasons = Counter(); steps = []
    initial_visual = current_visual = best_visual = None; initial_individual = current_individual = None
    strict_best = None; stop = 'round limit'; moved_owners = set()
    for round_id in range(args.rounds):
        if attempts >= args.budget or time.monotonic()-started >= args.seconds:
            stop = 'bounded budget or time'; break
        stage = args.out/f'round-{round_id:02}'; stage.mkdir(exist_ok=False)
        inputs = {name: digest(directory/name) for name in INPUT_FILES}
        assert all(inputs[name] == initial_inputs[name] for name in STATIC_FILES)
        assert source_signature(directory) in visited
        decoder = WalkDecoder(directory)
        native = SnapshotNative(args.environment, directory, stage/'learned.tsv', args.view == 'overview')
        local_attempts = local_considered = 0; action = None; accepted_row = None
        try:
            if initial_visual is None:
                initial_visual = best_visual = current_visual = native.initial['visual']
                initial_individual = current_individual = native.initial['individualVisual']
            assert native.initial['visual'] == current_visual and native.initial['individualVisual'] == current_individual
            zero = decoder.decode(np.arange(len(decoder.positions), dtype=np.int32))
            baseline = native.request(wire(zero, 'MEASURE'))
            assert baseline['legal'] and baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
            assert baseline['visual'] == current_visual and baseline['individualVisual'] == current_individual
            nodes = np.array([row['features'] for row in native.initial['nodes']])
            cycles, support = coordinated_cycles(decoder, nodes, measured_cycles, args.seed+round_id, args.triples)
            scores, hashes, error = rank(model, decoder, nodes, cycles, args.view == 'overview')
            order, noise, policy = sample_order(scores, args.seed+1000+round_id, args.temperature)
            np.savez_compressed(stage/'observations.npz', node_features=nodes, cycles=cycles, fractions=FRACTIONS, scores=scores, gumbel=noise)
            with (stage/'actions.jsonl').open('x') as stream:
                for index in order[:args.per_round]:
                    if attempts >= args.budget or time.monotonic()-started >= args.seconds:
                        stop = 'bounded budget or time'; break
                    ci, fi = map(int, np.unravel_index(index, scores.shape)); cycle = cycles[ci]; fraction = float(FRACTIONS[fi])
                    proposed = fractional_action(decoder, cycle, fraction); signature = action_signature(directory, decoder, proposed)
                    row = {'rank': local_considered, 'cycle': cycle.tolist(), 'fraction': fraction,
                        'score': float(scores[ci, fi]), 'gumbel': float(noise[ci, fi]), 'geometrySignature': signature}
                    local_considered += 1; considered += 1
                    if signature in visited:
                        stream.write(json.dumps(row | {'skipped': 'visited'})+'\n'); continue
                    command = wire(proposed); result = native.request(command)
                    attempts += 1; local_attempts += 1; reasons[result['reason']] += 1
                    row |= {'wireSha256': hashlib.sha256(command.encode()).hexdigest(), 'result': result}
                    stream.write(json.dumps(row)+'\n')
                    if result['accepted']:
                        assert result['legal'] and result['visual'] <= current_visual
                        assert result['hard'] == result['individualHard'] == result['spacing'] == 0
                        if args.view == 'individual':
                            assert result['individualVisual'] <= current_individual
                        action = proposed; accepted_row = row; admitted += 1
                        if result['visual'] < best_visual:
                            best_visual = result['visual']; improved += 1; strict_best = stage
                        current_visual = result['visual']; current_individual = result['individualVisual']
                        moved_owners.update(map(int, cycle))
                        break
            assert native.request('SAVE')['saved']
        finally:
            native.close()
        stats = json.loads((stage/'learned.tsv.stats.json').read_text())
        assert stats['policyActionsEvaluated'] == local_attempts and stats['acceptedActions'] == int(action is not None)
        assert stats['visual'] == current_visual
        verify_saved(directory, stage, decoder, action)
        next_directory = None
        if action is not None:
            np.save(stage/'accepted-action.npy', action)
            next_directory = args.out/f'source-{round_id+1:02}'
            make_next_source(directory, stage, next_directory)
            assert source_signature(next_directory) == accepted_row['geometrySignature']
            assert accepted_row['geometrySignature'] not in visited
            visited.add(accepted_row['geometrySignature'])
            measured_cycles = np.vstack([measured_cycles, np.array([accepted_row['cycle']], dtype=np.int32)])
        step = {'round': round_id, 'sourceDirectory': str(directory), 'sourceInputs': inputs,
            'nextSourceDirectory': str(next_directory) if next_directory is not None else None,
            'attempts': local_attempts, 'considered': local_considered, 'admitted': action is not None,
            'acceptedRow': accepted_row, 'visual': current_visual, 'individualVisual': current_individual,
            'support': support, 'policy': policy, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
            'stats': stats, 'savedGeometryMatchesModelOutput': True,
            'observationsSha256': digest(stage/'observations.npz'), 'actionsSha256': digest(stage/'actions.jsonl')}
        (stage/'report.json').write_text(json.dumps(step, indent=2)+'\n'); steps.append(step)
        print(json.dumps({'view': args.view, 'round': round_id, 'attempts': local_attempts,
            'admitted': action is not None, 'fraction': accepted_row['fraction'] if accepted_row else None,
            'visual': current_visual, 'seconds': time.monotonic()-started}), flush=True)
        if action is None:
            stop = 'no nonregressing neural action in bounded round'; break
        directory = next_directory
    report = {'kind': 'fraction-conditioned-direct-conflict-neutral-trajectory-v1', 'schema': SCHEMA,
        'view': args.view, 'sourceDirectory': str(args.directory), 'sourceInputs': initial_inputs,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'independentMeasuredSources': measured_sources, 'seed': args.seed, 'temperature': args.temperature,
        'triples': args.triples, 'roundLimit': args.rounds, 'perRound': args.per_round, 'budget': args.budget,
        'secondsLimit': args.seconds, 'attempts': attempts, 'considered': considered, 'admittedStates': admitted,
        'strictImprovements': improved, 'initialVisual': initial_visual, 'initialIndividualVisual': initial_individual,
        'walkVisual': current_visual, 'walkIndividualVisual': current_individual, 'bestVisual': best_visual,
        'strictBestStage': str(strict_best) if strict_best is not None else None,
        'distinctMovedDirectConflictOwners': len(moved_owners), 'finalSourceDirectory': str(directory),
        'reasons': dict(reasons), 'steps': steps, 'stopReason': stop, 'wallSeconds': time.monotonic()-started,
        'existingIntegerScoreTieAdmissionAllowance': 1, 'actualSourceObjectiveRegression': 0,
        'neutralStatesNotPromoted': True, 'policyFrozenTransferredToOwnGeneratedStates': True,
        'newTrainingUpdates': 0, 'modelNamesAsFeatures': False, 'absoluteCoordinatesAsFeatures': False,
        'heuristicSearchCalls': 0, 'coordinateRepairs': 0, 'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'attempts', 'admittedStates', 'strictImprovements',
        'initialVisual', 'walkVisual', 'distinctMovedDirectConflictOwners', 'stopReason', 'wallSeconds']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); original = Path(report['sourceDirectory'])
    assert digest(report['checkpoint']) == report['checkpointSha256'] and code_hashes() == report['codeSha256']
    assert digest(report['environment']) == report['environmentSha256']
    assert {name: digest(original/name) for name in INPUT_FILES} == report['sourceInputs']
    model, metadata = load(Path(report['checkpoint'])); assert metadata == report['checkpointTraining']
    measured_cycles = measured(metadata, report['view'])
    for record in report['independentMeasuredSources']:
        assert digest(record['report']) == record['reportSha256']
        stage = Path(record['report']).parent; assert digest(stage/'actions.jsonl') == record['actionsSha256']
        measured_cycles = np.vstack([measured_cycles, np.array([json.loads(line)['cycle']
            for line in (stage/'actions.jsonl').read_text().splitlines()], dtype=np.int32)])
    visited = {source_signature(original)}; directory = original
    visual = best_visual = report['initialVisual']; individual = report['initialIndividualVisual']
    attempts = considered = admitted = improved = scores_count = 0; moved_owners = set(); strict_best = None
    for step in report['steps']:
        stage = args.out/f"round-{step['round']:02}"; assert str(directory) == step['sourceDirectory']
        assert {name: digest(directory/name) for name in INPUT_FILES} == step['sourceInputs']
        assert digest(stage/'observations.npz') == step['observationsSha256']
        assert digest(stage/'actions.jsonl') == step['actionsSha256']
        decoder = WalkDecoder(directory)
        with np.load(stage/'observations.npz', allow_pickle=False) as saved:
            nodes = saved['node_features'].copy()
            cycles, support = coordinated_cycles(decoder, nodes, measured_cycles, report['seed']+step['round'], report['triples'])
            assert support == step['support'] and np.array_equal(cycles, saved['cycles'])
            assert np.array_equal(FRACTIONS, saved['fractions'])
            scores, hashes, _ = rank(model, decoder, nodes, cycles, report['view'] == 'overview')
            assert hashes == step['featureChunkHashes'] and np.array_equal(scores, saved['scores'])
            order, noise, policy = sample_order(scores, report['seed']+1000+step['round'], report['temperature'])
            assert np.array_equal(noise, saved['gumbel']) and policy == step['policy']
        scores_count += scores.size; action = None; accepted_row = None; local_attempts = local_considered = 0
        for line in (stage/'actions.jsonl').read_text().splitlines():
            row = json.loads(line); assert action is None and row['rank'] == local_considered
            ci, fi = map(int, np.unravel_index(order[local_considered], scores.shape))
            cycle = cycles[ci]; fraction = float(FRACTIONS[fi]); proposed = fractional_action(decoder, cycle, fraction)
            assert row['cycle'] == cycle.tolist() and row['fraction'] == fraction
            assert row['score'] == scores[ci, fi] and row['gumbel'] == noise[ci, fi]
            signature = action_signature(directory, decoder, proposed); assert signature == row['geometrySignature']
            local_considered += 1; considered += 1
            if 'skipped' in row:
                assert row['skipped'] == 'visited' and signature in visited; continue
            assert signature not in visited and hashlib.sha256(wire(proposed).encode()).hexdigest() == row['wireSha256']
            local_attempts += 1; attempts += 1; result = row['result']
            admissible = result['legal'] and result['visual'] <= visual and (report['view'] == 'overview' or result['individualVisual'] <= individual)
            assert admissible == result['accepted']
            if admissible:
                assert result['hard'] == result['individualHard'] == result['spacing'] == 0
                action = proposed; accepted_row = row; admitted += 1
                if result['visual'] < best_visual:
                    best_visual = result['visual']; improved += 1; strict_best = str(stage)
                visual = result['visual']; individual = result['individualVisual']; moved_owners.update(map(int, cycle))
        assert (local_attempts, local_considered, action is not None) == (step['attempts'], step['considered'], step['admitted'])
        assert accepted_row == step['acceptedRow'] and visual == step['visual'] and individual == step['individualVisual']
        verify_saved(directory, stage, decoder, action)
        if action is not None:
            assert np.array_equal(action, np.load(stage/'accepted-action.npy'))
            next_directory = Path(step['nextSourceDirectory'])
            assert all(digest(directory/name) == digest(next_directory/name) for name in STATIC_FILES)
            assert all(digest(stage/saved) == digest(next_directory/name) for name, saved in SAVED_INPUTS.items())
            assert source_signature(next_directory) == accepted_row['geometrySignature']
            visited.add(accepted_row['geometrySignature'])
            measured_cycles = np.vstack([measured_cycles, np.array([accepted_row['cycle']], dtype=np.int32)])
            directory = next_directory
    assert (attempts, considered, admitted, improved, visual, best_visual) == tuple(report[key] for key in
        ['attempts', 'considered', 'admittedStates', 'strictImprovements', 'walkVisual', 'bestVisual'])
    assert len(moved_owners) == report['distinctMovedDirectConflictOwners'] and strict_best == report['strictBestStage']
    assert str(directory) == report['finalSourceDirectory']
    result = {'status': 'pass', 'allNeuralActionScoresReplayed': scores_count, 'modelActionsReplayed': attempts,
        'admittedStates': admitted, 'strictImprovements': improved, 'distinctMovedDirectConflictOwners': len(moved_owners),
        'generatedSourcesByteIdenticalToModelSaves': True, 'sourceObjectiveRegression': 0,
        'nativeScoresRecomputed': False, 'neutralStatesNotPromoted': True}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('command', choices=['run', 'replay'])
    parser.add_argument('--out', type=Path, required=True); parser.add_argument('--directory', type=Path)
    parser.add_argument('--checkpoint', type=Path); parser.add_argument('--environment', type=Path)
    parser.add_argument('--independent', type=Path); parser.add_argument('--view', choices=['individual', 'overview'])
    parser.add_argument('--seed', type=int, default=84061); parser.add_argument('--temperature', type=float, default=1.)
    parser.add_argument('--triples', type=int, default=1024); parser.add_argument('--rounds', type=int, default=20)
    parser.add_argument('--per-round', type=int, default=64); parser.add_argument('--budget', type=int, default=512)
    parser.add_argument('--seconds', type=float, default=45)
    args = parser.parse_args(); (run if args.command == 'run' else replay)(args)
