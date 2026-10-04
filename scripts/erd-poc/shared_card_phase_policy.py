"""Nine shared NN weights rotate existing ports along their source card sides.

Original-card coordinates and dimensions remain immutable. Every endpoint on
the same original card receives one shared phase offset, retaining coincident
ties. Source-side limits are fixed before any learning or measurement. Native
metrics never enter inference, ranking, side selection or coordinate repair.
"""
import json
import numpy as np


def source_phase_limits(phases, sizes, edges, count, span):
    w, h = sizes[..., 0], sizes[..., 1]
    length = 2 * (w + h)
    t = np.mod(phases, 1.) * length
    corners = np.stack([np.zeros_like(w), w, w + h, 2 * w + h, length], axis=-1)
    side = (t >= w).astype(int) + (t >= w + h) + (t >= 2 * w + h)
    start = np.take_along_axis(corners, side[..., None], -1)[..., 0]
    end = np.take_along_axis(corners, (side + 1)[..., None], -1)[..., 0]
    corner = np.min(abs(t[..., None] - corners), axis=-1) <= .03
    negative = np.where(corner, 0., np.maximum(0., t - start - .02) / length)
    positive = np.where(corner, 0., np.maximum(0., end - t - .02) / length)
    bounds = np.full((2, count), span)
    np.minimum.at(bounds[0], edges.ravel(), negative.ravel())
    np.minimum.at(bounds[1], edges.ravel(), positive.ravel())
    assert np.isfinite(bounds).all() and (bounds >= 0).all() and (bounds <= span).all()
    return bounds


class SharedCardPhasePolicy:
    def __init__(self, features, decoder, seed, span):
        assert 0 < span <= .05
        self.decoder = decoder
        features = np.asarray(features, dtype=np.float32)
        assert features.shape == (len(decoder.positions), 64)
        self.mean = features.mean(0, dtype=np.float64)
        self.scale = np.maximum(.2, features.std(0, dtype=np.float64))
        rng = np.random.default_rng(seed)
        self.w1 = rng.normal(0, 1 / np.sqrt(64), (64, 12))
        self.w2 = rng.normal(0, 1 / np.sqrt(12), (12, 8))
        hidden = np.tanh(np.tanh(np.clip((features - self.mean) / self.scale, -8, 8)) @ self.w1) @ self.w2
        self.embedding = np.tanh(hidden)[decoder.provider.owner]
        provider = decoder.provider
        bounds = source_phase_limits(provider.phase, provider.endpoint_sizes,
            provider.full_edges, len(provider.owner), span)
        self.buffers = dict(phase_negative=bounds[0], phase_positive=bounds[1],
            full_edges=provider.full_edges.copy(), source_phase=provider.phase.copy(), phase_span=np.array(span))
        self.p = dict(wo=np.zeros((8, 1)), bo=np.zeros(1))

    def forward(self, features):
        fraction = np.tanh(self.embedding @ self.p['wo'] + self.p['bo'])[:, 0]
        phase = np.where(fraction >= 0, self.buffers['phase_positive'], self.buffers['phase_negative']) * fraction
        endpoints = phase[self.buffers['full_edges']]
        delta = np.zeros_like(self.decoder.positions)
        return np.concatenate([delta, endpoints]), dict(movedOwners=0,
            phaseChangedOriginalCards=int(np.count_nonzero(phase)),
            maximumPhaseOffset=float(abs(endpoints).max()),
            sourceCornersFixedOriginalCards=int(np.count_nonzero(
                (self.buffers['phase_positive'] == 0) & (self.buffers['phase_negative'] == 0))))

    def save(self, path, metadata):
        np.savez_compressed(path, **self.p, embedding=self.embedding, w1=self.w1, w2=self.w2,
            mean=self.mean, scale=self.scale, **self.buffers, metadata=np.array(json.dumps(metadata)))
