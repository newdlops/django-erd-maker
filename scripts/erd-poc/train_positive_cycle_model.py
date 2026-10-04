#!/usr/bin/env python3
"""Learn actual improving neural cycles; hold out complete synthetic graphs."""
import argparse
import gc
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import KEYS
from fractional_cycle_model import load, action_features, SCHEMA
from run_anchor_pair_policy import digest, wire
from run_anchor_pair_walk import WalkDecoder
from run_fractional_cycle_transfer import fractional_action
from train_anchor_pair_outcomes import outcome_target
from train_fractional_cycle_model import chunk_predictions
from synthetic_cycle_curriculum import code_hashes as collection_hashes


def hashes():
    return collection_hashes() | {'train_positive_cycle_model.py': digest(__file__)}


def collect(parent_directory, root):
    parent_report = json.loads((parent_directory/'training.json').read_text())
    assert digest(parent_directory/'model.npz') == parent_report['checkpointSha256']
    assert digest(parent_directory/'dataset.npz') == parent_report['datasetSha256']
    original_count = parent_report['rows']; synthetic_count = 32*2*64
    x = np.empty((original_count+synthetic_count, 3, 141)); y = np.empty(len(x)); gain = np.empty(len(x))
    graph = np.full(len(x), -1, dtype=np.int32); view = np.empty(len(x), dtype='<U10')
    cycles = np.empty((len(x), 3), dtype=np.int32); fractions = np.empty(len(x))
    with np.load(parent_directory/'dataset.npz', allow_pickle=False) as old:
        for name, destination in [('x', x), ('y', y), ('actual_gain', gain), ('view', view),
            ('cycles', cycles), ('fractions', fractions)]:
            destination[:original_count] = old[name]
        original_validation = old['validation'].copy(); original_training = old['training'].copy()
    stages = []; offset = original_count
    for graph_id in range(32):
        for view_name, stage_name in [('individual', 'individual-flat'), ('overview', 'overview')]:
            stage = root/f'graph-{graph_id:02}'/stage_name
            report = json.loads((stage/'report.json').read_text())
            assert report['graph'] == graph_id and report['view'] == view_name and report['attempts'] == 64
            assert report['initialHard'] == report['initialIndividualHard'] == 0
            assert report['checkpointSha256'] == parent_report['checkpointSha256']
            assert digest(stage/'observations.npz') == report['observationsSha256']
            assert digest(stage/'actions.jsonl') == report['actionsSha256'] and digest(stage/'labels.npz') == report['labelsSha256']
            source = Path(report['sourceDirectory']); decoder = WalkDecoder(source)
            assert all(digest(source/name) == value for name, value in report['sourceInputs'].items())
            if view_name == 'individual':
                for name in ['nodes.tsv', 'edges.tsv', 'positions.tsv', 'routes.tsv']:
                    assert digest(source/name) == digest(source/('individual.'+name))
            with np.load(stage/'observations.npz', allow_pickle=False) as saved:
                nodes = saved['node_features'].copy()
            rows = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
            measured_cycles = np.array([row['cycle'] for row in rows], dtype=np.int32)
            measured_fractions = np.array([row['fraction'] for row in rows])
            features, _ = action_features(nodes, decoder, measured_cycles, measured_fractions, view_name=='overview')
            targets = np.array([outcome_target(row['result'], report['initialVisual']) for row in rows])
            actual = np.array([report['initialVisual']-row['result']['visual'] if row['result']['legal'] else np.nan for row in rows])
            with np.load(stage/'labels.npz', allow_pickle=False) as saved:
                assert np.array_equal(features, saved['x']) and np.array_equal(targets, saved['y'])
                assert np.array_equal(actual, saved['actual_gain'], equal_nan=True)
                assert np.array_equal(measured_cycles, saved['cycles']) and np.array_equal(measured_fractions, saved['fractions'])
            for row, cycle, fraction in zip(rows, measured_cycles, measured_fractions):
                import hashlib
                assert hashlib.sha256(wire(fractional_action(decoder, cycle, fraction)).encode()).hexdigest() == row['wireSha256']
            end = offset+64
            x[offset:end] = features; y[offset:end] = targets; gain[offset:end] = actual
            graph[offset:end] = graph_id; view[offset:end] = view_name
            cycles[offset:end] = measured_cycles; fractions[offset:end] = measured_fractions
            stages.append({'stage': str(stage), 'reportSha256': digest(stage/'report.json'), 'graph': graph_id,
                'view': view_name, 'labelsSha256': report['labelsSha256'], 'sourceInputs': report['sourceInputs']})
            offset = end
    assert offset == len(x) and np.isfinite(x).all()
    return {'x': x, 'y': y, 'actual_gain': gain, 'graph': graph, 'view': view, 'cycles': cycles,
        'fractions': fractions, 'original_training': original_training, 'original_validation': original_validation}, stages


def split(data, seed):
    rng = np.random.default_rng(seed); validation_graphs = np.sort(rng.permutation(32)[:8])
    synthetic_validation = np.flatnonzero(np.isin(data['graph'], validation_graphs))
    synthetic_training = np.flatnonzero((data['graph']>=0) & ~np.isin(data['graph'], validation_graphs))
    training = np.r_[data['original_training'], synthetic_training]
    validation = np.r_[data['original_validation'], synthetic_validation]
    positives = training[data['actual_gain'][training]>0]
    sampling = np.r_[training, np.tile(positives, 7)]
    assert not np.intersect1d(training, validation).size
    assert set(data['graph'][synthetic_training]) & set(validation_graphs) == set()
    return training, synthetic_validation, validation_graphs, sampling, rng


def validation_loss(model, data, synthetic_validation):
    old = data['original_validation']
    captain = model.loss(data['x'][old], data['y'][old])
    synthetic = model.loss(data['x'][synthetic_validation], data['y'][synthetic_validation])
    return .5*(captain+synthetic), float(captain), float(synthetic)


def train(args):
    args.out.mkdir(parents=True, exist_ok=False)
    data, stages = collect(args.parent, args.source)
    training, validation, graphs, sampling, rng = split(data, args.seed)
    data |= {'training': training, 'synthetic_validation': validation, 'validation_graphs': graphs, 'sampling': sampling}
    np.savez_compressed(args.out/'dataset.npz', **data)
    model, parent_metadata = load(args.parent/'model.npz')
    initial = {key: value.copy() for key, value in model.p.items()}
    first = {key: np.zeros_like(value) for key, value in initial.items()}; second = {key: value.copy() for key, value in first.items()}
    initial_loss, initial_captain, initial_synthetic = validation_loss(model, data, validation)
    best_loss = initial_loss; best = initial; history = []; updates = selected_updates = selected_epoch = 0
    started = time.monotonic()
    for epoch in range(64):
        order = rng.permutation(sampling)
        for offset in range(0, len(order), 64):
            ids = order[offset:offset+64]; _, gradients = model.loss(data['x'][ids], data['y'][ids], True)
            norm = np.sqrt(sum(np.sum(value*value) for value in gradients.values())); updates += 1
            for key in KEYS:
                gradient = gradients[key]*min(1., 5/max(norm, 1e-12))
                first[key] = .9*first[key]+.1*gradient; second[key] = .999*second[key]+.001*gradient*gradient
                model.p[key] -= .0005*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started >= 10:
                break
        loss, captain, synthetic = validation_loss(model, data, validation)
        history.append({'epoch': epoch+1, 'updates': updates, 'validationLoss': loss,
            'captainValidationHuber': captain, 'syntheticGraphValidationHuber': synthetic})
        if loss < best_loss:
            best_loss = loss; best = {key: value.copy() for key, value in model.p.items()}
            selected_updates = updates; selected_epoch = epoch+1
        if time.monotonic()-started >= 10:
            break
    assert selected_updates > 0
    model.p = best
    changes = {key: float(np.linalg.norm(best[key]-initial[key])) for key in KEYS}; assert all(v>0 for v in changes.values())
    metadata = parent_metadata | {'schema': SCHEMA, 'kind': 'positive-synthetic-graph-cycle-critic-v1',
        'parent': str(args.parent/'model.npz'), 'parentSha256': digest(args.parent/'model.npz'),
        'parentTrainingReport': str(args.parent/'training.json'), 'parentTrainingReportSha256': digest(args.parent/'training.json'),
        'parentDataset': str(args.parent/'dataset.npz'), 'parentDatasetSha256': digest(args.parent/'dataset.npz'),
        'trainingDatasetPath': str(args.out/'dataset.npz'), 'trainingDatasetSha256': digest(args.out/'dataset.npz'),
        'datasetRole': 'inherited Captain measured-word exclusion; mixed training dataset is trainingDatasetPath',
        'sourceStages': stages, 'rows': len(data['x']), 'syntheticRows': int((data['graph']>=0).sum()),
        'trainingRows': len(training), 'actualImprovementExamples': int((data['actual_gain'][training]>0).sum()),
        'syntheticValidationRows': len(validation), 'syntheticValidationGraphs': graphs.tolist(),
        'captainValidationRows': len(data['original_validation']), 'positiveTrainingSamplingMultiplicity': 8,
        'syntheticValidationScope': 'complete graphs, both views and all actions absent from synthetic training',
        'captainValidationScope': 'original whole-triple held-out actions retained, not unseen Captain graphs',
        'originalNormalizationUnchanged': True, 'newTrainingUpdatesExecuted': updates,
        'selectedCheckpointUpdates': selected_updates, 'selectedEpoch': selected_epoch, 'seed': args.seed,
        'initialValidationLoss': initial_loss, 'bestValidationLoss': best_loss,
        'initialCaptainValidationHuber': initial_captain, 'initialSyntheticGraphValidationHuber': initial_synthetic,
        'parameterGroupChangesL2': changes, 'codeSha256': hashes(), 'seconds': time.monotonic()-started}
    np.savez_compressed(args.out/'model.npz', **best, **{'initial__'+key: value for key, value in initial.items()},
        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out/'model.npz')
    assert np.array_equal(chunk_predictions(restored, data['x']), chunk_predictions(model, data['x']))
    report = metadata | {'checkpointSha256': digest(args.out/'model.npz'), 'history': history}
    (args.out/'training.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['rows', 'syntheticRows', 'trainingRows', 'actualImprovementExamples',
        'syntheticValidationRows', 'syntheticValidationGraphs', 'newTrainingUpdatesExecuted', 'selectedCheckpointUpdates',
        'initialValidationLoss', 'bestValidationLoss', 'checkpointSha256', 'seconds']}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=86351); train(parser.parse_args())
