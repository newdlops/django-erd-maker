"""Small learned geometric event classifiers; no exact future event counts."""
import hashlib
import json
from pathlib import Path
import numpy as np
from learn_pair_policy import Ranker, KEYS
from learn_card_policy import sigmoid

SCHEMA = 'geometric-pair-event-world-v1'
FEATURES = {'cross': 6, 'hit': 4}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def cross_eligible(a, b):
    return np.all(np.minimum(a[:, 0], a[:, 1]) <= np.maximum(b[:, 0], b[:, 1]), axis=1) & np.all(
        np.minimum(b[:, 0], b[:, 1]) <= np.maximum(a[:, 0], a[:, 1]), axis=1)


def cross_features(a, b):
    # Continuous oriented areas and products, never an event Boolean.
    low = np.minimum(a.min(1), b.min(1))
    scale = np.maximum(np.maximum(a.max(1), b.max(1)) - low, 1.)
    a = (a - low[:, None]) / scale[:, None]
    b = (b - low[:, None]) / scale[:, None]
    def orient(p, q, r):
        u = q - p
        v = r - p
        return u[:, 0] * v[:, 1] - u[:, 1] * v[:, 0]
    areas = np.stack((orient(a[:, 0], a[:, 1], b[:, 0]), orient(a[:, 0], a[:, 1], b[:, 1]),
                      orient(b[:, 0], b[:, 1], a[:, 0]), orient(b[:, 0], b[:, 1], a[:, 1])), axis=1)
    products = np.stack((areas[:, 0] * areas[:, 1], areas[:, 2] * areas[:, 3]), axis=1)
    compressed = np.sign(products) * np.log1p(abs(products) * 1e6) / np.log1p(1e6)
    return np.concatenate((compressed, areas), axis=1).astype(np.float32)


def hit_eligible(lines, positions, sizes):
    low = positions - sizes / 2 - 10
    high = positions + sizes / 2 + 10
    return np.all(np.maximum(lines[:, 0], lines[:, 1]) > low, axis=1) & np.all(
        np.minimum(lines[:, 0], lines[:, 1]) < high, axis=1)


def hit_features(lines, positions, sizes):
    # Per-axis continuous segment parameter intervals. The classifier learns
    # whether they jointly intersect; no interval-overlap test is supplied.
    low = positions - sizes / 2 - 10
    high = positions + sizes / 2 + 10
    origin = lines[:, 0]
    direction = lines[:, 1] - origin
    parallel = abs(direction) < 1e-9
    den = np.where(parallel, 1., direction)
    left = (low - origin) / den
    right = (high - origin) / den
    lower = np.where(parallel, 0., np.maximum(0., np.minimum(left, right)))
    upper = np.where(parallel, 1., np.minimum(1., np.maximum(left, right)))
    return np.stack((lower[:, 0], upper[:, 0], lower[:, 1], upper[:, 1]), axis=1).astype(np.float32)


class Classifier(Ranker):
    def forward(self, features):
        return super().forward(np.asarray(features, dtype=np.float32))

    def probability(self, features):
        return sigmoid(self.forward(features)[0])


def load(path, untrained=False):
    with np.load(path, allow_pickle=False) as saved:
        metadata = json.loads(str(saved['metadata']))
        assert metadata['schema'] == SCHEMA and metadata['newTrainingUpdatesExecuted'] > 0
        models = {}
        for task in FEATURES:
            model = Classifier(features=FEATURES[task], hidden=24)
            model.p = {key: saved[(task + '__initial__' if untrained else task + '__') + key].copy() for key in KEYS}
            model.mean = saved[task + '__mean'].copy()
            model.scale = saved[task + '__scale'].copy()
            assert all(np.isfinite(value).all() for value in model.p.values())
            models[task] = model
    return models, metadata


class WorldProxy:
    def __init__(self, models):
        self.models = models

    def crosses(self, lines, left, right):
        a = lines[left]
        b = lines[right]
        possible = cross_eligible(a, b)
        values = np.zeros(len(left))
        ids = np.flatnonzero(possible)
        for first in range(0, len(ids), 1024):
            chosen = ids[first:first + 1024]
            values[chosen] = self.models['cross'].probability(cross_features(a[chosen], b[chosen]))
        return float(values.sum())

    def hits(self, lines, positions, sizes, edges, line_ids, node_ids):
        valid = np.all(edges[line_ids] != node_ids[:, None], axis=1)
        a = lines[line_ids]
        p = positions[node_ids]
        s = sizes[node_ids]
        valid &= hit_eligible(a, p, s)
        values = np.zeros(len(line_ids))
        ids = np.flatnonzero(valid)
        for first in range(0, len(ids), 1024):
            chosen = ids[first:first + 1024]
            values[chosen] = self.models['hit'].probability(hit_features(a[chosen], p[chosen], s[chosen]))
        return float(values.sum())

    def delta(self, before, after, sizes, edges):
        old_positions, old_lines = before
        new_positions, new_lines = after
        changed_edges = np.flatnonzero(np.any(new_lines != old_lines, axis=(1, 2)))
        changed_nodes = np.flatnonzero(np.any(new_positions != old_positions, axis=1))
        assert len(changed_nodes) == 1
        left = np.repeat(changed_edges, len(old_lines))
        right = np.tile(np.arange(len(old_lines)), len(changed_edges))
        changed = np.zeros(len(old_lines), dtype=bool)
        changed[changed_edges] = True
        keep = (left != right) & ((~changed[right]) | (left < right))
        left = left[keep]
        right = right[keep]
        cross_before = self.crosses(old_lines, left, right)
        cross_after = self.crosses(new_lines, left, right)
        line_ids = np.r_[np.repeat(changed_edges, len(sizes)), np.repeat(np.flatnonzero(~changed), len(changed_nodes))]
        node_ids = np.r_[np.tile(np.arange(len(sizes)), len(changed_edges)),
                         np.tile(changed_nodes, np.count_nonzero(~changed))]
        hit_before = self.hits(old_lines, old_positions, sizes, edges, line_ids, node_ids)
        hit_after = self.hits(new_lines, new_positions, sizes, edges, line_ids, node_ids)
        return {'predictedDelta': cross_after + hit_after - cross_before - hit_before,
                'predictedCrossDelta': cross_after - cross_before,
                'predictedHitDelta': hit_after - hit_before,
                'changedEdges': len(changed_edges), 'changedNodes': len(changed_nodes)}
