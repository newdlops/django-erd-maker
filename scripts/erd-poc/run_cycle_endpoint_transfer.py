#!/usr/bin/env python3
"""Matched decoder transfer: retain neural words, change only port pooling."""
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
from run_neural_cycle_policy import cycle_action
from run_directed_cycle_policy import replay as replay_source


class IndependentDecoder(WalkDecoder):
    def pool(self, values):
        return values


class CircularDecoder(WalkDecoder):
    def pool(self, values):
        sine = np.bincount(self.endpoint_groups, weights=np.sin(2*np.pi*values.ravel()))
        cosine = np.bincount(self.endpoint_groups, weights=np.cos(2*np.pi*values.ravel()))
        if np.any(np.hypot(sine, cosine)/self.endpoint_counts < 1e-10):
            raise ArithmeticError('ambiguous circular port mean')
        phase = np.arctan2(sine, cosine)/(2*np.pi)
        return phase[self.endpoint_groups].reshape(values.shape)


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['run_cycle_endpoint_transfer.py',
        'run_directed_cycle_policy.py', 'run_neural_cycle_policy.py', 'run_anchor_pair_policy.py',
        'run_anchor_pair_walk.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def wrapped_group_count(decoder, action):
    count = len(decoder.positions); delta = action[:count]
    direction = decoder.ray_base_direction+delta[decoder.owner_edges[:, 1]]-delta[decoder.owner_edges[:, 0]]
    phase, _ = decoder.ray_phase(direction)
    values = (np.mod(phase-decoder.ray_initial_phase+.5, 1.)-.5).ravel()
    minimum = np.full(len(decoder.endpoint_counts), np.inf); maximum = np.full_like(minimum, -np.inf)
    np.minimum.at(minimum, decoder.endpoint_groups, values); np.maximum.at(maximum, decoder.endpoint_groups, values)
    return int(np.sum(maximum-minimum > .5))


def run(args):
    assert 1 <= args.budget <= 1024 and 0 < args.seconds <= 20
    replay_source(SimpleNamespace(out=args.source))
    source = json.loads((args.source/'report.json').read_text())
    assert source['kind'] == 'trained-directed-coordinated-cycle-policy-v1'
    assert digest(source['environment']) == source['environmentSha256']
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    directory = Path(source['sourceDirectory'])
    decoder = (CircularDecoder if args.mode == 'circular' else IndependentDecoder)(directory)
    original = WalkDecoder(directory)
    zero = decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)); assert np.count_nonzero(zero) == 0
    native = Native(source['environment'], directory, args.out/'learned.tsv', source['view'] == 'overview', True)
    rows = [json.loads(line) for line in (args.source/'actions.jsonl').read_text().splitlines()]
    attempts = considered = accepted = rejects = wrapped = 0; best = None; reasons = Counter(); best_visual = native.initial['visual']
    try:
        zero_result = native.request(wire(zero, 'MEASURE'))
        assert zero_result['legal'] and zero_result['visual'] == source['initialVisual']
        with (args.out/'actions.jsonl').open('x') as stream:
            for row in rows[:args.budget]:
                if time.monotonic()-started >= args.seconds:
                    break
                record = {'rank': considered, 'cycle': row['cycle'], 'neuralScore': row['score'],
                    'arithmeticResult': row['result']}; considered += 1
                try:
                    action = cycle_action(decoder, row['cycle'])
                except ArithmeticError:
                    rejects += 1; stream.write(json.dumps(record | {'decoderRejected': 'ambiguous circular mean'})+'\n'); continue
                baseline_action = cycle_action(original, row['cycle'])
                assert np.array_equal(action[:len(decoder.positions)], baseline_action[:len(decoder.positions)])
                wrap_count = wrapped_group_count(original, baseline_action); wrapped += wrap_count > 0
                command = wire(action); result = native.request(command); attempts += 1; reasons[result['reason']] += 1
                if result['accepted']:
                    accepted += 1; best = action.copy(); best_visual = result['visual']
                stream.write(json.dumps(record | {'wrappedEndpointGroups': wrap_count,
                    'wireSha256': hashlib.sha256(command.encode()).hexdigest(), 'result': result})+'\n')
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['policyActionsEvaluated'], stats['acceptedActions'], stats['visual']) == (attempts, accepted, best_visual)
    verify_saved(directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'matched-neural-cycle-endpoint-decoder-transfer-v1', 'view': source['view'], 'mode': args.mode,
        'sourceReport': str(args.source/'report.json'), 'sourceReportSha256': digest(args.source/'report.json'),
        'sourceDirectory': str(directory), 'sourceInputs': source['sourceInputs'],
        'checkpoint': source['checkpoint'], 'checkpointSha256': source['checkpointSha256'],
        'environment': source['environment'], 'environmentSha256': source['environmentSha256'],
        'budget': args.budget, 'secondsLimit': args.seconds, 'considered': considered, 'attempts': attempts,
        'decoderRejected': rejects, 'accepted': accepted, 'reasons': dict(reasons), 'wrappedProposals': wrapped,
        'initialVisual': source['initialVisual'], 'final': stats, 'wallSeconds': time.monotonic()-started,
        'sameNeuralWordsScoresAndTranslations': True, 'trainedOnThisDecoder': False, 'newTrainingUpdates': 0,
        'coordinateRepairs': 0, 'heuristicSearchCalls': 0, 'ambiguousCircularMeansRejectedWithoutFallback': True,
        'zeroActionPreservesSource': True, 'savedGeometryMatchesModelOutput': True,
        'actionsSha256': digest(args.out/'actions.jsonl'), 'codeSha256': code_hashes()}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'mode', 'considered', 'attempts', 'decoderRejected', 'accepted',
        'reasons', 'wrappedProposals', 'wallSeconds']} | {'finalVisual': stats['visual']}))


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert code_hashes() == report['codeSha256'] and digest(args.out/'actions.jsonl') == report['actionsSha256']
    assert digest(report['sourceReport']) == report['sourceReportSha256']
    source_dir = Path(report['sourceReport']).parent; replay_source(SimpleNamespace(out=source_dir))
    source_rows = [json.loads(line) for line in (source_dir/'actions.jsonl').read_text().splitlines()]
    decoder = (CircularDecoder if report['mode'] == 'circular' else IndependentDecoder)(directory)
    original = WalkDecoder(directory); best = None; considered = attempts = rejects = accepted = wrapped = 0
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); source = source_rows[considered]
        assert row['rank'] == considered and row['cycle'] == source['cycle'] and row['neuralScore'] == source['score']
        assert row['arithmeticResult'] == source['result']; considered += 1
        if 'decoderRejected' in row:
            try:
                cycle_action(decoder, row['cycle'])
            except ArithmeticError:
                rejects += 1; continue
            raise AssertionError('recorded decoder rejection not reproduced')
        action = cycle_action(decoder, row['cycle']); baseline = cycle_action(original, row['cycle'])
        assert np.array_equal(action[:len(decoder.positions)], baseline[:len(decoder.positions)])
        assert hashlib.sha256(wire(action).encode()).hexdigest() == row['wireSha256']
        count = wrapped_group_count(original, baseline); assert count == row['wrappedEndpointGroups']; wrapped += count > 0
        attempts += 1
        if row['result']['accepted']:
            best = action; accepted += 1
    assert (considered, attempts, rejects, accepted, wrapped) == tuple(report[key] for key in
        ['considered', 'attempts', 'decoderRejected', 'accepted', 'wrappedProposals'])
    if best is not None:
        assert np.array_equal(best, np.load(args.out/'best-action.npy'))
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'considered': considered, 'nativeActions': attempts, 'accepted': accepted,
        'sameNeuralWordsAndTranslations': True, 'decodedActionWiresReplayed': True,
        'savedGeometryMatchesModelOutput': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('command', choices=['run', 'replay'])
    p.add_argument('--source', type=Path); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--mode', choices=['circular', 'independent']); p.add_argument('--budget', type=int, default=128)
    p.add_argument('--seconds', type=float, default=20); args = p.parse_args(); (run if args.command == 'run' else replay)(args)
