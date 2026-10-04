"""NN-ranked equal-size slots and one NN interior anchor per original card.

The decoder keeps the source rectangle multiset and original compound members.
All rays use common original-card anchors, rather than per-edge source rays.
No future geometry costs, Native feedback, search or repairs enter inference.
"""
import json
import numpy as np
from joint_neural_ports import perimeter_phase


def source_slots(positions, sizes):
    normalized = (positions - positions.min(0)) / np.maximum(1., np.ptp(positions, axis=0))
    integer = np.floor(normalized * 65535).astype(np.uint64)
    morton = np.zeros(len(positions), dtype=np.uint64)
    for bit in range(16):
        morton |= ((integer[:, 0] >> bit) & 1) << (2 * bit)
        morton |= ((integer[:, 1] >> bit) & 1) << (2 * bit + 1)
    groups = {}
    for index, size in enumerate(sizes):
        groups.setdefault(tuple(size), []).append(index)
    order, bounds = [], [0]
    rank = np.zeros(len(positions))
    for shape in sorted(groups):
        members = np.array(groups[shape], dtype=np.int32)
        members = members[np.argsort(morton[members], kind='stable')]
        rank[members] = np.linspace(-1., 1., len(members)) if len(members) > 1 else 0.
        order.extend(members.tolist())
        bounds.append(len(order))
    return dict(slot_origin=positions.copy(), slot_order=np.array(order, dtype=np.int32),
                group_bounds=np.array(bounds, dtype=np.int32), rank_origin=rank,
                rank_span=np.array(4.), anchor_span=np.array(.2))


class CommonSlotAnchorPolicy:
    def __init__(self, features, decoder, seed):
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
        self.p = dict(so=np.zeros((8, 1)), sb=np.zeros(1), wo=np.zeros((8, 2)), bo=np.zeros(2))
        self.buffers = source_slots(decoder.positions, decoder.sizes)

    def forward(self, features):
        buffers, provider = self.buffers, self.decoder.provider
        logits = buffers['rank_origin'] + float(buffers['rank_span']) * np.tanh(
            self.embedding @ self.p['so'] + self.p['sb'])[:, 0]
        permutation = np.arange(len(self.decoder.positions), dtype=np.int32)
        for start, end in zip(buffers['group_bounds'][:-1], buffers['group_bounds'][1:]):
            slots = buffers['slot_order'][start:end]
            nodes = slots[np.argsort(logits[slots], kind='stable')]
            permutation[nodes] = slots
        assert np.array_equal(self.decoder.sizes[permutation], self.decoder.sizes)
        delta = buffers['slot_origin'][permutation] - buffers['slot_origin']
        delta = np.copysign(np.floor(abs(delta) * 100 + .5), delta) / 100
        fraction = np.tanh(self.embedding @ self.p['wo'] + self.p['bo'])
        full = self.decoder.positions[provider.owner] + provider.offsets + delta[provider.owner]
        offsets = float(buffers['anchor_span']) * provider.sizes * fraction[provider.owner]
        anchors = full + offsets
        ends = anchors[provider.full_edges]
        direction = ends[:, ::-1] - ends
        residue = provider.port_offsets - provider.base_boundary
        local = offsets[provider.full_edges] - residue
        half = provider.endpoint_sizes / 2
        nonzero = abs(direction) > 1e-12
        denominator = np.where(nonzero, direction, 1.)
        sides = np.where(direction >= 0, half, -half)
        parameter = np.min(np.where(nonzero, (sides - local) / denominator, np.inf), axis=2)
        assert np.isfinite(parameter).all() and (parameter > 0).all()
        boundary = local + parameter[:, :, None] * direction
        phase = perimeter_phase(boundary, provider.endpoint_sizes)
        phases = np.mod(phase - provider.phase + .5, 1.) - .5
        return np.concatenate([delta, phases]), dict(permutation=permutation, logits=logits,
            movedOwners=int(np.count_nonzero(permutation != np.arange(len(permutation)))))

    def save(self, path, metadata):
        np.savez_compressed(path, **self.p, embedding=self.embedding, w1=self.w1, w2=self.w2,
            mean=self.mean, scale=self.scale, **self.buffers, metadata=np.array(json.dumps(metadata)))
