#!/usr/bin/env python3
"""Adapt the full-context critic with new exact active-conflict outcomes.

Validation uses new pairs absent from the parent's complete measured dataset.
Native outcomes are labels; coordinates are neither searched nor trained.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from full_context_pair_model import expand, load, SCHEMA
from learn_pair_policy import KEYS
from run_anchor_pair_policy import digest, pair_features, wire
from run_anchor_pair_walk import WalkDecoder
from run_full_context_pairs import rank, vocabulary
from train_anchor_pair_outcomes import outcome_target, regression


def collect(parent_directory, stages):
    parent_report = json.loads((parent_directory/'training.json').read_text())
    assert digest(parent_directory/'dataset.npz') == parent_report['datasetSha256']
    assert digest(parent_directory/'model.npz') == parent_report['checkpointSha256']
    with np.load(parent_directory/'dataset.npz', allow_pickle=False) as saved:
        records = {(str(view), int(pair[0]), int(pair[1])):
            {'x': x.copy(), 'y': float(y), 'active': bool(active), 'gain': float(gain), 'novel': False}
            for view, pair, x, y, active, gain in zip(saved['view'], saved['pairs'], saved['x'],
                                                   saved['y'], saved['active'], saved['actual_gain'])}
    model, _ = load(parent_directory/'model.npz')
    inputs = {}; sources = []; duplicate = 0
    for stage in stages:
        report = json.loads((stage/'report.json').read_text())
        assert report['kind'] == 'full-context-pressure-pair-policy-v1'
        assert report['checkpointSha256'] == digest(parent_directory/'model.npz')
        assert digest(stage/'actions.jsonl') == report['actionsSha256']
        assert digest(stage/'observations.npz') == report['observationsSha256']
        directory = Path(report['sourceDirectory']); decoder = WalkDecoder(directory)
        for name, expected in report['sourceInputs'].items():
            assert digest(directory/name) == expected
        if report['view'] in inputs:
            assert inputs[report['view']] == report['sourceInputs']
        inputs[report['view']] = report['sourceInputs']
        with np.load(stage/'observations.npz', allow_pickle=False) as saved:
            nodes = saved['node_features'].copy(); pairs, support = vocabulary(decoder, nodes)
            assert support == report['support'] and np.array_equal(pairs, saved['pairs'])
            scores, hashes, _ = rank(model, nodes, decoder, pairs, report['view'] == 'overview')
            assert np.array_equal(scores, saved['scores']) and hashes == report['featureChunkHashes']
        actions = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
        assert len(actions) == report['attempts']
        order = np.argsort(-scores, kind='stable'); selected = pairs[order[:len(actions)]]
        base, _ = pair_features(nodes, decoder, selected, report['view'] == 'overview')
        full = expand(base, nodes, selected)
        for i, (row, pair) in enumerate(zip(actions, selected)):
            a, b = map(int, pair)
            assert row['rank'] == i and (a, b) == (row['source'], row['target'])
            assert row['score'] == scores[order[i]]
            assert hashlib.sha256(wire(decoder.action(a, b)).encode()).hexdigest() == row['wireSha256']
            result = row['result']; key = (report['view'], a, b)
            example = {'x': full[i].copy(), 'y': outcome_target(result, report['initialVisual']),
                'active': True, 'gain': float(report['initialVisual']-result['visual']) if result['legal'] else np.nan,
                'novel': key not in records}
            if key in records:
                original = records[key]
                assert np.array_equal(original['x'], example['x']) and original['y'] == example['y']
                assert original['active'] and (original['gain'] == example['gain'] or
                                               np.isnan(original['gain']) and np.isnan(example['gain']))
                duplicate += 1
            else:
                records[key] = example
        sources.append({'stage': str(stage), 'reportSha256': digest(stage/'report.json'),
            'observationsSha256': report['observationsSha256'], 'actionsSha256': report['actionsSha256']})
    for source in parent_report['sourceStages']:
        path = Path(source['stage'])/'report.json'
        assert digest(path) == source['reportSha256']
        old_report = json.loads(path.read_text())
        assert old_report['sourceInputs'] == inputs[old_report['view']]
    keys = sorted(records); rows = [records[key] for key in keys]
    data = {'x': np.array([row['x'] for row in rows]), 'y': np.array([row['y'] for row in rows]),
        'active': np.array([row['active'] for row in rows]), 'actual_gain': np.array([row['gain'] for row in rows]),
        'novel_to_parent': np.array([row['novel'] for row in rows]),
        'view': np.array([key[0] for key in keys]), 'pairs': np.array([key[1:] for key in keys], dtype=np.int32)}
    assert len(data['x']) <= 8192 and data['novel_to_parent'].sum() >= 32
    return data, sources, inputs, duplicate


def split(data, seed):
    rng = np.random.default_rng(seed); validation = []
    legal = np.isfinite(data['actual_gain'])
    for view in sorted(set(data['view'])):
        for value in [False, True]:
            ids = np.flatnonzero((data['view'] == view) & (legal == value) & data['novel_to_parent'])
            assert len(ids) >= 4
            validation.extend(rng.permutation(ids)[:max(1, int(.25*len(ids)))])
    validation = np.array(sorted(validation)); training = np.setdiff1d(np.arange(len(legal)), validation)
    sampling = np.r_[training, np.tile(training[data['active'][training]], 3)]
    assert data['novel_to_parent'][validation].all() and data['active'][validation].all()
    return training, validation, sampling, rng


def train(args):
    assert 0 < args.seconds <= 20 and 1 <= args.epochs <= 100
    args.out.mkdir(parents=True, exist_ok=False)
    data, sources, inputs, duplicate = collect(args.parent, args.stages)
    training, validation, sampling, rng = split(data, args.seed)
    data |= {'training': training, 'validation': validation, 'sampling': sampling}
    np.savez_compressed(args.out/'dataset.npz', **data)
    model, _ = load(args.parent/'model.npz'); x = data['x']; y = data['y']
    initial = {key: value.copy() for key, value in model.p.items()}
    first = {key: np.zeros_like(value) for key, value in model.p.items()}
    second = {key: value.copy() for key, value in first.items()}
    initial_loss = regression(model, x[validation], y[validation]); best_loss = initial_loss
    best = initial; selected_epoch = selected_updates = updates = 0; history = []; started = time.monotonic()
    for epoch in range(args.epochs):
        order = rng.permutation(sampling)
        for offset in range(0, len(order), 128):
            batch = order[offset:offset+128]; _, gradients = regression(model, x[batch], y[batch], True)
            norm = np.sqrt(sum(np.sum(g*g) for g in gradients.values())); updates += 1
            for key in KEYS:
                gradient = gradients[key]*min(1, 5/max(norm, 1e-12))
                first[key] = .9*first[key]+.1*gradient
                second[key] = .999*second[key]+.001*gradient*gradient
                model.p[key] -= .001*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started >= args.seconds:
                break
        loss = regression(model, x[validation], y[validation])
        history.append({'epoch': epoch+1, 'updates': updates, 'validationHuber': loss})
        if loss < best_loss:
            best_loss = loss; best = {key: value.copy() for key, value in model.p.items()}
            selected_epoch = epoch+1; selected_updates = updates
        if time.monotonic()-started >= args.seconds:
            break
    assert selected_updates > 0
    metadata = {'schema': SCHEMA, 'kind': 'full-context-active-outcome-adaptation-v1',
        'features': 136, 'parameterCount': sum(value.size for value in best.values()),
        'parent': str(args.parent/'model.npz'), 'parentSha256': digest(args.parent/'model.npz'),
        'parentTrainingReportSha256': digest(args.parent/'training.json'),
        'parentDatasetSha256': digest(args.parent/'dataset.npz'), 'parentDirectory': str(args.parent),
        'sourceStages': sources, 'sourceInputsByView': inputs, 'duplicatesRemoved': duplicate,
        'datasetPath': str(args.out/'dataset.npz'), 'datasetSha256': digest(args.out/'dataset.npz'),
        'seed': args.seed, 'rows': len(x), 'activeRows': int(data['active'].sum()),
        'newUniqueMeasuredPairs': int(data['novel_to_parent'].sum()), 'validationRows': len(validation),
        'actualImprovementExamples': int(np.sum(data['actual_gain'] > 0)),
        'newTrainingUpdatesExecuted': updates, 'selectedCheckpointUpdates': selected_updates,
        'selectedEpoch': selected_epoch, 'initialValidationHuber': initial_loss, 'bestValidationHuber': best_loss,
        'validationScope': 'new measured pairs absent from all parent measured rows, held out from adaptation; same two layouts',
        'activeSamplingMultiplicity': 4, 'normalizationChanged': False,
        'absoluteCoordinatesAsFeatures': False, 'modelNamesAsFeatures': False,
        'seconds': time.monotonic()-started,
        'codeSha256': {name: digest(Path(__file__).parent/name) for name in ['train_active_pair_outcomes.py',
            'full_context_pair_model.py', 'run_full_context_pairs.py', 'train_anchor_pair_outcomes.py', 'learn_pair_policy.py']}}
    model.p = best
    np.savez_compressed(args.out/'model.npz', **best, **{'initial__'+key: value for key, value in initial.items()},
        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out/'model.npz')
    assert np.array_equal(restored.forward(x)[0], model.forward(x)[0])
    report = metadata | {'checkpointSha256': digest(args.out/'model.npz'), 'history': history}
    (args.out/'training.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['rows', 'activeRows', 'newUniqueMeasuredPairs', 'validationRows',
        'duplicatesRemoved', 'newTrainingUpdatesExecuted', 'selectedCheckpointUpdates', 'initialValidationHuber',
        'bestValidationHuber', 'actualImprovementExamples', 'seconds', 'checkpointSha256']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--stages', type=Path, nargs='+', required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=84019); p.add_argument('--epochs', type=int, default=60)
    p.add_argument('--seconds', type=float, default=10); train(p.parse_args())
