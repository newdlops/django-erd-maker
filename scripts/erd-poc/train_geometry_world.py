#!/usr/bin/env python3
"""Train small event classifiers on fixed source and synthetic geometry labels.

Exact predicates occur only in this teacher. No Captain candidate coordinates
are searched, ranked or emitted by training.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import time
import numpy as np
from geometry_world_model import (SCHEMA, FEATURES, Classifier, digest,
    cross_features, cross_eligible, hit_features, hit_eligible)
from learn_pair_policy import KEYS
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder
from single_owner_cached_observer_v2 import CachedObserver, crossing_matrix, hit_matrix


def paired_cross_teacher(a, b):
    def orient(p, q, r):
        u, v = q-p, r-p
        return u[:, 0]*v[:, 1]-u[:, 1]*v[:, 0]
    return ((orient(a[:, 0], a[:, 1], b[:, 0])*orient(a[:, 0], a[:, 1], b[:, 1]) < -1e-9)
            & (orient(b[:, 0], b[:, 1], a[:, 0])*orient(b[:, 0], b[:, 1], a[:, 1]) < -1e-9)
            & cross_eligible(a, b))


def paired_hit_teacher(lines, positions, sizes):
    low, high = positions-sizes/2-10, positions+sizes/2+10
    origin, direction = lines[:, 0], lines[:, 1]-lines[:, 0]
    valid = hit_eligible(lines, positions, sizes)
    lower, upper = np.zeros(len(lines)), np.ones(len(lines))
    for axis in (0, 1):
        d = direction[:, axis]; parallel = abs(d) < 1e-9
        den = np.where(parallel, 1., d)
        left, right = (low[:, axis]-origin[:, axis])/den, (high[:, axis]-origin[:, axis])/den
        lower = np.maximum(lower, np.where(parallel, 0., np.minimum(left, right)))
        upper = np.minimum(upper, np.where(parallel, 1., np.maximum(left, right)))
        valid &= np.where(parallel, (origin[:, axis] > low[:, axis]) & (origin[:, axis] < high[:, axis]), upper-lower > 1e-9)
    return valid & (upper > 1e-9) & (lower < 1-1e-9)


def select(labels, limit, rng):
    yes, no = np.flatnonzero(labels), np.flatnonzero(~labels)
    yes = yes if len(yes) <= limit//2 else rng.choice(yes, limit//2, replace=False)
    no = no if len(no) <= limit-len(yes) else rng.choice(no, limit-len(yes), replace=False)
    return np.r_[yes, no]


def source_rows(binding_path, environment, out, seed):
    binding = json.loads(binding_path.read_text()); result = {k: [] for k in FEATURES}; records = []
    rng = np.random.default_rng(seed)
    for view_id, (view, spec) in enumerate(binding['viewSources'].items()):
        directory = Path(spec['directory']); decoder = WalkDecoder(directory)
        assert {name: digest(directory/name) for name in spec['inputs']} == spec['inputs']
        observer = CachedObserver(directory, decoder)
        zero = decoder.decode(np.arange(len(decoder.positions), dtype=np.int32))
        native = Native(environment, directory, out/(view+'-baseline.tsv'), view == 'overview', False)
        try:
            baseline = native.request(wire(zero, 'MEASURE'))
            assert baseline['legal'] and baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
            assert baseline['visual'] == spec['expectedVisual']
        finally:
            native.close()
        pos, full_pos, physical, full, physical_edges = observer.decode(zero)
        positions, lines, sizes, edges = ((pos, physical, observer.sizes, physical_edges) if view == 'overview'
            else (full_pos, full, observer.full_sizes, observer.edges))
        crosses = crossing_matrix(lines); hits = hit_matrix(lines, positions, sizes, edges)
        count = int(np.count_nonzero(crosses)//2+np.count_nonzero(hits))
        assert count == baseline['visual']
        left_parts, right_parts = [], []
        for first in range(0, len(lines), 32):
            left = np.repeat(np.arange(first, min(first+32, len(lines))), len(lines))
            right = np.tile(np.arange(len(lines)), min(32, len(lines)-first))
            keep = left < right; left, right = left[keep], right[keep]
            possible = cross_eligible(lines[left], lines[right])
            left_parts.append(left[possible]); right_parts.append(right[possible])
        left, right = np.concatenate(left_parts), np.concatenate(right_parts)
        labels = crosses[left, right]
        chosen = select(labels, 12000, rng); left, right, labels = left[chosen], right[chosen], labels[chosen]
        assert np.array_equal(labels, paired_cross_teacher(lines[left], lines[right]))
        fold = ((left.astype(np.int64)*1000003+right*9176+view_id*13) % 5 == 0)
        result['cross'].append((cross_features(lines[left], lines[right]), labels, fold, np.full(len(labels), view_id, np.int16)))
        line_ids, node_ids = [], []
        for first in range(0, len(lines), 32):
            a = np.repeat(np.arange(first, min(first+32, len(lines))), len(sizes))
            b = np.tile(np.arange(len(sizes)), min(32, len(lines)-first))
            possible = np.all(edges[a] != b[:, None], axis=1) & hit_eligible(lines[a], positions[b], sizes[b])
            line_ids.append(a[possible]); node_ids.append(b[possible])
        a, b = np.concatenate(line_ids), np.concatenate(node_ids); labels = hits[a, b]
        chosen = select(labels, 12000, rng); a, b, labels = a[chosen], b[chosen], labels[chosen]
        assert np.array_equal(labels, paired_hit_teacher(lines[a], positions[b], sizes[b]))
        fold = ((a.astype(np.int64)*1000003+b*9176+view_id*13) % 5 == 0)
        result['hit'].append((hit_features(lines[a], positions[b], sizes[b]), labels, fold, np.full(len(labels), view_id, np.int16)))
        records.append({'view': view, 'nativeBaseline': baseline, 'pythonTeacherAggregate': count,
            'edgeCrossings': int(np.count_nonzero(crosses)//2), 'cardHits': int(np.count_nonzero(hits)),
            'sourceInputs': spec['inputs'], 'pairLabelsComparedToVectorTeacher': True,
            'individualNativePairLabelsExported': False})
        del observer, decoder, crosses, hits, lines, positions
    return result, records


def synthetic_rows(seed):
    rng = np.random.default_rng(seed); result = {k: [] for k in FEATURES}
    for graph in range(20):
        count = 5000; a = rng.uniform(-1000, 1000, (count, 2, 2)); b = rng.uniform(-1000, 1000, (count, 2, 2))
        # Shared endpoints and near-boundary examples teach zero-area contacts.
        b[:count//5, 0] = a[:count//5, 0]
        a[count//5:count//4, 1, 0] = a[count//5:count//4, 0, 0]
        eligible = cross_eligible(a, b); a, b = a[eligible], b[eligible]
        labels = paired_cross_teacher(a, b); chosen = select(labels, 2200, rng)
        labels = labels[chosen]; features = cross_features(a[chosen], b[chosen])
        result['cross'].append((features, labels, np.full(len(labels), graph%5 == 4), np.full(len(labels), graph+100, np.int16)))
        lines = rng.uniform(-1000, 1000, (count, 2, 2)); positions = rng.uniform(-700, 700, (count, 2))
        sizes = rng.uniform(20, 900, (count, 2)); lines[:count//6, 1, 0] = lines[:count//6, 0, 0]
        lines[count//6:count//3, 1, 1] = lines[count//6:count//3, 0, 1]
        eligible = hit_eligible(lines, positions, sizes); lines, positions, sizes = lines[eligible], positions[eligible], sizes[eligible]
        labels = paired_hit_teacher(lines, positions, sizes); chosen = select(labels, 2200, rng)
        labels = labels[chosen]; features = hit_features(lines[chosen], positions[chosen], sizes[chosen])
        result['hit'].append((features, labels, np.full(len(labels), graph%5 == 4), np.full(len(labels), graph+100, np.int16)))
    return result


def metrics(model, x, y):
    predicted = model.probability(x) >= .5
    tp = np.count_nonzero(predicted & y); fp = np.count_nonzero(predicted & ~y); fn = np.count_nonzero(~predicted & y)
    return {'rows': len(y), 'positives': int(y.sum()), 'bce': model.loss(x, y, np.ones(len(y))),
            'precision': float(tp/max(1, tp+fp)), 'recall': float(tp/max(1, tp+fn)),
            'falsePositive': int(fp), 'falseNegative': int(fn)}


def train_head(task, x, y, validation, groups, seed, seconds):
    train = np.flatnonzero(~validation); val = np.flatnonzero(validation)
    model = Classifier(seed, FEATURES[task], 24)
    model.mean = x[train].mean(0, dtype=np.float64); model.scale = np.maximum(.1, x[train].std(0, dtype=np.float64))
    initial = {k: v.copy() for k, v in model.p.items()}; first = {k: np.zeros_like(v) for k, v in model.p.items()}
    second = {k: np.zeros_like(v) for k, v in model.p.items()}; rng = np.random.default_rng(seed)
    positive, negative = train[y[train]], train[~y[train]]
    weights = np.ones(len(y)); updates = 0; selected_updates = 0; best = None; best_loss = math.inf; trace = []
    initial_metrics = {name: metrics(model, x[mask], y[mask]) for name, mask in
        [('source', validation & (groups < 100)), ('synthetic', validation & (groups >= 100))]}
    started = time.monotonic()
    for epoch in range(80):
        balanced = np.r_[train, rng.choice(positive, max(0, len(negative)-len(positive)), replace=True)]
        order = rng.permutation(balanced)
        for offset in range(0, len(order), 256):
            batch = order[offset:offset+256]; loss, grads = model.loss(x[batch], y[batch], weights[batch], True)
            assert math.isfinite(loss); updates += 1
            for k in KEYS:
                g = grads[k]; first[k] = .9*first[k]+.1*g; second[k] = .999*second[k]+.001*g*g
                model.p[k] -= .003*(first[k]/(1-.9**updates))/(np.sqrt(second[k]/(1-.999**updates))+1e-8)
            if time.monotonic()-started >= seconds: break
        current = model.loss(x[val], y[val], weights[val]); trace.append({'epoch': epoch+1, 'updates': updates, 'validationBce': current})
        if current < best_loss: best_loss = current; selected_updates = updates; best = {k: v.copy() for k, v in model.p.items()}
        if time.monotonic()-started >= seconds: break
    assert best is not None and selected_updates > 0; model.p = best
    final_metrics = {name: metrics(model, x[mask], y[mask]) for name, mask in
        [('source', validation & (groups < 100)), ('synthetic', validation & (groups >= 100))]}
    return model, initial, {'rows': len(y), 'trainingRows': len(train), 'validationRows': len(val),
        'updatesExecuted': updates, 'selectedCheckpointUpdates': selected_updates, 'seconds': time.monotonic()-started,
        'initial': initial_metrics, 'final': final_metrics, 'trace': trace,
        'changedParameterGroups': [k for k in KEYS if not np.array_equal(initial[k], model.p[k])]}


def run(args):
    args.out.mkdir(parents=True, exist_ok=False); started = time.monotonic()
    source, records = source_rows(args.source_binding, args.environment, args.out, args.seed)
    synthetic = synthetic_rows(args.seed+1); data = {}; saved = {}; reports = {}
    for task in FEATURES:
        parts = source[task]+synthetic[task]
        x, y, validation, groups = [np.concatenate([p[i] for p in parts]) for i in range(4)]
        assert x.dtype == np.float32 and np.isfinite(x).all() and y.dtype == validation.dtype == bool
        data.update({task+'__x': x, task+'__y': y, task+'__validation': validation, task+'__groups': groups})
        model, initial, reports[task] = train_head(task, x, y, validation, groups, args.seed+FEATURES[task], args.seconds)
        saved.update({task+'__'+k: v for k, v in model.p.items()})
        saved.update({task+'__initial__'+k: v for k, v in initial.items()})
        saved[task+'__mean'], saved[task+'__scale'] = model.mean, model.scale
        print(json.dumps({'task': task, **reports[task]}), flush=True)
    np.savez_compressed(args.out/'dataset.npz', **data)
    metadata = {'schema': SCHEMA, 'kind': 'two-small-neural-geometric-event-classifiers', 'seed': args.seed,
        'featureInputDtype': 'float32', 'parameterAndInnerDtype': 'float64', 'hidden': 24,
        'newTrainingUpdatesExecuted': sum(r['updatesExecuted'] for r in reports.values()), 'tasks': reports,
        'trainingOnlyExactPredicates': True, 'trainingSearchCalls': 0, 'candidateCoordinatesEmittedByTeacher': 0,
        'sourcePairHoldout': 'Deterministic within-source pair holdout; not an unseen Captain scene',
        'syntheticHoldoutGraphs': [104,109,114,119], 'sourceBinding': str(args.source_binding),
        'sourceBindingSha256': digest(args.source_binding), 'nativeEnvironmentSha256': digest(args.environment),
        'nativeBaselineControls': records, 'datasetSha256': digest(args.out/'dataset.npz'),
        'parameters': sum(v.size for k, v in saved.items() if '__initial__' not in k and k.rsplit('__',1)[-1] in KEYS),
        'codeSha256': {name: digest(Path(__file__).parent/name) for name in
            ['train_geometry_world.py', 'geometry_world_model.py', 'single_owner_cached_observer_v2.py', 'learn_pair_policy.py']},
        'wallSeconds': time.monotonic()-started}
    np.savez_compressed(args.out/'model.npz', **saved, metadata=np.array(json.dumps(metadata)))
    (args.out/'report.json').write_text(json.dumps(metadata, indent=2)+'\n')
    print(json.dumps({'checkpoint': str(args.out/'model.npz'), 'sha256': digest(args.out/'model.npz'),
        'updates': metadata['newTrainingUpdatesExecuted'], 'wallSeconds': metadata['wallSeconds']}), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=103109); p.add_argument('--seconds', type=float, default=6.)
    run(p.parse_args())
