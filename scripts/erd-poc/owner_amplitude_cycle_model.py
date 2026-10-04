"""Cycle features with sign-or-zero amplitudes for each of three owners."""
from itertools import product
import json
import numpy as np
from directed_cycle_model import cycle_features, KEYS
from fractional_cycle_model import FractionCritic, load as load_positive, SCHEMA as POSITIVE_SCHEMA
from signed_cycle_model import SCHEMA as SIGNED_SCHEMA
from signed_trained_critic import load as load_signed
from run_anchor_pair_walk import array_hash

SCHEMA = 'owner-amplitude-cycle-mean-moment-v1-3x141'
MAGNITUDES = np.array([.001, .005, .02, .1, .4])
SIGNS = np.array([p for p in product([-1, 0, 1], repeat=3)
                  if np.count_nonzero(p) >= 2 and len(set(p)) > 1], dtype=np.int32)
AMPLITUDES = np.concatenate([SIGNS*value for value in MAGNITUDES])
assert SIGNS.shape == (18, 3) and AMPLITUDES.shape == (90, 3)


def expand_actions(core, nodes, decoder, cycles, amplitudes):
    cycles = np.asarray(cycles, dtype=np.int32); amplitudes = np.asarray(amplitudes, dtype=float)
    assert cycles.shape == amplitudes.shape == (len(cycles), 3)
    assert np.isfinite(amplitudes).all() and (abs(amplitudes) <= 1).all()
    assert core.shape == (len(cycles), 3, 136) and (nodes[:, 62] < 8-1e-10).all()
    scales = 512*np.expm1(nodes[:, 62]); assert (scales >= 512-1e-7).all()
    delta = amplitudes[:, :, None]*(decoder.positions[np.roll(cycles, -1, axis=1)]-decoder.positions[cycles])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    motion = np.clip(delta/scales[cycles, None], -8, 8)
    extra = np.concatenate([amplitudes[:, :, None], motion, np.roll(motion, -1, axis=1)], axis=2)
    return np.concatenate([core, extra], axis=2)


def action_features(nodes, decoder, cycles, amplitudes, overview):
    core, error = cycle_features(nodes, decoder, cycles, overview)
    return expand_actions(core, nodes, decoder, cycles, amplitudes), error


def owner_action(decoder, cycle, amplitudes):
    cycle = np.asarray(cycle, dtype=np.int32); amplitudes = np.asarray(amplitudes, dtype=float)
    assert cycle.shape == amplitudes.shape == (3,) and len(set(map(int, cycle))) == 3
    assert np.isfinite(amplitudes).all() and (abs(amplitudes) <= 1).all()
    assert (cycle >= 0).all() and (cycle < len(decoder.positions)).all()
    assert all(np.array_equal(decoder.sizes[cycle[0]], decoder.sizes[n]) for n in cycle)
    delta = np.zeros_like(decoder.positions)
    delta[cycle] = amplitudes[:, None]*(decoder.positions[np.roll(cycle, -1)]-decoder.positions[cycle])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    phases, _ = decoder.endpoint_offsets(delta)
    return np.concatenate([delta, phases])


def rank(model, decoder, nodes, cycles, overview, chunk=128):
    scores = np.empty((len(cycles), len(AMPLITUDES))); hashes = []; maximum = 0
    for offset in range(0, len(cycles), chunk):
        selected = cycles[offset:offset+chunk]
        core, error = cycle_features(nodes, decoder, selected, overview); maximum = max(maximum, error)
        group = []
        for column, amplitude in enumerate(AMPLITUDES):
            features = expand_actions(core, nodes, decoder, selected, np.broadcast_to(amplitude, (len(selected), 3)))
            scores[offset:offset+len(selected), column] = model.forward(features)[0]
            group.append(array_hash(features))
        hashes.append(group)
    assert np.isfinite(scores).all()
    return scores, hashes, maximum


def load(path):
    with np.load(path, allow_pickle=False) as saved:
        metadata = json.loads(str(saved['metadata']))
    if metadata['schema'] == POSITIVE_SCHEMA:
        return load_positive(path)
    if metadata['schema'] == SIGNED_SCHEMA:
        return load_signed(path)
    assert metadata['schema'] == SCHEMA and metadata['kind'] == 'owner-amplitude-synthetic-cycle-critic-v1'
    assert metadata['selectedCheckpointUpdates'] > 0 and metadata['nonuniformTrainingRows'] > 0
    with np.load(path, allow_pickle=False) as saved:
        model = object.__new__(FractionCritic); model.p = {key: saved[key].copy() for key in KEYS}
        model.mean = saved['mean'].copy(); model.scale = saved['scale'].copy()
    assert model.mean.shape == model.scale.shape == (141,)
    assert model.p['w1'].shape == (141, 64) and model.p['wh'].shape == (128, 32)
    assert all(np.isfinite(v).all() for v in model.p.values())
    assert np.isfinite(model.mean).all() and np.isfinite(model.scale).all() and (model.scale > 0).all()
    return model, metadata
