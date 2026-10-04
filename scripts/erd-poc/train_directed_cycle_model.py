#!/usr/bin/env python3
"""Learn direction-sensitive cycle outcomes, holding out whole triples."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import CycleCritic, cycle_features, load, SCHEMA, KEYS
from full_context_pair_model import load as load_parent
from run_anchor_pair_walk import WalkDecoder
from run_anchor_pair_policy import digest, wire
from run_neural_cycle_policy import cycles, rank_cycles, cycle_action
from train_anchor_pair_outcomes import outcome_target


def collect(parent, stages):
    model, _ = load_parent(parent); records = {}; inputs = {}; sources = []
    for stage in stages:
        report = json.loads((stage/'report.json').read_text())
        assert report['kind'] == 'frozen-neural-factor-three-cycle-policy-v1'
        assert digest(parent) == report['checkpointSha256']
        assert digest(stage/'actions.jsonl') == report['actionsSha256']
        assert digest(stage/'observations.npz') == report['observationsSha256']
        directory = Path(report['sourceDirectory']); decoder = WalkDecoder(directory)
        assert all(digest(directory/name) == expected for name, expected in report['sourceInputs'].items())
        inputs[report['view']] = report['sourceInputs']
        with np.load(stage/'observations.npz', allow_pickle=False) as saved:
            nodes = saved['node_features'].copy()
            vocabulary, support = cycles(decoder, nodes, report['seed'], report['triples'])
            assert support == report['support'] and np.array_equal(vocabulary, saved['cycles'])
            scores, hashes, _ = rank_cycles(model, decoder, nodes, vocabulary, report['view'] == 'overview')
            assert np.array_equal(scores, saved['scores']) and hashes == report['featureChunkHashes']
        rows = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
        assert len(rows) == report['attempts']
        order = np.argsort(-scores, kind='stable'); selected = vocabulary[order[:len(rows)]]
        features, _ = cycle_features(nodes, decoder, selected, report['view'] == 'overview')
        active = np.any(nodes[:, 4:6] > 0, axis=1)
        for index, (row, cycle) in enumerate(zip(rows, selected)):
            assert row['rank'] == index and row['cycle'] == cycle.tolist() and row['score'] == scores[order[index]]
            assert hashlib.sha256(wire(cycle_action(decoder, cycle)).encode()).hexdigest() == row['wireSha256']
            key = (report['view'], *map(int, cycle)); assert key not in records
            result = row['result']
            records[key] = {'x': features[index].copy(), 'y': outcome_target(result, report['initialVisual']),
                'active': int(active[cycle].sum()), 'gain': float(report['initialVisual']-result['visual']) if result['legal'] else np.nan}
        sources.append({'stage': str(stage), 'reportSha256': digest(stage/'report.json'),
            'observationsSha256': report['observationsSha256'], 'actionsSha256': report['actionsSha256']})
    keys = sorted(records); rows = [records[key] for key in keys]
    data = {'x': np.array([row['x'] for row in rows]), 'y': np.array([row['y'] for row in rows]),
        'active_count': np.array([row['active'] for row in rows]), 'actual_gain': np.array([row['gain'] for row in rows]),
        'view': np.array([key[0] for key in keys]), 'cycles': np.array([key[1:] for key in keys], dtype=np.int32)}
    assert 32 <= len(rows) <= 4096
    return data, sources, inputs


def split(data, seed):
    groups = {}
    for i, (view, cycle) in enumerate(zip(data['view'], data['cycles'])):
        groups.setdefault((str(view), *sorted(map(int, cycle))), []).append(i)
    assert all(len(ids) == 2 for ids in groups.values())
    rng = np.random.default_rng(seed); validation = []
    for view in sorted(set(data['view'])):
        for multi in [False, True]:
            keys = [key for key, ids in groups.items() if key[0] == view and bool(data['active_count'][ids[0]] >= 2) == multi]
            assert len(keys) >= 4
            for index in rng.permutation(len(keys))[:max(1, int(.25*len(keys)))]:
                validation.extend(groups[keys[index]])
    validation = np.array(sorted(validation)); training = np.setdiff1d(np.arange(len(data['x'])), validation)
    sampling = np.r_[training, np.tile(training[data['active_count'][training] >= 2], 3)]
    validation_keys = {(str(data['view'][i]), *sorted(map(int, data['cycles'][i]))) for i in validation}
    assert not any((str(data['view'][i]), *sorted(map(int, data['cycles'][i]))) in validation_keys for i in training)
    return training, validation, sampling, rng


def train(args):
    assert 0 < args.seconds <= 20 and 1 <= args.epochs <= 100
    args.out.mkdir(parents=True, exist_ok=False)
    data, sources, inputs = collect(args.parent, args.stages)
    training, validation, sampling, rng = split(data, args.seed)
    data |= {'training': training, 'validation': validation, 'sampling': sampling}
    np.savez_compressed(args.out/'dataset.npz', **data)
    parent, _ = load_parent(args.parent); model = CycleCritic(parent, args.seed)
    x = data['x']; y = data['y']; initial = {key: value.copy() for key, value in model.p.items()}
    warm_error = float(np.max(abs(model.forward(x)[0]-parent.forward(x.reshape(-1, 136))[0].reshape(-1, 3).mean(1))))
    assert warm_error < 1e-10
    first = {key: np.zeros_like(value) for key, value in model.p.items()}
    second = {key: value.copy() for key, value in first.items()}
    initial_loss = model.loss(x[validation], y[validation]); best_loss = initial_loss
    best = initial; updates = selected_updates = selected_epoch = 0; history = []; started = time.monotonic()
    for epoch in range(args.epochs):
        order = rng.permutation(sampling)
        for offset in range(0, len(order), 64):
            batch = order[offset:offset+64]; _, gradients = model.loss(x[batch], y[batch], True)
            norm = np.sqrt(sum(np.sum(value*value) for value in gradients.values())); updates += 1
            for key in KEYS:
                gradient = gradients[key]*min(1, 5/max(norm, 1e-12))
                first[key] = .9*first[key]+.1*gradient; second[key] = .999*second[key]+.001*gradient*gradient
                model.p[key] -= .001*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started >= args.seconds:
                break
        loss = model.loss(x[validation], y[validation])
        history.append({'epoch': epoch+1, 'updates': updates, 'validationHuber': loss})
        if loss < best_loss:
            best_loss = loss; best = {key: value.copy() for key, value in model.p.items()}
            selected_updates = updates; selected_epoch = epoch+1
        if time.monotonic()-started >= args.seconds:
            break
    assert selected_updates > 0 and np.linalg.norm(best['oh']) > 0
    metadata = {'schema': SCHEMA, 'kind': 'directed-cycle-outcome-critic-v1', 'parent': str(args.parent),
        'parentSha256': digest(args.parent), 'sourceStages': sources, 'sourceInputsByView': inputs,
        'rows': len(x), 'multiConflictRows': int(np.sum(data['active_count'] >= 2)),
        'validationRows': len(validation), 'validationMultiConflictRows': int(np.sum(data['active_count'][validation] >= 2)),
        'validationScope': 'whole unordered triples including both directions held out; same source layouts, not unseen graphs',
        'cycleOutcomesUsedForTraining': True, 'absoluteCoordinatesAsFeatures': False, 'modelNamesAsFeatures': False,
        'orderedDirectedPairFeatures': True, 'cyclicRotationInvariantPooling': True,
        'nonlinearMeanSecondMomentInteractionHead': True, 'normalizationChanged': False, 'multiConflictSamplingMultiplicity': 4,
        'actualImprovementExamples': int(np.sum(data['actual_gain'] > 0)), 'seed': args.seed,
        'parameterCount': sum(value.size for value in best.values()),
        'newTrainingUpdatesExecuted': updates, 'selectedCheckpointUpdates': selected_updates, 'selectedEpoch': selected_epoch,
        'initialValidationHuber': initial_loss, 'bestValidationHuber': best_loss, 'warmPredictionMaxDifference': warm_error,
        'datasetPath': str(args.out/'dataset.npz'), 'datasetSha256': digest(args.out/'dataset.npz'),
        'seconds': time.monotonic()-started,
        'codeSha256': {name: digest(Path(__file__).parent/name) for name in ['directed_cycle_model.py',
            'train_directed_cycle_model.py', 'run_neural_cycle_policy.py', 'run_anchor_pair_policy.py',
            'run_anchor_pair_walk.py', 'full_context_pair_model.py']}}
    model.p = best
    np.savez_compressed(args.out/'model.npz', **best, **{'initial__'+key: value for key, value in initial.items()},
        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out/'model.npz'); assert np.array_equal(restored.forward(x)[0], model.forward(x)[0])
    report = metadata | {'checkpointSha256': digest(args.out/'model.npz'), 'history': history}
    (args.out/'training.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['rows', 'multiConflictRows', 'validationRows', 'validationMultiConflictRows',
        'parameterCount', 'newTrainingUpdatesExecuted', 'selectedCheckpointUpdates', 'initialValidationHuber',
        'bestValidationHuber', 'actualImprovementExamples', 'warmPredictionMaxDifference', 'seconds', 'checkpointSha256']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--stages', type=Path, nargs='+', required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=84023); p.add_argument('--epochs', type=int, default=80)
    p.add_argument('--seconds', type=float, default=10); train(p.parse_args())
