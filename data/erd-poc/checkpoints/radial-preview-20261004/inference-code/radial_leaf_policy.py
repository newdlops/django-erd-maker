"""NN translates single-relationship cards toward their source peer endpoint.

Ray directions, original card-relative ports, frame and pair separation bounds
are immutable source decoding data. Nine shared NN parameters predict scalar
fractions. No coordinate is a parameter; no future geometry metric, rejection,
coordinate search or repair enters inference.
"""
import json
import numpy as np


def radial_buffers(decoder, max_step):
    assert 0 < max_step <= 4096
    provider = decoder.provider
    n = len(decoder.positions)
    counts = np.bincount(provider.owner_edges.ravel(), minlength=n)
    eligible = counts == 1
    velocity = np.zeros((n, 2))
    incident = np.full(n, -1, dtype=np.int32)
    for index, owners in enumerate(provider.owner_edges):
        for end in (0, 1):
            node, peer = int(owners[end]), int(owners[1 - end])
            if not eligible[node]:
                continue
            # For a two-card component only the larger source id is movable.
            if counts[peer] == 1 and node < peer:
                eligible[node] = False
                continue
            velocity[node] = provider.original_ports[index, 1 - end] - provider.original_ports[index, end]
            incident[node] = index
    velocity[~eligible] = 0.
    magnitude = np.max(abs(velocity), axis=1)
    limits = np.where(eligible, np.minimum(.95,
        np.divide(max_step, magnitude, out=np.zeros(n), where=magnitude > 1e-9)), 0.)
    positions, sizes = decoder.positions, decoder.sizes
    frame_low = (positions - sizes / 2).min(0)
    frame_high = (positions + sizes / 2).max(0)
    for node in np.flatnonzero(eligible):
        v = velocity[node]
        for axis in (0, 1):
            if v[axis] > 1e-12:
                room = frame_high[axis] - sizes[node, axis] / 2 - positions[node, axis]
                limits[node] = min(limits[node], max(0., room - .02) / v[axis])
            elif v[axis] < -1e-12:
                room = positions[node, axis] - sizes[node, axis] / 2 - frame_low[axis]
                limits[node] = min(limits[node], max(0., room - .02) / -v[axis])
        others = np.flatnonzero(np.arange(n) != node)
        delta = positions[others] - positions[node]
        gap = abs(delta) - (sizes[others] + sizes[node]) / 2 - [55.99, 41.99]
        allowed = gap >= -1e-8
        assert allowed.any(axis=1).all()
        sign = np.sign(delta)
        own_closing = np.maximum(0., sign * v)
        peer_closing = np.maximum(0., -sign * velocity[others])
        total = own_closing + peer_closing
        safe_gap = np.maximum(0., gap - .03)
        ratio = np.divide(safe_gap, total, out=np.full_like(gap, np.inf), where=total > 1e-12)
        axis = np.argmax(np.where(allowed, ratio, -np.inf), axis=1)
        row = np.arange(len(others))
        closing = own_closing[row, axis]
        count = (own_closing[row, axis] > 1e-12).astype(int) + (peer_closing[row, axis] > 1e-12).astype(int)
        constrained = closing > 1e-12
        if constrained.any():
            budget = safe_gap[row, axis][constrained] / (closing[constrained] * count[constrained])
            limits[node] = min(limits[node], float(budget.min()))
    assert (limits >= 0).all() and (limits <= .95).all()
    return dict(eligible=eligible, velocity=velocity, fraction_limit=limits,
        source_incident_edge=incident, max_step=np.array(max_step), frame_low=frame_low, frame_high=frame_high)


class RadialLeafPolicy:
    def __init__(self, features, decoder, seed, max_step):
        self.decoder = decoder
        features = np.asarray(features, dtype=np.float32)
        assert features.shape == (len(decoder.positions), 64)
        self.mean = features.mean(0, dtype=np.float64)
        self.scale = np.maximum(.2, features.std(0, dtype=np.float64))
        rng = np.random.default_rng(seed)
        self.w1 = rng.normal(0, 1 / np.sqrt(64), (64, 12))
        self.w2 = rng.normal(0, 1 / np.sqrt(12), (12, 8))
        hidden = np.tanh(np.tanh(np.clip((features - self.mean) / self.scale, -8, 8)) @ self.w1) @ self.w2
        self.embedding = np.tanh(hidden)
        self.buffers = radial_buffers(decoder, max_step)
        self.p = dict(wo=np.zeros((8, 1)), bo=np.zeros(1))

    def forward(self, features):
        fraction = np.maximum(0., np.tanh(self.embedding @ self.p['wo'] + self.p['bo'])[:, 0])
        delta = self.buffers['velocity'] * (fraction * self.buffers['fraction_limit'])[:, None]
        delta = np.copysign(np.floor(abs(delta) * 100 + .5), delta) / 100
        return np.concatenate([delta, np.zeros_like(self.decoder.provider.phase)]), dict(
            movedOwners=int(np.any(delta != 0, axis=1).sum()),
            maximumDisplacementPixels=float(abs(delta).max()),
            eligibleOwners=int(self.buffers['eligible'].sum()),
            positiveCapacityOwners=int(np.count_nonzero(self.buffers['fraction_limit'] > 0)))

    def save(self, path, metadata):
        np.savez_compressed(path, **self.p, embedding=self.embedding, w1=self.w1, w2=self.w2,
            mean=self.mean, scale=self.scale, **self.buffers, metadata=np.array(json.dumps(metadata)))
