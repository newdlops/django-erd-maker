"""NN selects one equal-size swap and predicts anchors in its graph neighborhood.

The no-op retains every original endpoint. Affected stars use a common anchor;
rays facing an unchanged endpoint target that exact original endpoint. This is
a fixed decoder, with no objective, future metric, Native rejection or repair.
"""
import numpy as np
from compact_common_slot_policy import CommonSlotAnchorPolicy
from joint_neural_ports import perimeter_phase
from run_anchor_pair_policy import pair_features


def nearby_pairs(decoder, seed, limit=512, reach=2048.):
    rows = set()
    for group in decoder.groups:
        for first in group:
            remaining = group[group != first]
            distance = np.hypot(*(decoder.positions[remaining] - decoder.positions[first]).T)
            allowed = (distance <= reach) & ~(decoder.irrelevant_isolate[first] & decoder.irrelevant_isolate[remaining])
            choices = np.flatnonzero(allowed)
            choices = choices[np.lexsort((remaining[choices], distance[choices]))][:8]
            rows.update(tuple(sorted((int(first), int(remaining[row])))) for row in choices)
    ordered = np.array(sorted(rows), dtype=np.int32).reshape(-1, 2)
    assert len(ordered), 'source has no nearby equal-size eligible pair'
    if len(ordered) > limit:
        selected = np.sort(np.random.default_rng(seed + 700).choice(len(ordered), limit, replace=False))
        ordered = ordered[selected]
    return ordered


class CommonPairAnchorPolicy(CommonSlotAnchorPolicy):
    def __init__(self, features, decoder, seed, overview=False, pairs=None, hops=2):
        super().__init__(features, decoder, seed)
        assert 1 <= hops <= 3
        pairs = nearby_pairs(decoder, seed) if pairs is None else np.asarray(pairs, dtype=np.int32)
        assert np.array_equal(decoder.sizes[pairs[:, 0]], decoder.sizes[pairs[:, 1]])
        inputs, error = pair_features(features, decoder, pairs, overview)
        assert error < 1e-9
        mean = inputs.mean(0, dtype=np.float64)
        scale = np.maximum(.2, inputs.std(0, dtype=np.float64))
        hidden = np.tanh(np.tanh(np.clip((inputs - mean) / scale, -8, 8)) @ self.w1) @ self.w2
        self.p = dict(po=np.zeros((8, 1)), pb=np.zeros(1), wo=np.zeros((8, 2)), bo=np.zeros(2))
        neighbors = [set() for _ in decoder.positions]
        for first, second in decoder.provider.owner_edges:
            if first != second:
                neighbors[first].add(int(second))
                neighbors[second].add(int(first))
        ptr, ids = [0], []
        for row in neighbors:
            ids.extend(sorted(row))
            ptr.append(len(ids))
        self.buffers.update(pair_vocabulary=pairs, pair_mean=mean, pair_scale=scale,
            pair_embedding=np.tanh(hidden), neighbor_ptr=np.array(ptr, dtype=np.int32),
            neighbor_ids=np.array(ids, dtype=np.int32), neighborhood_hops=np.array(hops, dtype=np.int32))

    def forward(self, features):
        provider, buffers = self.decoder.provider, self.buffers
        scores = (buffers['pair_embedding'] @ self.p['po'] + self.p['pb'])[:, 0]
        selected = int(np.argmax(np.r_[0., scores])) - 1
        delta = np.zeros_like(self.decoder.positions)
        if selected < 0:
            return np.concatenate([delta, np.zeros_like(provider.phase)]), dict(movedOwners=0, selectedPair=-1, affectedOwners=0)
        first, second = buffers['pair_vocabulary'][selected]
        delta[first] = self.decoder.positions[second] - self.decoder.positions[first]
        delta[second] = -delta[first]
        delta = np.copysign(np.floor(abs(delta) * 100 + .5), delta) / 100
        affected = np.zeros(len(delta), dtype=bool)
        affected[[first, second]] = True
        frontier = {int(first), int(second)}
        for _ in range(int(buffers['neighborhood_hops'])):
            following = set()
            for node in frontier:
                start, end = buffers['neighbor_ptr'][node:node + 2]
                following.update(map(int, buffers['neighbor_ids'][start:end]))
            frontier = {node for node in following if not affected[node]}
            affected[list(frontier)] = True
        fraction = np.tanh(self.embedding @ self.p['wo'] + self.p['bo'])
        fraction[~affected] = 0.
        full = self.decoder.positions[provider.owner] + provider.offsets + delta[provider.owner]
        offsets = float(buffers['anchor_span']) * provider.sizes * fraction[provider.owner]
        anchors = full + offsets
        ends = anchors[provider.full_edges]
        mask = affected[provider.owner_edges]
        original = provider.original_ports + delta[provider.owner_edges]
        destination = np.where(mask[:, ::-1, None], ends[:, ::-1], original[:, ::-1])
        direction = destination - ends
        residue = provider.port_offsets - provider.base_boundary
        local = offsets[provider.full_edges] - residue
        half = provider.endpoint_sizes / 2
        nonzero = abs(direction) > 1e-12
        denominator = np.where(nonzero, direction, 1.)
        sides = np.where(direction >= 0, half, -half)
        parameter = np.min(np.where(nonzero, (sides - local) / denominator, np.inf), axis=2)
        assert np.isfinite(parameter).all() and (parameter > 0).all()
        boundary = local + parameter[:, :, None] * direction
        phases = np.mod(perimeter_phase(boundary, provider.endpoint_sizes) - provider.phase + .5, 1.) - .5
        phases[~mask] = 0.
        return np.concatenate([delta, phases]), dict(movedOwners=2, selectedPair=selected, affectedOwners=int(affected.sum()))
