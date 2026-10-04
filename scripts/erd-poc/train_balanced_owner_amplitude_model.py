#!/usr/bin/env python3
"""Learn mixed owner moves while replaying positive and signed outcomes."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import zipfile
import numpy as np
from directed_cycle_model import KEYS
from owner_amplitude_cycle_model import SCHEMA, load
from run_anchor_pair_policy import digest
from synthetic_mixed_size_owner_curriculum import code_hashes as collection_hashes
from train_fractional_cycle_model import chunk_predictions
from train_signed_cycle_model_v2 import read_rows


def hashes():
    return collection_hashes() | {name: digest(Path(__file__).parent/name) for name in
        ['train_balanced_owner_amplitude_model.py', 'train_signed_cycle_model_v2.py']}


def save_dataset(path, data):
    # NumPy's normal NPZ writer can allocate large transient feature buffers.
    # Stream the same NPY payload format through a small compression window.
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as archive:
        for name, value in data.items():
            assert value.flags.c_contiguous and not value.dtype.hasobject
            with archive.open(name+'.npy', 'w', force_zip64=True) as stream:
                np.lib.format.write_array_header_1_0(stream, {'descr': np.lib.format.dtype_to_descr(value.dtype),
                    'fortran_order': False, 'shape': value.shape})
                view = memoryview(value).cast('B')
                for offset in range(0, len(view), 65536): stream.write(view[offset:offset+65536])
    with zipfile.ZipFile(path) as archive:
        for name, expected in data.items():
            with archive.open(name+'.npy') as stream:
                version = np.lib.format.read_magic(stream)
                shape, fortran, dtype = (np.lib.format.read_array_header_1_0 if version == (1, 0)
                    else np.lib.format.read_array_header_2_0)(stream)
                assert shape == expected.shape and dtype == expected.dtype and not fortran
                observed = hashlib.sha256(); size = 0
                while block := stream.read(65536): observed.update(block); size += len(block)
                assert size == expected.nbytes and observed.hexdigest() == hashlib.sha256(memoryview(expected).cast('B')).hexdigest()


def collect(args, metadata):
    assert digest(args.captain) == metadata['datasetSha256']
    old_count = 5120; count = old_count+3*4096
    x = np.empty((count, 3, 141)); y = np.empty(count); gain = np.empty(count)
    graph = np.full(count, -1, dtype=np.int32); kind = np.full(count, 'captain', dtype='<U10')
    view = np.empty(count, dtype='<U10'); cycles = np.empty((count, 3), dtype=np.int32); amplitudes = np.empty((count, 3))
    x[:old_count] = read_rows(args.captain, 'x', np.arange(old_count))
    with np.load(args.captain, allow_pickle=False) as saved:
        for name, destination in [('y', y), ('actual_gain', gain), ('view', view), ('cycles', cycles)]:
            destination[:old_count] = saved[name]
        amplitudes[:old_count] = saved['fractions'][:, None]
        captain_training = saved['training'].copy(); captain_validation = saved['validation'].copy()
    stages = []; offset = old_count
    for category, root, start in [('positive', args.positive_source, 0), ('negative', args.negative_source, 0),
                                  ('mixed', args.mixed_source, 64)]:
        for graph_id in range(start, start+32):
            for view_name in ['individual', 'overview']:
                name = 'individual-flat' if category == 'positive' and view_name == 'individual' else view_name
                stage = root/f'graph-{graph_id:02}'/name; report = json.loads((stage/'report.json').read_text())
                assert report['graph'] == graph_id and report['view'] == view_name and report['attempts'] == 64
                assert report['initialHard'] == report['initialIndividualHard'] == 0
                assert digest(stage/'labels.npz') == report['labelsSha256'] and digest(stage/'actions.jsonl') == report['actionsSha256']
                source = Path(report['sourceDirectory'])
                assert all(digest(source/n) == value for n, value in report['sourceInputs'].items())
                if category == 'mixed':
                    assert report['codeSha256'] == collection_hashes() and report['checkpointSha256'] == digest(args.parent)
                with np.load(stage/'labels.npz', allow_pickle=False) as saved:
                    assert saved['x'].shape == (64, 3, 141)
                    for key, destination in [('x', x), ('y', y), ('actual_gain', gain), ('cycles', cycles)]:
                        destination[offset:offset+64] = saved[key]
                    a = saved['amplitudes'] if category == 'mixed' else np.repeat(saved['fractions'][:, None], 3, axis=1)
                    assert np.array_equal(saved['x'][:, :, 136], a)
                    amplitudes[offset:offset+64] = a
                graph[offset:offset+64] = graph_id; kind[offset:offset+64] = category; view[offset:offset+64] = view_name; offset += 64
                stages.append({'path': str(stage/'report.json'), 'sha256': digest(stage/'report.json'), 'kind': category,
                    'graph': graph_id, 'view': view_name, 'labelsSha256': report['labelsSha256']})
    assert offset == count and np.isfinite(x).all() and np.isfinite(y).all()
    uniform_graphs = np.array(metadata['validationGraphs'], dtype=np.int32); mixed_graphs = uniform_graphs+64
    validation_graphs = np.r_[uniform_graphs, mixed_graphs]
    parts = {category+'_validation': np.flatnonzero((kind == category) & np.isin(graph, validation_graphs))
             for category in ['positive', 'negative', 'mixed']}
    training = np.r_[captain_training, np.flatnonzero((graph >= 0) & ~np.isin(graph, validation_graphs))]
    assert not np.intersect1d(training, np.r_[captain_validation, *parts.values()]).size
    assert not np.isin(graph[training], validation_graphs).any() and all(len(v) == 1024 for v in parts.values())
    positives = training[gain[training] > 0]; sampling = np.r_[training, np.tile(positives, 7)]
    return {'x': x, 'y': y, 'actual_gain': gain, 'graph': graph, 'kind': kind, 'view': view, 'cycles': cycles,
        'amplitudes': amplitudes, 'training': training, 'captain_validation': captain_validation,
        'uniform_validation_graphs': uniform_graphs, 'mixed_validation_graphs': mixed_graphs,
        'sampling': sampling, **parts}, stages


def validation_loss(model, data):
    parts = {}
    for name in ['captain', 'positive', 'negative', 'mixed']:
        ids = data[name+'_validation']; parts[name] = float(model.loss(data['x'][ids], data['y'][ids]))
    return float(sum(parts.values())/4), parts


def train(args):
    args.out.mkdir(parents=True, exist_ok=False); model, parent = load(args.parent)
    assert parent['kind'] == 'signed-synthetic-graph-cycle-critic-v1'
    data, stages = collect(args, parent); save_dataset(args.out/'dataset.npz', data)
    initial = {key: v.copy() for key, v in model.p.items()}
    first = {key: np.zeros_like(v) for key, v in initial.items()}; second = {key: v.copy() for key, v in first.items()}
    initial_loss, initial_parts = validation_loss(model, data); best_loss = initial_loss; best = initial
    history = []; updates = selected_updates = selected_epoch = 0; rng = np.random.default_rng(args.seed); started = time.monotonic()
    for epoch in range(32):
        order = rng.permutation(data['sampling'])
        for offset in range(0, len(order), 64):
            ids = order[offset:offset+64]; _, gradients = model.loss(data['x'][ids], data['y'][ids], True)
            norm = np.sqrt(sum(np.sum(v*v) for v in gradients.values())); updates += 1
            for key in KEYS:
                g = gradients[key]*min(1., 5/max(norm, 1e-12)); first[key] = .9*first[key]+.1*g
                second[key] = .999*second[key]+.001*g*g
                model.p[key] -= .0005*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started >= 10: break
        loss, parts = validation_loss(model, data)
        history.append({'epoch': epoch+1, 'updates': updates, 'validationLoss': loss, 'parts': parts})
        if loss < best_loss:
            best_loss = loss; best = {key: v.copy() for key, v in model.p.items()}; selected_updates = updates; selected_epoch = epoch+1
        if time.monotonic()-started >= 10: break
    assert selected_updates > 0; model.p = best; final_loss, final_parts = validation_loss(model, data); assert final_loss == best_loss
    changes = {key: float(np.linalg.norm(best[key]-initial[key])) for key in KEYS}; assert all(v > 0 for v in changes.values())
    rows_by_kind = {k: int((data['kind'] == k).sum()) for k in ['captain', 'positive', 'negative', 'mixed']}
    positives_by_kind = {k: int(((data['kind'][data['training']] == k) & (data['actual_gain'][data['training']] > 0)).sum()) for k in rows_by_kind}
    metadata = {'schema': SCHEMA, 'kind': 'owner-amplitude-synthetic-cycle-critic-v1',
        'parent': str(args.parent), 'parentSha256': digest(args.parent), 'datasetPath': parent['datasetPath'], 'datasetSha256': parent['datasetSha256'],
        'datasetRole': 'inherited Captain exclusion dataset only; actual balanced learning uses trainingDatasetPath',
        'trainingDatasetPath': str(args.out/'dataset.npz'), 'trainingDatasetSha256': digest(args.out/'dataset.npz'),
        'sourceInputsByView': parent['sourceInputsByView'], 'sourceStages': stages, 'rows': len(data['x']),
        'rowsByKind': rows_by_kind, 'trainingRows': len(data['training']), 'improvingTrainingExamplesByKind': positives_by_kind,
        'nonuniformTrainingRows': int((data['kind'][data['training']] == 'mixed').sum()),
        'trainedOnMixedSizeWords': True, 'positiveSyntheticExamplesReplayedIntoTraining': True,
        'captainValidationRows': len(data['captain_validation']), 'syntheticValidationRowsPerKind': 1024,
        'uniformValidationGraphs': data['uniform_validation_graphs'].tolist(), 'mixedValidationGraphs': data['mixed_validation_graphs'].tolist(),
        'validationScope': 'complete graphs excluded from optimizer training; original Captain triple fold and reused uniform graph folds; new mixed fixture graph fold',
        'positiveTrainingSamplingMultiplicity': 8, 'normalizationUnchanged': True, 'streamedDatasetWriter': True,
        'streamedDatasetAllArrayPayloadsRoundTripVerified': True,
        'newTrainingUpdatesExecuted': updates, 'selectedCheckpointUpdates': selected_updates, 'selectedEpoch': selected_epoch,
        'seed': args.seed, 'epochsLimit': 32, 'secondsLimit': 10, 'batchSize': 64, 'learningRate': .0005,
        'initialValidationLoss': initial_loss, 'bestValidationLoss': best_loss, 'initialValidationParts': initial_parts,
        'selectedValidationParts': final_parts, 'parameterGroupChangesL2': changes, 'codeSha256': hashes(), 'seconds': time.monotonic()-started}
    np.savez_compressed(args.out/'model.npz', **best, **{'initial__'+key: v for key, v in initial.items()},
                        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out/'model.npz'); assert np.array_equal(chunk_predictions(restored, data['x']), chunk_predictions(model, data['x']))
    report = metadata | {'checkpointSha256': digest(args.out/'model.npz'), 'history': history}
    (args.out/'training.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ['rows', 'rowsByKind', 'trainingRows', 'improvingTrainingExamplesByKind',
        'newTrainingUpdatesExecuted', 'selectedCheckpointUpdates', 'initialValidationLoss', 'bestValidationLoss',
        'initialValidationParts', 'selectedValidationParts', 'checkpointSha256', 'seconds']}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--parent', type=Path, required=True); p.add_argument('--captain', type=Path, required=True)
    p.add_argument('--positive-source', type=Path, required=True); p.add_argument('--negative-source', type=Path, required=True)
    p.add_argument('--mixed-source', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=89213); train(p.parse_args())
