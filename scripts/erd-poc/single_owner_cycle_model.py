"""Neural single-owner moves with unchanged three-row feature semantics."""
from itertools import combinations
from pathlib import Path
import json
import numpy as np
from directed_cycle_model import cycle_features, KEYS
from float32_input_owner_model import Float32InputCritic, load as parent_load
from owner_amplitude_cycle_model import SCHEMA, expand_actions
from run_anchor_pair_walk import array_hash
from mixed_size_owner_cycles import mixed_action

ACTION_SCHEMA = 'single-owner-partial-cycle-action-v1'
MODEL_KIND = 'single-owner-partial-cycle-critic-v1'
MAGNITUDES = np.array([.00025, .0005, .001, .0025, .005, .01, .02, .05, .1, .2, .4])
AMPLITUDES = np.array([np.eye(3)[owner]*sign*magnitude
    for magnitude in MAGNITUDES for owner in range(3) for sign in [-1, 1]])
assert AMPLITUDES.shape == (66, 3) and (np.count_nonzero(AMPLITUDES, axis=1) == 1).all()


def load(path):
    with np.load(path, allow_pickle=False) as saved: metadata = json.loads(str(saved['metadata']))
    if metadata.get('kind') != MODEL_KIND: return parent_load(path)
    assert metadata['schema'] == SCHEMA and metadata['actionSchema'] == ACTION_SCHEMA
    assert metadata['selectedCheckpointUpdates'] > 0 and metadata['singleOwnerTrainingRows'] > 0
    with np.load(path, allow_pickle=False) as saved:
        model = object.__new__(Float32InputCritic)
        model.p = {k: saved[k].copy() for k in KEYS}; model.mean = saved['mean'].copy(); model.scale = saved['scale'].copy()
    assert model.mean.shape == model.scale.shape == (141,) and all(np.isfinite(v).all() for v in model.p.values())
    assert (model.scale > 0).all() and np.isfinite(model.mean).all()
    return model, metadata


def vocabulary(decoder, nodes, seed, count):
    assert 1 <= count <= 1024
    active = np.any(nodes[:, 4:6] > 0, axis=1); indices = np.arange(len(nodes)); words = set()
    for owner in indices:
        other = indices[indices != owner]
        distance = np.rint(np.sum((decoder.positions[other]-decoder.positions[owner])**2, axis=1)*1e6)
        nearby = other[np.lexsort((other, distance))[:8]]
        for left, right in combinations(map(int, nearby), 2):
            word = tuple(sorted((int(owner), left, right)))
            if active[list(word)].any() and not decoder.irrelevant_isolate[list(word)].all(): words.add(word)
    words = np.array(sorted(words), dtype=np.int32); requested = min(count, len(words)); assert requested > 0
    rng = np.random.default_rng(seed); chosen = words[rng.choice(len(words), requested, replace=False)]
    cycles = np.empty((2*requested, 3), dtype=np.int32); cycles[::2] = chosen; cycles[1::2] = chosen[:, [0, 2, 1]]
    return cycles, {'kind': 'local all-size source triples with at least one directly conflicting owner',
        'eligibleUnorderedTriples': len(words), 'sampledUnorderedTriples': requested,
        'nearestNeighbors': 8, 'bothDirections': True, 'sameSizeTriplesAllowed': True,
        'sourceGeometryAndInitialObservationsOnly': True, 'coordinateProposalsBySupport': False}


def rank(model, decoder, nodes, cycles, overview, chunk=128):
    scores = np.empty((len(cycles), len(AMPLITUDES))); hashes = []; maximum = 0.
    for first in range(0, len(cycles), chunk):
        selected = cycles[first:first+chunk]; core, error = cycle_features(nodes, decoder, selected, overview)
        maximum = max(maximum, error); block = []
        for ai, a in enumerate(AMPLITUDES):
            x = expand_actions(core, nodes, decoder, selected, np.broadcast_to(a, (len(selected), 3)))
            scores[first:first+len(selected), ai] = model.forward(x)[0]; block.append(array_hash(x))
        hashes.append(block)
    assert np.isfinite(scores).all()
    return scores, hashes, maximum


def allowed_indices(nodes, cycles):
    active = np.any(nodes[:, 4:6] > 0, axis=1)
    moving_slots = np.argmax(abs(AMPLITUDES), axis=1)
    allowed = active[cycles[:, moving_slots]]
    return np.flatnonzero(allowed.ravel())


def motion_key(decoder, cycle, amplitude):
    slot = int(np.argmax(abs(amplitude))); owner = int(cycle[slot]); target = int(cycle[(slot+1)%3])
    raw = amplitude[slot]*(decoder.positions[target]-decoder.positions[owner])
    cents = np.copysign(np.floor(abs(raw)*100+.5), raw).astype(np.int64)
    return owner, int(cents[0]), int(cents[1])


def schedule(decoder, cycles, ordered, budget):
    selected = []; seen = set(); duplicates = zeros = 0
    for index in ordered:
        ci, ai = map(int, np.unravel_index(index, (len(cycles), len(AMPLITUDES))))
        key = motion_key(decoder, cycles[ci], AMPLITUDES[ai])
        if key[1:] == (0, 0): zeros += 1; continue
        if key in seen: duplicates += 1; continue
        seen.add(key); selected.append((ci, ai))
        if len(selected) == budget: break
    assert len(selected) == budget
    return np.array(selected, dtype=np.int32), {'duplicateSingleOwnerDeltasExcluded': duplicates,
        'roundedZeroDeltasExcluded': zeros, 'uniqueOwnerDeltaTuples': len(seen),
        'nativeMeasurementsUsedToChooseSchedule': False}


def action(decoder, cycle, amplitude):
    assert np.count_nonzero(amplitude) == 1
    result = mixed_action(decoder, cycle, amplitude); key = motion_key(decoder, cycle, amplitude)
    moved = np.flatnonzero(np.any(result[:len(decoder.positions)] != 0, axis=1))
    assert moved.tolist() == [key[0]]
    assert np.array_equal(result[key[0]], np.array(key[1:])/100)
    return result
