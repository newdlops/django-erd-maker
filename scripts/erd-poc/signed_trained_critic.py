"""Load a critic whose selected weights have learned negative cycle outcomes."""
import json
import numpy as np
from directed_cycle_model import KEYS
from fractional_cycle_model import FractionCritic
from signed_cycle_model import SCHEMA


def load(path):
    with np.load(path, allow_pickle=False) as saved:
        metadata = json.loads(str(saved['metadata']))
        assert metadata['schema'] == SCHEMA and metadata['kind'] == 'signed-synthetic-graph-cycle-critic-v1'
        assert metadata['selectedCheckpointUpdates'] > 0 and metadata['negativeTrainingRows'] > 0
        model = object.__new__(FractionCritic)
        model.p = {key: saved[key].copy() for key in KEYS}
        model.mean = saved['mean'].copy(); model.scale = saved['scale'].copy()
    assert model.mean.shape == model.scale.shape == (141,)
    assert model.p['w1'].shape == (141, 64) and model.p['wh'].shape == (128, 32)
    assert all(np.isfinite(value).all() for value in model.p.values())
    assert np.isfinite(model.mean).all() and np.isfinite(model.scale).all() and (model.scale > 0).all()
    return model, metadata
