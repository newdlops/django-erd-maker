"""Versioned signed cycle features and a deterministic geometric decoder.

Positive values are byte-compatible with the archived fractional primitive.
Negative values are new actions; an inherited critic is not trained on them.
"""
import numpy as np
from directed_cycle_model import cycle_features
from run_anchor_pair_walk import array_hash

SCHEMA = 'signed-fraction-conditioned-cycle-mean-moment-v1-3x141'
FRACTIONS = np.array([-.001, -.005, -.02, -.1, -.4])


def expand_actions(core, nodes, decoder, cycles, fractions):
    cycles = np.asarray(cycles, dtype=np.int32)
    fractions = np.asarray(fractions, dtype=float)
    assert cycles.shape == (len(fractions), 3)
    assert np.isfinite(fractions).all() and ((abs(fractions) > 0) & (abs(fractions) <= 1)).all()
    assert core.shape == (len(cycles), 3, 136)
    assert (nodes[:, 62] < 8-1e-10).all()
    scales = 512*np.expm1(nodes[:, 62]); assert (scales >= 512-1e-7).all()
    delta = fractions[:, None, None]*(decoder.positions[np.roll(cycles, -1, axis=1)]-decoder.positions[cycles])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    motion = np.clip(delta/scales[cycles, None], -8, 8)
    extra = np.concatenate([np.broadcast_to(fractions[:, None, None], (len(cycles), 3, 1)),
                            motion, np.roll(motion, -1, axis=1)], axis=2)
    return np.concatenate([core, extra], axis=2)


def signed_action(decoder, cycle, fraction):
    assert np.isfinite(fraction) and 0 < abs(fraction) <= 1
    cycle = np.asarray(cycle, dtype=np.int32)
    assert cycle.shape == (3,) and len(set(map(int, cycle))) == 3
    assert (cycle >= 0).all() and (cycle < len(decoder.positions)).all()
    assert all(np.array_equal(decoder.sizes[cycle[0]], decoder.sizes[n]) for n in cycle)
    delta = np.zeros_like(decoder.positions)
    delta[cycle] = fraction*(decoder.positions[np.roll(cycle, -1)]-decoder.positions[cycle])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    phases, _ = decoder.endpoint_offsets(delta)
    return np.concatenate([delta, phases])


def rank(model, decoder, nodes, cycles, overview, fractions=FRACTIONS, chunk=256):
    scores = np.empty((len(cycles), len(fractions))); hashes = []; maximum = 0
    for offset in range(0, len(cycles), chunk):
        selected = cycles[offset:offset+chunk]
        core, error = cycle_features(nodes, decoder, selected, overview); maximum = max(maximum, error)
        group = []
        for column, fraction in enumerate(fractions):
            features = expand_actions(core, nodes, decoder, selected, np.full(len(selected), fraction))
            scores[offset:offset+len(selected), column] = model.forward(features)[0]
            group.append(array_hash(features))
        hashes.append(group)
    assert np.isfinite(scores).all()
    return scores, hashes, maximum
