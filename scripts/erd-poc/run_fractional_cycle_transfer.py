#!/usr/bin/env python3
"""Frozen neural cycle words decoded as smaller, simultaneous translations.

Fractions are fixed experimental action units, not native search or repairs.
The critic was trained at unit fraction only; this is explicitly a transfer.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace
import numpy as np
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_anchor_pair_policy import Native, wire, digest
from run_directed_cycle_policy import replay as replay_source


def fractional_action(decoder, cycle, fraction):
    assert 0 <= fraction <= 1 and len(set(map(int, cycle))) == 3
    cycle = np.asarray(cycle, dtype=np.int32)
    assert all(np.array_equal(decoder.sizes[cycle[0]], decoder.sizes[n]) for n in cycle)
    delta = np.zeros_like(decoder.positions)
    delta[cycle] = fraction*(decoder.positions[np.roll(cycle, -1)]-decoder.positions[cycle])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    assert not delta[np.setdiff1d(np.arange(len(delta)), cycle)].any()
    phases, _ = decoder.endpoint_offsets(delta)
    return np.concatenate([delta, phases])


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_fractional_cycle_transfer.py',
        'run_directed_cycle_policy.py', 'run_anchor_pair_policy.py', 'run_anchor_pair_walk.py',
        'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def run(args):
    assert 0 < args.fraction < 1 and 1 <= args.budget <= 1024 and 0 < args.seconds <= 20
    replay_source(SimpleNamespace(out=args.source))
    source = json.loads((args.source/'report.json').read_text())
    assert source['kind'] == 'trained-directed-coordinated-cycle-policy-v1'
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    directory = Path(source['sourceDirectory']); decoder = WalkDecoder(directory)
    native = Native(source['environment'], directory, args.out/'learned.tsv', source['view'] == 'overview', True)
    rows = [json.loads(line) for line in (args.source/'actions.jsonl').read_text().splitlines()]
    attempts = accepted = 0; best = None; reasons = Counter(); best_visual = native.initial['visual']
    try:
        with (args.out/'actions.jsonl').open('x') as stream:
            for row in rows[:args.budget]:
                if time.monotonic()-started >= args.seconds:
                    break
                action = fractional_action(decoder, row['cycle'], args.fraction)
                command = wire(action); result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'cycle': row['cycle'], 'neuralScore': row['score'],
                    'unitFractionResult': row['result'], 'fraction': args.fraction,
                    'wireSha256': hashlib.sha256(command.encode()).hexdigest(), 'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1
                if result['accepted']:
                    accepted += 1; best = action.copy(); best_visual = result['visual']
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['policyActionsEvaluated'], stats['acceptedActions'], stats['visual']) == (attempts, accepted, best_visual)
    verify_saved(directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'frozen-neural-cycle-fractional-decoder-transfer-v1', 'view': source['view'],
        'sourceReport': str(args.source/'report.json'), 'sourceReportSha256': digest(args.source/'report.json'),
        'sourceDirectory': str(directory), 'sourceInputs': source['sourceInputs'],
        'checkpoint': source['checkpoint'], 'checkpointSha256': source['checkpointSha256'],
        'environment': source['environment'], 'environmentSha256': source['environmentSha256'],
        'fraction': args.fraction, 'budget': args.budget, 'secondsLimit': args.seconds,
        'attempts': attempts, 'accepted': accepted, 'reasons': dict(reasons),
        'initialVisual': source['initialVisual'], 'final': stats, 'wallSeconds': time.monotonic()-started,
        'sameNeuralWordsAndScores': True, 'trainedOnThisFraction': False, 'newTrainingUpdates': 0,
        'fractionFixedBeforeMeasurements': True, 'coordinateRepairs': 0, 'heuristicSearchCalls': 0,
        'savedGeometryMatchesModelOutput': True, 'actionsSha256': digest(args.out/'actions.jsonl'),
        'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'fraction', 'attempts', 'accepted', 'reasons', 'wallSeconds']}
        | {'finalVisual': stats['visual']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert code_hashes() == report['codeSha256'] and digest(args.out/'actions.jsonl') == report['actionsSha256']
    assert digest(report['sourceReport']) == report['sourceReportSha256']
    source_dir = Path(report['sourceReport']).parent; replay_source(SimpleNamespace(out=source_dir))
    source_rows = [json.loads(line) for line in (source_dir/'actions.jsonl').read_text().splitlines()]
    decoder = WalkDecoder(directory); attempts = accepted = 0; best = None
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); source = source_rows[attempts]
        assert row['rank'] == attempts and row['cycle'] == source['cycle'] and row['neuralScore'] == source['score']
        assert row['unitFractionResult'] == source['result'] and row['fraction'] == report['fraction']
        action = fractional_action(decoder, row['cycle'], report['fraction'])
        assert hashlib.sha256(wire(action).encode()).hexdigest() == row['wireSha256']
        attempts += 1
        if row['result']['accepted']:
            best = action; accepted += 1
    assert (attempts, accepted) == (report['attempts'], report['accepted'])
    if best is not None:
        assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'modelWordsAndScoresReplayed': True,
        'fractionalActionsReplayed': attempts, 'accepted': accepted,
        'savedGeometryMatchesModelOutput': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('command', choices=['run', 'replay'])
    parser.add_argument('--source', type=Path); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fraction', type=float); parser.add_argument('--budget', type=int, default=128)
    parser.add_argument('--seconds', type=float, default=20)
    args = parser.parse_args(); (run if args.command == 'run' else replay)(args)
