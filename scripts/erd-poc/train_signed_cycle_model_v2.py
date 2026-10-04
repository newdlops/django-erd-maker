#!/usr/bin/env python3
"""Learn signed outcomes with streamed inherited graph validation features."""
import argparse
import json
import zipfile
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import KEYS
from fractional_cycle_model import load as parent_load
from run_anchor_pair_policy import digest
from signed_cycle_model import SCHEMA
from signed_trained_critic import load
from synthetic_signed_cycle_curriculum import code_hashes as collection_hashes
from train_fractional_cycle_model import chunk_predictions


def read_rows(path, name, ids):
    """Read selected C-order rows without holding the full inherited tensor."""
    ids = np.asarray(ids, dtype=np.int64)
    assert len(np.unique(ids)) == len(ids)
    with zipfile.ZipFile(path) as archive, archive.open(name+'.npy') as stream:
        version = np.lib.format.read_magic(stream)
        shape, fortran, dtype = (np.lib.format.read_array_header_1_0 if version == (1, 0)
            else np.lib.format.read_array_header_2_0)(stream)
        assert not fortran and not dtype.hasobject and ((ids >= 0) & (ids < shape[0])).all()
        result = np.empty((len(ids), *shape[1:]), dtype=dtype)
        row_bytes = int(np.prod(shape[1:]))*dtype.itemsize
        order = np.argsort(ids); wanted = ids[order]; copied = 0
        for first in range(0, shape[0], 64):
            count = min(64, shape[0]-first)
            block = stream.read(count*row_bytes); assert len(block) == count*row_bytes
            left = np.searchsorted(wanted, first); right = np.searchsorted(wanted, first+count)
            if left != right:
                rows = np.frombuffer(block, dtype=dtype).reshape(count, *shape[1:])
                result[order[left:right]] = rows[wanted[left:right]-first]
                copied += right-left
        assert copied == len(ids) and not stream.read(1)
    return result


def hashes():
    return collection_hashes() | {name: digest(Path(__file__).parent/name) for name in
        ['train_signed_cycle_model.py', 'train_signed_cycle_model_v2.py', 'signed_trained_critic.py']}


def collect(args, metadata):
    assert digest(args.captain) == metadata['datasetSha256']
    old_count = 5120; new_count = 4096; count = old_count+new_count
    x = np.empty((count, 3, 141)); y = np.empty(count); gain = np.empty(count)
    graph = np.full(count, -1, dtype=np.int32); view = np.empty(count, dtype='<U10')
    cycles = np.empty((count, 3), dtype=np.int32); fractions = np.empty(count)
    with np.load(args.captain, allow_pickle=False) as saved:
        assert saved['x'].shape == (old_count, 3, 141)
        for name, destination in [('x', x), ('y', y), ('actual_gain', gain), ('view', view),
                                  ('cycles', cycles), ('fractions', fractions)]:
            destination[:old_count] = saved[name]
        captain_training = saved['training'].copy(); captain_validation = saved['validation'].copy()
    stages = []; offset = old_count
    for graph_id in range(32):
        for name in ['overview', 'individual']:
            stage = args.source/f'graph-{graph_id:02}'/name
            report = json.loads((stage/'report.json').read_text())
            assert report['graph'] == graph_id and report['view'] == name and report['attempts'] == 64
            assert report['checkpointSha256'] == digest(args.parent) and report['codeSha256'] == collection_hashes()
            assert report['initialHard'] == report['initialIndividualHard'] == 0
            assert digest(stage/'labels.npz') == report['labelsSha256']
            assert digest(stage/'actions.jsonl') == report['actionsSha256']
            source = Path(report['sourceDirectory'])
            assert all(digest(source/n) == value for n, value in report['sourceInputs'].items())
            with np.load(stage/'labels.npz', allow_pickle=False) as saved:
                assert saved['x'].shape == (64, 3, 141) and (saved['fractions'] < 0).all()
                for key, destination in [('x', x), ('y', y), ('actual_gain', gain), ('cycles', cycles), ('fractions', fractions)]:
                    destination[offset:offset+64] = saved[key]
            graph[offset:offset+64] = graph_id; view[offset:offset+64] = name; offset += 64
            stages.append({'path': str(stage/'report.json'), 'sha256': digest(stage/'report.json'),
                           'graph': graph_id, 'view': name, 'labelsSha256': report['labelsSha256']})
    assert offset == count and np.isfinite(x).all() and np.isfinite(y).all()
    assert digest(args.positive_validation) == metadata['trainingDatasetSha256']
    with np.load(args.positive_validation, allow_pickle=False) as saved:
        graphs = saved['validation_graphs'].copy(); ids = saved['synthetic_validation'].copy()
        assert graphs.tolist() == metadata['syntheticValidationGraphs'] and len(ids) == 1024
        positive_x = read_rows(args.positive_validation, 'x', ids); positive_y = saved['y'][ids]
        assert np.isin(saved['graph'][ids], graphs).all()
        assert not np.isin(saved['graph'][saved['training']], graphs).any()
    signed_validation = np.flatnonzero(np.isin(graph, graphs))
    signed_training = np.flatnonzero((graph >= 0) & ~np.isin(graph, graphs))
    training = np.r_[captain_training, signed_training]
    validation = np.r_[captain_validation, signed_validation]
    assert not np.intersect1d(training, validation).size
    assert len(signed_validation) == 1024 and not np.isin(graph[training], graphs).any()
    positive = training[gain[training] > 0]; sampling = np.r_[training, np.tile(positive, 7)]
    return {'x': x, 'y': y, 'actual_gain': gain, 'graph': graph, 'view': view, 'cycles': cycles,
        'fractions': fractions, 'training': training, 'captain_validation': captain_validation,
        'signed_validation': signed_validation, 'validation_graphs': graphs, 'sampling': sampling,
        'positive_validation_x': positive_x, 'positive_validation_y': positive_y}, stages


def validation_loss(model, data):
    values = {}
    for name in ['captain', 'signed']:
        ids = data[name+'_validation']; values[name] = float(model.loss(data['x'][ids], data['y'][ids]))
    values['positive'] = float(model.loss(data['positive_validation_x'], data['positive_validation_y']))
    return float(sum(values.values())/3), values


def train(args):
    args.out.mkdir(parents=True, exist_ok=False)
    model, parent = parent_load(args.parent)
    assert parent['kind'] == 'positive-synthetic-graph-cycle-critic-v1'
    data, stages = collect(args, parent); np.savez_compressed(args.out/'dataset.npz', **data)
    initial = {key: value.copy() for key, value in model.p.items()}
    first = {key: np.zeros_like(value) for key, value in initial.items()}; second = {key: value.copy() for key, value in first.items()}
    initial_loss, initial_parts = validation_loss(model, data)
    best_loss = initial_loss; best = initial; history = []; updates = selected_updates = selected_epoch = 0
    rng = np.random.default_rng(args.seed); started = time.monotonic()
    for epoch in range(64):
        order = rng.permutation(data['sampling'])
        for offset in range(0, len(order), 64):
            ids = order[offset:offset+64]; _, gradients = model.loss(data['x'][ids], data['y'][ids], True)
            norm = np.sqrt(sum(np.sum(v*v) for v in gradients.values())); updates += 1
            for key in KEYS:
                g = gradients[key]*min(1., 5/max(norm, 1e-12))
                first[key] = .9*first[key]+.1*g; second[key] = .999*second[key]+.001*g*g
                model.p[key] -= .0005*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started >= 10:
                break
        loss, parts = validation_loss(model, data)
        history.append({'epoch': epoch+1, 'updates': updates, 'validationLoss': loss, 'parts': parts})
        if loss < best_loss:
            best_loss = loss; best = {key: value.copy() for key, value in model.p.items()}
            selected_updates = updates; selected_epoch = epoch+1
        if time.monotonic()-started >= 10:
            break
    assert selected_updates > 0
    model.p = best; changes = {key: float(np.linalg.norm(best[key]-initial[key])) for key in KEYS}
    assert all(v > 0 for v in changes.values()); final_loss, final_parts = validation_loss(model, data)
    assert final_loss == best_loss
    metadata = {'schema': SCHEMA, 'kind': 'signed-synthetic-graph-cycle-critic-v1',
        'parent': str(args.parent), 'parentSha256': digest(args.parent),
        'datasetPath': parent['datasetPath'], 'datasetSha256': parent['datasetSha256'],
        'datasetRole': 'inherited Captain measured-word exclusion only; training dataset is trainingDatasetPath',
        'trainingDatasetPath': str(args.out/'dataset.npz'), 'trainingDatasetSha256': digest(args.out/'dataset.npz'),
        'positiveValidationDataset': str(args.positive_validation), 'positiveValidationDatasetSha256': digest(args.positive_validation),
        'sourceInputsByView': parent['sourceInputsByView'], 'sourceStages': stages,
        'rows': len(data['x']), 'captainRows': 5120, 'signedSyntheticRows': 4096,
        'trainingRows': len(data['training']), 'negativeTrainingRows': int((data['fractions'][data['training']] < 0).sum()),
        'actualImprovementExamples': int((data['actual_gain'][data['training']] > 0).sum()),
        'captainValidationRows': len(data['captain_validation']), 'signedValidationRows': len(data['signed_validation']),
        'positiveValidationRows': len(data['positive_validation_x']), 'validationGraphs': data['validation_graphs'].tolist(),
        'validationScope': 'complete synthetic graphs held out from parent positive and new signed optimizer training; original Captain whole-triple fold retained',
        'positiveExamplesReplayedIntoTraining': False, 'positiveValidationOnly': True,
        'positiveTrainingSamplingMultiplicity': 8, 'originalNormalizationUnchanged': True,
        'newTrainingUpdatesExecuted': updates, 'selectedCheckpointUpdates': selected_updates,
        'selectedEpoch': selected_epoch, 'seed': args.seed, 'epochsLimit': 64, 'secondsLimit': 10,
        'batchSize': 64, 'learningRate': .0005, 'initialValidationLoss': initial_loss, 'bestValidationLoss': best_loss,
        'initialValidationParts': initial_parts, 'selectedValidationParts': final_parts,
        'parameterGroupChangesL2': changes, 'trainedOnNegativeFractions': True,
        'codeSha256': hashes(), 'seconds': time.monotonic()-started}
    np.savez_compressed(args.out/'model.npz', **best, **{'initial__'+key: v for key, v in initial.items()},
                        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out/'model.npz')
    assert np.array_equal(chunk_predictions(restored, data['x']), chunk_predictions(model, data['x']))
    report = metadata | {'checkpointSha256': digest(args.out/'model.npz'), 'history': history}
    (args.out/'training.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({key: report[key] for key in ['rows', 'trainingRows', 'negativeTrainingRows', 'actualImprovementExamples',
        'newTrainingUpdatesExecuted', 'selectedCheckpointUpdates', 'initialValidationLoss', 'bestValidationLoss',
        'initialValidationParts', 'selectedValidationParts', 'checkpointSha256', 'seconds']}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--parent', type=Path, required=True); p.add_argument('--captain', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True); p.add_argument('--positive-validation', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True); p.add_argument('--seed', type=int, default=87509); train(p.parse_args())
