"""Shared directed-edge encoder with a nonlinear cycle interaction head."""
import json
from pathlib import Path
import numpy as np
from learn_pair_policy import KEYS as PAIR_KEYS
from full_context_pair_model import expand
from run_anchor_pair_policy import pair_features

SCHEMA = 'directed-cycle-mean-moment-v1-3x136'
KEYS = PAIR_KEYS + ('wh', 'bh', 'oh', 'ob')


def cycle_features(nodes, decoder, vocabulary, overview):
    directed = np.stack([vocabulary, np.roll(vocabulary, -1, axis=1)], axis=-1).reshape(-1, 2)
    base, error = pair_features(nodes, decoder, directed, overview)
    features = expand(base, nodes, directed).reshape(len(vocabulary), 3, 136)
    return features, error


class CycleCritic:
    def __init__(self, parent, seed=84023, head_hidden=32):
        hidden = parent.p['wo'].size; rng = np.random.default_rng(seed)
        self.p = {key: value.copy() for key, value in parent.p.items()}
        self.p |= {'wh': rng.normal(0, 1/np.sqrt(2*hidden), (2*hidden, head_hidden)),
                   'bh': np.zeros(head_hidden), 'oh': np.zeros(head_hidden), 'ob': np.zeros(1)}
        self.mean = parent.mean.copy(); self.scale = parent.scale.copy()

    def forward(self, features):
        assert features.ndim == 3 and features.shape[1] == 3
        batch = len(features); hidden = self.p['wo'].size
        x = np.clip((features-self.mean)/self.scale, -8, 8).reshape(-1, len(self.mean))
        h1 = np.tanh(x@self.p['w1']+self.p['b1'])
        h2 = np.tanh(h1@self.p['w2']+self.p['b2']).reshape(batch, 3, hidden)
        mean = h2.mean(1); moments = np.concatenate([mean, (h2*h2).mean(1)], axis=1)
        head = np.tanh(moments@self.p['wh']+self.p['bh'])
        score = mean@self.p['wo']+self.p['bo']+head@self.p['oh']+self.p['ob']
        return score, (x, h1, h2, mean, moments, head)

    def loss(self, features, targets, gradient=False):
        predicted, (x, h1, h2, mean, moments, head) = self.forward(features)
        error = predicted-targets
        loss = float(np.mean(np.where(abs(error) <= 1, .5*error**2, abs(error)-.5)))
        if not gradient:
            return loss
        dz = np.clip(error, -1, 1)/len(error)
        dhead = dz[:, None]*self.p['oh'][None, :]*(1-head*head)
        dmoments = dhead@self.p['wh'].T; hidden = mean.shape[1]
        dmean = dz[:, None]*self.p['wo'][None, :]+dmoments[:, :hidden]
        dsecond = dmoments[:, hidden:]
        d2 = ((dmean[:, None, :]+2*h2*dsecond[:, None, :])/3)*(1-h2*h2)
        d2 = d2.reshape(-1, hidden)
        d1 = (d2@self.p['w2'].T)*(1-h1*h1)
        gradients = {'w1': x.T@d1, 'b1': d1.sum(0), 'w2': h1.T@d2, 'b2': d2.sum(0),
            'wo': mean.T@dz, 'bo': np.array([dz.sum()]), 'wh': moments.T@dhead, 'bh': dhead.sum(0),
            'oh': head.T@dz, 'ob': np.array([dz.sum()])}
        return loss, gradients


def load(path):
    with np.load(path, allow_pickle=False) as saved:
        metadata = json.loads(str(saved['metadata']))
        assert metadata['schema'] == SCHEMA and metadata['selectedCheckpointUpdates'] > 0
        model = object.__new__(CycleCritic)
        model.p = {key: saved[key].copy() for key in KEYS}
        model.mean = saved['mean'].copy(); model.scale = saved['scale'].copy()
    assert model.mean.shape == model.scale.shape == (136,)
    assert model.p['w1'].shape == (136, 64) and model.p['wh'].shape == (128, 32)
    assert all(np.isfinite(value).all() for value in model.p.values())
    assert np.isfinite(model.mean).all() and np.isfinite(model.scale).all() and (model.scale > 0).all()
    return model, metadata
