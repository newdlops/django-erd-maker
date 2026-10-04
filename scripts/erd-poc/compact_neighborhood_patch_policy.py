"""NN translates a source-constrained patch and rebuilds affected complete stars."""
import numpy as np
from compact_source_port_policy import SourcePortPatchActor
from joint_neural_ports import perimeter_phase


class NeighborhoodPatchAnchorPolicy(SourcePortPatchActor):
    def __init__(self, features, decoder, active, max_step, seed, hops=2):
        super().__init__(features, decoder, active, max_step, seed)
        assert 1 <= hops <= 3
        features = np.asarray(features, dtype=np.float32)
        hidden = np.tanh(np.tanh(np.clip((features - self.mean) / self.scale, -8, 8)) @ self.w1) @ self.w2
        all_embedding = np.tanh(hidden)
        assert np.max(abs(all_embedding[self.active] - self.embedding)) < 1e-12
        all_embedding[self.active] = self.embedding
        neighbors = [set() for _ in decoder.positions]
        for first, second in decoder.provider.owner_edges:
            if first != second:
                neighbors[first].add(int(second))
                neighbors[second].add(int(first))
        ptr, ids = [0], []
        for row in neighbors:
            ids.extend(sorted(row))
            ptr.append(len(ids))
        self.buffers.update(all_embedding=all_embedding, neighbor_ptr=np.array(ptr, dtype=np.int32),
            neighbor_ids=np.array(ids, dtype=np.int32), neighborhood_hops=np.array(hops, dtype=np.int32))

    def forward(self, features):
        action, _ = super().forward(features)
        n = len(self.decoder.positions)
        delta = action[:n]
        moved = np.flatnonzero(np.any(delta != 0., axis=1))
        if not len(moved):
            return action, dict(movedOwners=0, affectedOwners=0)
        provider, buffers = self.decoder.provider, self.buffers
        affected = np.zeros(n, dtype=bool)
        affected[moved] = True
        frontier = set(map(int, moved))
        for _ in range(int(buffers['neighborhood_hops'])):
            following = set()
            for node in frontier:
                start, end = buffers['neighbor_ptr'][node:node + 2]
                following.update(map(int, buffers['neighbor_ids'][start:end]))
            frontier = {node for node in following if not affected[node]}
            affected[list(frontier)] = True
        fraction = np.tanh(buffers['all_embedding'] @ self.p['wo'] + self.p['bo'])
        fraction[~affected] = 0.
        full = self.decoder.positions[provider.owner] + provider.offsets + delta[provider.owner]
        offsets = .2 * provider.sizes * fraction[provider.owner]
        ends = (full + offsets)[provider.full_edges]
        mask = affected[provider.owner_edges]
        original = provider.original_ports + delta[provider.owner_edges]
        destination = np.where(mask[:, ::-1, None], ends[:, ::-1], original[:, ::-1])
        direction = destination - ends
        residue = provider.port_offsets - provider.base_boundary
        local = offsets[provider.full_edges] - residue
        half = provider.endpoint_sizes / 2
        denominator = np.where(abs(direction) > 1e-12, direction, 1.)
        sides = np.where(direction >= 0, half, -half)
        parameter = np.min(np.where(abs(direction) > 1e-12, (sides - local) / denominator, np.inf), axis=2)
        assert np.isfinite(parameter).all() and (parameter > 0).all()
        boundary = local + parameter[:, :, None] * direction
        phases = np.mod(perimeter_phase(boundary, provider.endpoint_sizes) - provider.phase + .5, 1.) - .5
        phases[~mask] = 0.
        action[n:] = phases
        return action, dict(movedOwners=len(moved), affectedOwners=int(affected.sum()))
