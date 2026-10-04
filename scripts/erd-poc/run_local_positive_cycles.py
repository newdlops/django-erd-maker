#!/usr/bin/env python3
"""Select local Captain cycles with the frozen positive-example neural critic."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from fractional_cycle_model import load
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_directed_cycle_policy import measured
from local_positive_cycle_support import local_cycles as coordinated_cycles
from run_fraction_conditioned_policy import rank, sample_order, FRACTIONS, code_hashes as parent_hashes
from run_fractional_cycle_transfer import fractional_action


def hashes():
    return parent_hashes() | {'run_local_positive_cycles.py': digest(__file__), 'local_positive_cycle_support.py': digest(Path(__file__).parent/'local_positive_cycle_support.py')}


def exclusions(metadata, view, previous_root):
    words = [measured(metadata, view)]; inputs = metadata['sourceInputsByView'][view]; reports = []
    for name in ['sample1', 'sample4']:
        stage = previous_root/(view+'-'+name); report = json.loads((stage/'report.json').read_text())
        assert report['sourceInputs'] == inputs and digest(stage/'actions.jsonl') == report['actionsSha256']
        words.append(np.array([json.loads(line)['cycle'] for line in (stage/'actions.jsonl').read_text().splitlines()], dtype=np.int32))
        reports.append({'path': str(stage/'report.json'), 'sha256': digest(stage/'report.json'),
            'actionsSha256': report['actionsSha256']})
    stage = previous_root/(view+'-walk1')/'round-00'; report = json.loads((stage/'report.json').read_text())
    assert report['sourceInputs'] == inputs and digest(stage/'actions.jsonl') == report['actionsSha256']
    rows = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
    words.append(np.array([row['cycle'] for row in rows if 'result' in row], dtype=np.int32).reshape(-1, 3))
    reports.append({'path': str(stage/'report.json'), 'sha256': digest(stage/'report.json'),
        'actionsSha256': report['actionsSha256']})
    return np.concatenate(words), reports


def run(args):
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    model, metadata = load(args.checkpoint)
    assert metadata['kind'] == 'positive-synthetic-graph-cycle-critic-v1'
    inputs = {name: digest(args.directory/name) for name in INPUT_FILES}
    assert inputs == metadata['sourceInputsByView'][args.view]
    old_words, prior = exclusions(metadata, args.view, args.previous)
    decoder = WalkDecoder(args.directory)
    native = Native(args.environment, args.directory, args.out/'learned.tsv', args.view=='overview', True)
    attempts = accepted = 0; reasons = Counter(); best = None; best_visual = native.initial['visual']
    try:
        zero = native.request(wire(decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)), 'MEASURE'))
        assert zero['legal'] and zero['hard'] == zero['individualHard'] == zero['spacing'] == 0
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        vocabulary, support = coordinated_cycles(decoder, nodes, old_words, args.seed, 4096)
        scores, feature_hashes, error = rank(model, decoder, nodes, vocabulary, args.view=='overview')
        order, noise, policy = sample_order(scores, args.seed+1000, 1.)
        np.savez_compressed(args.out/'observations.npz', node_features=nodes, cycles=vocabulary, scores=scores, gumbel=noise)
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:1024]:
                if time.monotonic()-started >= 20:
                    break
                ci, fi = map(int, np.unravel_index(index, scores.shape)); cycle = vocabulary[ci]; fraction = float(FRACTIONS[fi])
                action = fractional_action(decoder, cycle, fraction); command = wire(action); result = native.request(command)
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'fractionIndex': fi,
                    'cycle': cycle.tolist(), 'fraction': fraction, 'score': float(scores[ci, fi]),
                    'gumbel': float(noise[ci, fi]), 'wireSha256': hashlib.sha256(command.encode()).hexdigest(), 'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1
                if result['accepted']:
                    accepted += 1; best = action.copy(); best_visual = result['visual']
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory, args.out, decoder, best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['visual'] == best_visual and stats['policyActionsEvaluated'] == attempts and stats['acceptedActions'] == accepted
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    report = {'kind': 'positive-example-trained-local-cycle-Captain-transfer-v1', 'view': args.view,
        'sourceDirectory': str(args.directory), 'sourceInputs': inputs,
        'checkpoint': str(args.checkpoint), 'checkpointSha256': digest(args.checkpoint), 'checkpointTraining': metadata,
        'environment': str(args.environment), 'environmentSha256': digest(args.environment),
        'previousRoot': str(args.previous), 'exclusionReports': prior, 'exclusionScope': 'all parent measured words, both prior matched temperatures, and source-identical first walk round',
        'seed': args.seed, 'vocabularyTriples': 4096, 'budget': 1024, 'secondsLimit': 20, 'support': support,
        'policy': policy, 'attempts': attempts, 'accepted': accepted, 'reasons': dict(reasons),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'],
        'final': stats, 'wallSeconds': time.monotonic()-started,
        'featureChunkHashes': feature_hashes, 'scaleFeatureMaxError': error, 'codeSha256': hashes(),
        'newTrainingUpdatesDuringInference': 0, 'coordinateRepairs': 0, 'nativeSearchCalls': 0,
        'policyOrderFixedBeforeNativeMeasurement': True, 'savedGeometryMatchesModelOutput': True,
        'observationsSha256': digest(args.out/'observations.npz'), 'actionsSha256': digest(args.out/'actions.jsonl')}
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['view', 'attempts', 'accepted', 'initialVisual', 'reasons', 'wallSeconds']}
        | {'finalVisual': best_visual}), flush=True)


def replay(args):
    report = json.loads((args.out/'report.json').read_text()); directory = Path(report['sourceDirectory'])
    assert report['codeSha256'] == hashes() and digest(report['checkpoint']) == report['checkpointSha256']
    assert digest(report['environment']) == report['environmentSha256']
    assert {name: digest(directory/name) for name in INPUT_FILES} == report['sourceInputs']
    assert digest(args.out/'observations.npz') == report['observationsSha256'] and digest(args.out/'actions.jsonl') == report['actionsSha256']
    model, metadata = load(Path(report['checkpoint'])); assert metadata == report['checkpointTraining']
    old_words, prior = exclusions(metadata, report['view'], Path(report['previousRoot'])); assert prior == report['exclusionReports']
    decoder = WalkDecoder(directory)
    with np.load(args.out/'observations.npz', allow_pickle=False) as saved:
        nodes = saved['node_features'].copy()
        vocabulary, support = coordinated_cycles(decoder, nodes, old_words, report['seed'], 4096)
        assert support == report['support'] and np.array_equal(vocabulary, saved['cycles'])
        scores, feature_hashes, _ = rank(model, decoder, nodes, vocabulary, report['view']=='overview')
        assert feature_hashes == report['featureChunkHashes'] and np.array_equal(scores, saved['scores'])
        order, noise, policy = sample_order(scores, report['seed']+1000, 1.)
        assert policy == report['policy'] and np.array_equal(noise, saved['gumbel'])
    attempts = accepted = 0; best = None; reasons = Counter()
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row = json.loads(line); ci, fi = map(int, np.unravel_index(order[attempts], scores.shape))
        assert row['rank'] == attempts and row['cycleIndex'] == ci and row['fractionIndex'] == fi
        assert row['cycle'] == vocabulary[ci].tolist() and row['fraction'] == FRACTIONS[fi]
        assert row['score'] == scores[ci, fi] and row['gumbel'] == noise[ci, fi]
        proposed = fractional_action(decoder, vocabulary[ci], float(FRACTIONS[fi]))
        assert hashlib.sha256(wire(proposed).encode()).hexdigest() == row['wireSha256']
        attempts += 1; reasons[row['result']['reason']] += 1
        if row['result']['accepted']:
            best = proposed; accepted += 1
    assert attempts == report['attempts'] and accepted == report['accepted'] and dict(reasons) == report['reasons']
    verify_saved(directory, args.out, decoder, best)
    result = {'status': 'pass', 'actionsReplayed': attempts, 'accepted': accepted,
        'allScoresAndGumbelOrderReconstructed': True, 'completeMeasuredWordExclusionReplayed': True,
        'savedGeometryReconstructed': True, 'nativeScoresRecomputed': False}
    (args.out/'replay.json').write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps(result), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('mode', choices=['run', 'replay']); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--directory', type=Path); p.add_argument('--checkpoint', type=Path); p.add_argument('--environment', type=Path)
    p.add_argument('--previous', type=Path); p.add_argument('--view', choices=['individual', 'overview']); p.add_argument('--seed', type=int, default=86533)
    args = p.parse_args(); (run if args.mode=='run' else replay)(args)
