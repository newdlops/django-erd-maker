#!/usr/bin/env python3
"""Learn measured amplitudes with validation absent from the complete parent."""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import KEYS, load as load_parent, cycle_features
from fractional_cycle_model import FractionCritic, SCHEMA, load, action_features
from run_anchor_pair_walk import WalkDecoder
from run_anchor_pair_policy import digest, wire
from run_directed_cycle_policy import rank, coordinated_cycles, measured, code_hashes as source_code_hashes
from run_fractional_cycle_transfer import fractional_action, code_hashes as fractional_code_hashes
from train_anchor_pair_outcomes import outcome_target


def collect(parent_directory, source_root):
    parent, metadata = load_parent(parent_directory/'model.npz')
    parent_report = json.loads((parent_directory/'training.json').read_text())
    assert digest(parent_directory/'model.npz') == parent_report['checkpointSha256']
    assert digest(parent_directory/'dataset.npz') == metadata['datasetSha256']
    with np.load(parent_directory/'dataset.npz', allow_pickle=False) as saved:
        parent_data = {name: saved[name].copy() for name in ['x', 'y', 'active_count', 'actual_gain', 'view', 'cycles']}
    old_keys = {(str(view), *map(int, cycle)) for view, cycle in zip(parent_data['view'], parent_data['cycles'])}
    rows = {}; sources = []; inputs = {}
    for view in ['individual', 'overview']:
        unit_stage = source_root/(view+'-directed1')
        report = json.loads((unit_stage/'report.json').read_text())
        assert report['kind'] == 'trained-directed-coordinated-cycle-policy-v1'
        assert report['checkpointSha256'] == digest(parent_directory/'model.npz')
        assert report['checkpointTraining'] == metadata and report['codeSha256'] == source_code_hashes()
        directory = Path(report['sourceDirectory']); decoder = WalkDecoder(directory)
        assert all(digest(directory/name) == expected for name, expected in report['sourceInputs'].items())
        assert report['sourceInputs'] == metadata['sourceInputsByView'][view]
        inputs[view] = report['sourceInputs']
        assert digest(unit_stage/'observations.npz') == report['observationsSha256']
        assert digest(unit_stage/'actions.jsonl') == report['actionsSha256']
        with np.load(unit_stage/'observations.npz', allow_pickle=False) as saved:
            nodes = saved['node_features'].copy()
            vocabulary, support = coordinated_cycles(decoder, nodes, measured(metadata, view), report['seed'], report['triples'])
            assert support == report['support'] and np.array_equal(vocabulary, saved['cycles'])
            scores, hashes, _ = rank(parent, decoder, nodes, vocabulary, view == 'overview')
            assert np.array_equal(scores, saved['scores']) and hashes == report['featureChunkHashes']
        active = np.any(nodes[:, 4:6] > 0, axis=1)
        old_ids = np.flatnonzero(parent_data['view'] == view)
        for offset in range(0, len(old_ids), 256):
            ids = old_ids[offset:offset+256]; cycles = parent_data['cycles'][ids]
            core, _ = cycle_features(nodes, decoder, cycles, view == 'overview')
            assert np.array_equal(core, parent_data['x'][ids])
            expanded, _ = action_features(nodes, decoder, cycles, np.ones(len(ids)), view == 'overview')
            for i, feature in zip(ids, expanded):
                key = (view, *map(int, parent_data['cycles'][i]), 1.)
                rows[key] = (feature.copy(), float(parent_data['y'][i]), int(parent_data['active_count'][i]),
                    float(parent_data['actual_gain'][i]), False)
        order = np.argsort(-scores, kind='stable')
        unit_rows = [json.loads(line) for line in (unit_stage/'actions.jsonl').read_text().splitlines()]
        assert len(unit_rows) == report['attempts']
        for i, row in enumerate(unit_rows):
            cycle = vocabulary[order[i]]
            assert row['rank'] == i and row['cycle'] == cycle.tolist() and row['score'] == scores[order[i]]
            assert (view, *map(int, cycle)) not in old_keys
        for stage, fraction, stage_rows in [(unit_stage, 1., unit_rows)]+[
            (source_root/(view+'-fraction'+suffix), value, None)
            for suffix, value in [('0001', .001), ('0005', .005), ('0020', .02), ('0100', .1)]]:
            stage_report = json.loads((stage/'report.json').read_text())
            if fraction != 1:
                assert stage_report['kind'] == 'frozen-neural-cycle-fractional-decoder-transfer-v1'
                assert stage_report['fraction'] == fraction and stage_report['codeSha256'] == fractional_code_hashes()
                assert digest(unit_stage/'report.json') == stage_report['sourceReportSha256']
                stage_rows = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
            assert digest(stage/'actions.jsonl') == stage_report['actionsSha256']
            assert len(stage_rows) == stage_report['attempts']
            for offset in range(0, len(stage_rows), 256):
                batch_rows = stage_rows[offset:offset+256]
                cycles = np.array([row['cycle'] for row in batch_rows], dtype=np.int32)
                features, _ = action_features(nodes, decoder, cycles, np.full(len(cycles), fraction), view == 'overview')
                for i, (row, cycle, feature) in enumerate(zip(batch_rows, cycles, features), offset):
                    assert row['rank'] == i and row['cycle'] == unit_rows[i]['cycle']
                    if fraction != 1:
                        assert row['neuralScore'] == unit_rows[i]['score'] and row['unitFractionResult'] == unit_rows[i]['result']
                    assert hashlib.sha256(wire(fractional_action(decoder, cycle, fraction)).encode()).hexdigest() == row['wireSha256']
                    key = (view, *map(int, cycle), fraction); assert key not in rows
                    result = row['result']; gain = float(report['initialVisual']-result['visual']) if result['legal'] else np.nan
                    rows[key] = (feature.copy(), outcome_target(result, report['initialVisual']), int(active[cycle].sum()), gain, True)
            sources.append({'stage': str(stage), 'reportSha256': digest(stage/'report.json'),
                'actionsSha256': stage_report['actionsSha256'], 'fraction': fraction})
        del decoder, nodes, vocabulary, scores, features, expanded, core
        gc.collect()
    keys = sorted(rows); values = [rows[key] for key in keys]
    data = {'x': np.array([row[0] for row in values]), 'y': np.array([row[1] for row in values]),
        'active_count': np.array([row[2] for row in values]), 'actual_gain': np.array([row[3] for row in values]),
        'novel_to_parent': np.array([row[4] for row in values]), 'view': np.array([key[0] for key in keys]),
        'cycles': np.array([key[1:4] for key in keys], dtype=np.int32), 'fractions': np.array([key[4] for key in keys])}
    assert len(data['x']) == 5120 and data['novel_to_parent'].sum() == 3072
    return data, sources, inputs


def split(data, seed):
    groups = {}
    for i, (view, cycle) in enumerate(zip(data['view'], data['cycles'])):
        groups.setdefault((str(view), *sorted(map(int, cycle))), []).append(i)
    parent_groups = {key for key, ids in groups.items() if not data['novel_to_parent'][ids].all()}
    rng = np.random.default_rng(seed); validation = []
    for view in sorted(set(data['view'])):
        for fractional in [False, True]:
            keys = [key for key, ids in groups.items() if key[0] == view and key not in parent_groups
                and bool((data['fractions'][ids] < 1).any()) == fractional]
            assert len(keys) >= 4
            for selected in rng.permutation(len(keys))[:max(1, int(.2*len(keys)))]:
                validation.extend(groups[keys[selected]])
    validation = np.array(sorted(validation)); training = np.setdiff1d(np.arange(len(data['x'])), validation)
    val_groups = {(str(data['view'][i]), *sorted(map(int, data['cycles'][i]))) for i in validation}
    assert not (val_groups & parent_groups)
    assert all((str(data['view'][i]), *sorted(map(int, data['cycles'][i]))) not in val_groups for i in training)
    sampling = np.r_[training, np.tile(training[data['fractions'][training] < 1], 3)]
    return training, validation, sampling, rng


def code_hashes():
    return {name: digest(Path(__file__).parent/name) for name in ['fractional_cycle_model.py',
        'train_fractional_cycle_model.py', 'directed_cycle_model.py', 'train_directed_cycle_model.py',
        'run_directed_cycle_policy.py', 'run_fractional_cycle_transfer.py', 'run_anchor_pair_policy.py',
        'run_anchor_pair_walk.py', 'full_context_pair_model.py', 'joint_anchor_ray_policy.py', 'joint_neural_ports.py']}


def chunk_predictions(model, x, chunk=256):
    return np.concatenate([model.forward(x[offset:offset+chunk])[0] for offset in range(0, len(x), chunk)])


def train(args):
    assert 0 < args.seconds <= 20 and 1 <= args.epochs <= 100
    args.out.mkdir(parents=True, exist_ok=False)
    data, sources, inputs = collect(args.parent, args.source)
    training, validation, sampling, rng = split(data, args.seed)
    data |= {'training': training, 'validation': validation, 'sampling': sampling}
    np.savez_compressed(args.out/'dataset.npz', **data)
    parent, parent_metadata = load_parent(args.parent/'model.npz'); model = FractionCritic(parent)
    x = data['x']; y = data['y']; initial = {key: value.copy() for key, value in model.p.items()}
    warm_error = float(np.max(abs(chunk_predictions(model, x)-chunk_predictions(parent, x[:, :, :136]))))
    assert warm_error < 1e-10
    first = {key: np.zeros_like(value) for key, value in model.p.items()}
    second = {key: value.copy() for key, value in first.items()}
    first_loss = model.loss(x[validation], y[validation]); best_loss = first_loss
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
    assert selected_updates > 0 and np.linalg.norm(best['w1'][-5:]) > 0
    metadata = {'schema': SCHEMA, 'kind': 'fraction-conditioned-measured-cycle-critic-v1',
        'parent': str(args.parent/'model.npz'), 'parentSha256': digest(args.parent/'model.npz'),
        'parentDataset': str(args.parent/'dataset.npz'), 'parentDatasetSha256': parent_metadata['datasetSha256'],
        'parentTrainingReport': str(args.parent/'training.json'), 'parentTrainingReportSha256': digest(args.parent/'training.json'),
        'sourceStages': sources, 'sourceInputsByView': inputs, 'rows': len(x),
        'novelMeasuredRows': int(data['novel_to_parent'].sum()), 'fractionalRows': int((data['fractions'] < 1).sum()),
        'multiConflictRows': int((data['active_count'] >= 2).sum()), 'validationRows': len(validation),
        'validationFractionalRows': int((data['fractions'][validation] < 1).sum()),
        'validationScope': 'whole triples including every direction and fraction held out; absent from complete parent measured dataset; same source layouts, not unseen graphs',
        'normalizationOfExistingFeaturesUnchanged': True, 'newFeatures': 'fraction and actual cent-quantized displacement of both directed owners normalized by observed source scales',
        'fractionalSamplingMultiplicity': 4, 'fractionalOutcomesUsedForTraining': True,
        'absoluteCoordinatesAsFeatures': False, 'modelNamesAsFeatures': False,
        'actualImprovementExamples': int(np.sum(data['actual_gain'] > 0)), 'seed': args.seed,
        'parameterCount': sum(value.size for value in best.values()), 'newTrainingUpdatesExecuted': updates,
        'selectedCheckpointUpdates': selected_updates, 'selectedEpoch': selected_epoch,
        'initialValidationHuber': first_loss, 'bestValidationHuber': best_loss, 'warmPredictionMaxDifference': warm_error,
        'datasetPath': str(args.out/'dataset.npz'), 'datasetSha256': digest(args.out/'dataset.npz'),
        'seconds': time.monotonic()-started, 'codeSha256': code_hashes()}
    model.p = best
    np.savez_compressed(args.out/'model.npz', **best, **{'initial__'+key: value for key, value in initial.items()},
        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out/'model.npz')
    assert np.array_equal(chunk_predictions(restored, x), chunk_predictions(model, x))
    report = metadata | {'checkpointSha256': digest(args.out/'model.npz'), 'history': history}
    (args.out/'training.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['rows', 'novelMeasuredRows', 'fractionalRows',
        'validationRows', 'validationFractionalRows', 'parameterCount', 'newTrainingUpdatesExecuted',
        'selectedCheckpointUpdates', 'initialValidationHuber', 'bestValidationHuber', 'actualImprovementExamples',
        'warmPredictionMaxDifference', 'seconds', 'checkpointSha256']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=84047); parser.add_argument('--epochs', type=int, default=80)
    parser.add_argument('--seconds', type=float, default=10); train(parser.parse_args())
