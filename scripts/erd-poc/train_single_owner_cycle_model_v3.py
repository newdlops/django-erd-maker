#!/usr/bin/env python3
"""Adapt the frozen single-owner NN to verified global and trajectory outcomes."""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from directed_cycle_model import KEYS
from owner_amplitude_cycle_model import SCHEMA
from single_owner_cycle_model import ACTION_SCHEMA, MODEL_KIND, load
from train_single_owner_cycle_model_v2 import validation_loss, word, fold, hashes as parent_hashes


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(65536):
            h.update(block)
    return h.hexdigest()


def hashes():
    return parent_hashes() | {Path(__file__).name: digest(__file__)}


def collect(args, parent):
    original = Path(parent['trainingDatasetDirectory'])
    index = original / 'dataset-index.json'
    assert digest(index) == parent['trainingDatasetIndexSha256']
    ledger = json.loads(index.read_text())
    for name, item in ledger.items():
        path = original / name
        assert path.stat().st_size == item['bytes'] and digest(path) == item['sha256']
    prior = args.previous
    rewards = json.loads((args.root / 'training-reward-controls/audit.json').read_text())
    fresh = json.loads((args.root / 'verification/walks.audit.json').read_text())
    assert rewards['status'] == fresh['status'] == 'pass'
    assert rewards['readOnlyFullNativeMeasurements'] == 2259
    assert fresh['readOnlyFullNativeMeasurements'] == 255 and fresh['fullNativeFeatureControls'] == 85
    sources = []
    for row in rewards['checks']:
        path = Path(row['labels'])
        assert digest(path) == row['labelsSha256'] and row['allRewardsRecomputedExactly']
        stage = Path(row['stage'])
        category = 'captain_global' if stage.name.endswith('-global1') else 'captain_walk'
        sources.append((path, stage, category))
    for row in fresh['checks']:
        assert row['allModelInputStatesNativeVerified'] and row['allNewNativeActionScoresIndependentlyRecomputed']
        stage = Path(row['stage'])
        sources.append((args.root / 'verification' / (stage.name + '-labels.npz'), stage, 'captain_walk'))
    stages = []
    extra = 0
    for path, stage, category in sources:
        report = json.loads((stage / 'report.json').read_text())
        assert report['checkpointSha256'] == digest(args.parent)
        assert all(digest(Path(report['sourceDirectory']) / name) == sha for name, sha in report['sourceInputs'].items())
        # Word indices retain their meanings because all immutable topology,
        # dimensions, memberships and canonical order are unchanged.
        for name in ('nodes.tsv', 'edges.tsv', 'individual.nodes.tsv', 'individual.edges.tsv', 'components.tsv', 'groups.tsv'):
            assert report['sourceInputs'][name] == parent['sourceInputsByView'][report['view']][name]
        with np.load(path, allow_pickle=False) as saved:
            count = len(saved['y'])
            assert saved['x'].shape == (count, 3, 141) and saved['x'].dtype == np.float32
        assert count == report.get('attempts', report.get('newNeuralProposals'))
        extra += count
        stages.append({'path': str(stage / 'report.json'), 'sha256': digest(stage / 'report.json'),
                       'labels': str(path), 'labelsSha256': digest(path),
                       'kind': category, 'view': report['view'], 'rows': count})
    assert extra == 2512
    old_count = parent['rows']
    assert old_count == 23552
    count = old_count + extra
    dataset = args.out / 'dataset'
    dataset.mkdir()
    x = np.lib.format.open_memmap(dataset / 'x.npy', mode='w+', dtype=np.float32, shape=(count, 3, 141))
    with (original / 'x.npy').open('rb') as stream:
        version = np.lib.format.read_magic(stream)
        shape, fortran, dtype = (np.lib.format.read_array_header_1_0 if version == (1, 0)
                                 else np.lib.format.read_array_header_2_0)(stream)
        assert shape == (old_count, 3, 141) and dtype == np.float32 and not fortran
        for first in range(0, old_count, 64):
            size = min(64, old_count - first)
            block = stream.read(size * 3 * 141 * 4)
            assert len(block) == size * 3 * 141 * 4
            x[first:first + size] = np.frombuffer(block, dtype=np.float32).reshape(size, 3, 141)
        assert not stream.read(1)
    data = {'x': x, 'y': np.empty(count), 'actual_gain': np.empty(count),
            'cycles': np.empty((count, 3), dtype=np.int32), 'amplitudes': np.empty((count, 3)),
            'view': np.empty(count, dtype='<U10'), 'kind': np.empty(count, dtype='<U16'),
            'graph': np.full(count, -1, dtype=np.int32)}
    for name in ('y', 'actual_gain', 'cycles', 'amplitudes', 'view', 'kind', 'graph'):
        data[name][:old_count] = np.load(original / (name + '.npy'), allow_pickle=False)
    old_training = np.load(original / 'training.npy', allow_pickle=False)
    part_names = ('captain', 'positive', 'negative', 'mixed', 'captain_single', 'single_synthetic')
    parts = {name: np.load(original / (name + '_validation.npy'), allow_pickle=False) for name in part_names}
    val_words = {word(data['view'][i], data['cycles'][i]) for i in np.r_[parts['captain'], parts['captain_single']]}
    seen_words = {word(data['view'][i], data['cycles'][i]) for i in old_training
                  if data['kind'][i] in ('captain', 'captain_single')}
    assert not val_words & seen_words
    offset = old_count
    new_training = []
    new_validation = {name: [] for name in ('captain_global', 'captain_walk')}
    for stage in stages:
        size = stage['rows']
        with np.load(stage['labels'], allow_pickle=False) as saved:
            for name in ('x', 'y', 'actual_gain', 'cycles', 'amplitudes'):
                data[name][offset:offset + size] = saved[name]
        data['view'][offset:offset + size] = stage['view']
        data['kind'][offset:offset + size] = stage['kind']
        for i in range(offset, offset + size):
            key = word(stage['view'], data['cycles'][i])
            if key in val_words or (fold(key) and key not in seen_words):
                new_validation[stage['kind']].append(i)
            else:
                new_training.append(i)
        offset += size
    assert offset == count
    parts.update({name: np.array(ids, dtype=np.int64) for name, ids in new_validation.items()})
    assert all(len(ids) for ids in parts.values())
    training = np.r_[old_training, np.array(new_training, dtype=np.int64)]
    validation = np.r_[*parts.values()]
    assert not np.intersect1d(training, validation).size
    hold_words = {word(data['view'][i], data['cycles'][i]) for name in
                  ('captain', 'captain_single', 'captain_global', 'captain_walk') for i in parts[name]}
    assert all(word(data['view'][i], data['cycles'][i]) not in hold_words for i in training
               if str(data['kind'][i]).startswith('captain'))
    graph_fold = np.load(original / 'single_validation_graphs.npy', allow_pickle=False)
    assert not np.isin(data['graph'][training], graph_fold).any()
    single = training[np.isin(data['kind'][training], ['captain_single', 'single_synthetic', 'captain_global', 'captain_walk'])]
    improving = training[data['actual_gain'][training] > 0]
    sampling = np.r_[training, np.tile(single, 2), np.tile(improving, 7)]
    data.update(training=training, sampling=sampling, single_validation_graphs=graph_fold,
                **{name + '_validation': ids for name, ids in parts.items()})
    for first in range(0, count, 64):
        assert np.isfinite(x[first:first + 64]).all()
    assert np.isfinite(data['y']).all()
    x.flush()
    for name, value in data.items():
        if name != 'x':
            np.save(dataset / (name + '.npy'), value, allow_pickle=False)
    # Release the write map before training, so collection and learning never
    # retain two complete feature maps in process RSS.
    del data['x']
    del x
    gc.collect()
    data['x'] = np.load(dataset / 'x.npy', allow_pickle=False, mmap_mode='r')
    ledger = {path.name: {'bytes': path.stat().st_size, 'sha256': digest(path)} for path in dataset.glob('*.npy')}
    (dataset / 'dataset-index.json').write_text(json.dumps(ledger, indent=2) + '\n')
    return data, parts, stages, original


def train(args):
    args.out.mkdir(exist_ok=False)
    model, parent = load(args.parent)
    assert parent['kind'] == MODEL_KIND
    data, parts, stages, original = collect(args, parent)
    initial = {key: value.copy() for key, value in model.p.items()}
    first = {key: np.zeros_like(value) for key, value in initial.items()}
    second = {key: value.copy() for key, value in first.items()}
    initial_loss, initial_parts = validation_loss(model, data, parts)
    best_loss = initial_loss
    best = initial
    updates = selected_updates = selected_epoch = 0
    history = []
    rng = np.random.default_rng(args.seed)
    started = time.monotonic()
    for epoch in range(32):
        order = rng.permutation(data['sampling'])
        for offset in range(0, len(order), 64):
            ids = order[offset:offset + 64]
            _, gradients = model.loss(data['x'][ids], data['y'][ids], True)
            norm = np.sqrt(sum(np.sum(g * g) for g in gradients.values()))
            updates += 1
            for key in KEYS:
                gradient = gradients[key] * min(1., 5 / max(norm, 1e-12))
                first[key] = .9 * first[key] + .1 * gradient
                second[key] = .999 * second[key] + .001 * gradient * gradient
                model.p[key] -= .0005 * (first[key] / (1 - .9 ** updates)) / (np.sqrt(second[key] / (1 - .999 ** updates)) + 1e-8)
            if time.monotonic() - started >= 10:
                break
        loss, values = validation_loss(model, data, parts)
        history.append({'epoch': epoch + 1, 'updates': updates, 'loss': loss, 'parts': values})
        if loss < best_loss:
            best_loss = loss
            best = {key: value.copy() for key, value in model.p.items()}
            selected_updates = updates
            selected_epoch = epoch + 1
        if time.monotonic() - started >= 10:
            break
    model.p = best
    final_loss, final_parts = validation_loss(model, data, parts)
    assert final_loss == best_loss
    changes = {key: float(np.linalg.norm(best[key] - initial[key])) for key in KEYS}
    assert (selected_updates == 0) == all(value == 0 for value in changes.values())
    metadata = {
        'schema': SCHEMA, 'kind': MODEL_KIND, 'actionSchema': ACTION_SCHEMA,
        'parent': str(args.parent), 'parentSha256': digest(args.parent),
        'sourceInputsByView': parent['sourceInputsByView'],
        'adaptationGeometryBinding': str(args.root / 'source-binding.json'),
        'adaptationGeometryBindingSha256': digest(args.root / 'source-binding.json'),
        'sourceStages': stages, 'trainingDatasetDirectory': str(args.out / 'dataset'),
        'trainingDatasetIndexSha256': digest(args.out / 'dataset/dataset-index.json'),
        'parentDatasetDirectory': str(original), 'parentDatasetIndexSha256': digest(original / 'dataset-index.json'),
        'rows': len(data['x']), 'trainingRows': len(data['training']),
        'rowsByKind': {str(kind): int((data['kind'] == kind).sum()) for kind in np.unique(data['kind'])},
        'singleOwnerTrainingRows': int(np.isin(data['kind'][data['training']],
          ['captain_single', 'single_synthetic', 'captain_global', 'captain_walk']).sum()),
        'newCaptainOutcomeRows': 2512, 'newCaptainTrainingRows': int((data['training'] >= 23552).sum()),
        'improvingTrainingExamplesByKind': {str(kind): int(((data['kind'][data['training']] == kind) &
          (data['actual_gain'][data['training']] > 0)).sum()) for kind in np.unique(data['kind'])},
        'validationRowsByPart': {name: len(ids) for name, ids in parts.items()},
        'singleSyntheticValidationGraphs': data['single_validation_graphs'].tolist(),
        'wholeCaptainWordFoldsPreserved': True, 'parentSeenTrainingWordsExcludedFromNewValidation': True,
        'validationScope': 'whole words and synthetic graphs excluded from adaptation; not an independent Captain scene',
        'allNewSourceRewardsIndependentlyFullNativeMeasured': True,
        'noHumanCoordinatesOrNativeSearch': True, 'normalizationUnchanged': True,
        'singleTrainingSamplingMultiplicity': 3, 'additionalImprovingTrainingCopies': 7,
        'trainingFeatureDtype': 'float32', 'innerNetworkArithmeticDtype': 'float64',
        'newTrainingUpdatesExecuted': updates, 'selectedAdaptationUpdates': selected_updates,
        'parentSelectedCheckpointUpdates': parent['selectedCheckpointUpdates'],
        'selectedCheckpointUpdates': parent['selectedCheckpointUpdates'] + selected_updates,
        'selectedCheckpointUpdatesScope': 'cumulative selected updates in the immediate parent and this adaptation',
        'selectedEpoch': selected_epoch, 'savedWeightsChanged': selected_updates > 0,
        'seed': args.seed, 'learningRate': .0005, 'batchSize': 64, 'epochsLimit': 32, 'secondsLimit': 10,
        'initialValidationLoss': initial_loss, 'bestValidationLoss': best_loss,
        'initialValidationParts': initial_parts, 'selectedValidationParts': final_parts,
        'parameterGroupChangesL2': changes, 'codeSha256': hashes(), 'seconds': time.monotonic() - started
    }
    np.savez_compressed(args.out / 'model.npz', **best, **{'initial__' + key: value for key, value in initial.items()},
                        mean=model.mean, scale=model.scale, metadata=json.dumps(metadata))
    restored, _ = load(args.out / 'model.npz')
    for first in range(0, len(data['x']), 256):
        assert np.array_equal(restored.forward(data['x'][first:first + 256])[0], model.forward(data['x'][first:first + 256])[0])
    report = metadata | {'checkpointSha256': digest(args.out / 'model.npz'), 'history': history}
    (args.out / 'training.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('rows', 'trainingRows', 'newCaptainTrainingRows',
        'improvingTrainingExamplesByKind', 'newTrainingUpdatesExecuted', 'selectedAdaptationUpdates',
        'initialValidationLoss', 'bestValidationLoss', 'initialValidationParts', 'selectedValidationParts',
        'savedWeightsChanged', 'checkpointSha256', 'seconds')}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('parent', 'root', 'previous', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--seed', type=int, default=101907)
    train(parser.parse_args())
