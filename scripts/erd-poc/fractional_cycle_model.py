"""An action-conditioned cycle critic with source and actual-motion features."""
import json
import numpy as np
from directed_cycle_model import CycleCritic, KEYS, cycle_features

SCHEMA = 'fraction-conditioned-cycle-mean-moment-v1-3x141'


def expand_actions(core, nodes, decoder, cycles, fractions):
    fractions = np.asarray(fractions, dtype=float)
    assert fractions.shape == (len(cycles),) and ((fractions > 0) & (fractions <= 1)).all()
    assert core.shape == (len(cycles), 3, 136)
    # Native scale feature is observed to twelve significant digits. Reject a
    # saturated log scale rather than silently reconstructing a wrong motion.
    assert (nodes[:, 62] < 8-1e-10).all()
    scales = 512*np.expm1(nodes[:, 62])
    assert (scales >= 512-1e-7).all()
    delta = fractions[:, None, None]*(decoder.positions[np.roll(cycles, -1, axis=1)]-decoder.positions[cycles])
    delta = np.copysign(np.floor(abs(delta)*100+.5), delta)/100
    motion = np.clip(delta/scales[cycles, None], -8, 8)
    fraction = np.broadcast_to(fractions[:, None, None], (len(cycles), 3, 1))
    extra = np.concatenate([fraction, motion, np.roll(motion, -1, axis=1)], axis=2)
    return np.concatenate([core, extra], axis=2)


def action_features(nodes, decoder, cycles, fractions, overview):
    core, error = cycle_features(nodes, decoder, cycles, overview)
    return expand_actions(core, nodes, decoder, cycles, fractions), error


class FractionCritic(CycleCritic):
    def __init__(self, parent):
        self.p = {key: value.copy() for key, value in parent.p.items()}
        self.p['w1'] = np.concatenate([self.p['w1'], np.zeros((5, self.p['w1'].shape[1]))])
        self.mean = np.r_[parent.mean, 1., np.zeros(4)]
        self.scale = np.r_[parent.scale, np.ones(5)]


def load(path):
    with np.load(path, allow_pickle=False) as saved:
        metadata = json.loads(str(saved['metadata']))
        assert metadata['schema'] == SCHEMA and metadata['selectedCheckpointUpdates'] > 0
        model = object.__new__(FractionCritic)
        model.p = {key: saved[key].copy() for key in KEYS}
        model.mean = saved['mean'].copy(); model.scale = saved['scale'].copy()
    assert model.mean.shape == model.scale.shape == (141,)
    assert model.p['w1'].shape == (141, 64) and model.p['wh'].shape == (128, 32)
    assert all(np.isfinite(value).all() for value in model.p.values())
    assert np.isfinite(model.mean).all() and np.isfinite(model.scale).all() and (model.scale > 0).all()
    return model, metadata
