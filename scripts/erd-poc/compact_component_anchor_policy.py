"""Shared NN body and anchor heads with closed source graph components.

The immutable source line directions define an interior anchor basis. Least
squares reconstructs that basis once from existing lines; it never observes a
future pose or its visual cost. NN residuals then predict every anchor in the
selected source components. Whole components retain complete stars on both
sides of each changed line. Unselected components retain all original ports.
"""
import numpy as np
from compact_source_port_policy import SourcePortPatchActor
from joint_neural_ports import perimeter_phase


def source_anchor_basis(provider):
    count = len(provider.owner)
    matrix = np.zeros((count, 2, 2))
    rhs = np.zeros((count, 2))
    direction = provider.original_ports[:, 1] - provider.original_ports[:, 0]
    length = np.hypot(direction[:, 0], direction[:, 1])
    assert (length > 1e-9).all()
    normal = np.stack([-direction[:, 1], direction[:, 0]], axis=1) / length[:, None]
    outer = normal[:, :, None] * normal[:, None, :]
    for end in (0, 1):
        ids = provider.full_edges[:, end]
        local = provider.port_offsets[:, end]
        np.add.at(matrix, ids, outer)
        np.add.at(rhs, ids, normal * np.sum(normal * local, axis=1)[:, None])
    # Minimum-norm source reconstruction keeps degree-one null directions zero.
    eigenvalues, eigenvectors = np.linalg.eigh(matrix)
    inverse = np.divide(1., eigenvalues, out=np.zeros_like(eigenvalues), where=eigenvalues > 1e-9)
    projected = np.einsum('nji,nj->ni', eigenvectors, rhs) * inverse
    local = np.einsum('nij,nj->ni', eigenvectors, projected)
    normalized = np.clip(local / (.4 * provider.sizes), -.975, .975)
    return np.arctanh(normalized)


def selected_components(decoder, active):
    neighbors = [set() for _ in decoder.positions]
    for first, second in decoder.provider.owner_edges:
        if first != second:
            neighbors[first].add(int(second))
            neighbors[second].add(int(first))
    affected = np.zeros(len(neighbors), dtype=bool)
    stack = list(map(int, active))
    affected[stack] = True
    while stack:
        current = stack.pop()
        following = [node for node in neighbors[current] if not affected[node]]
        affected[following] = True
        stack.extend(following)
    mask = affected[decoder.provider.owner_edges]
    assert np.array_equal(mask[:, 0], mask[:, 1])
    return affected


class ComponentAnchorPatchPolicy(SourcePortPatchActor):
    def __init__(self, features, decoder, active, max_step, seed):
        super().__init__(features, decoder, active, max_step, seed)
        features = np.asarray(features, dtype=np.float32)
        hidden = np.tanh(np.tanh(np.clip((features - self.mean) / self.scale, -8, 8)) @ self.w1) @ self.w2
        embedding = np.tanh(hidden)
        assert np.max(abs(embedding[self.active] - self.embedding)) < 1e-12
        embedding[self.active] = self.embedding
        self.p.update(aw=np.zeros((8, 2)), ab=np.zeros(2))
        self.buffers.update(all_embedding=embedding, source_anchor_logit=source_anchor_basis(decoder.provider),
            selected_component_owners=selected_components(decoder, active))

    def forward(self, features):
        action, _ = super().forward(features)
        n = len(self.decoder.positions)
        provider = self.decoder.provider
        selected = self.buffers['selected_component_owners']
        fraction = np.tanh(self.buffers['source_anchor_logit'] +
            (self.buffers['all_embedding'] @ self.p['aw'] + self.p['ab'])[provider.owner])
        offsets = .4 * provider.sizes * fraction
        full = self.decoder.positions[provider.owner] + provider.offsets + action[:n][provider.owner]
        ends = (full + offsets)[provider.full_edges]
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
        phases = np.mod(perimeter_phase(boundary, provider.endpoint_sizes) - provider.phase + .5, 1.) - .5
        phases[~selected[provider.owner_edges]] = 0.
        action[n:] = phases
        return action, dict(movedOwners=int(np.any(action[:n] != 0, axis=1).sum()),
            affectedOwners=int(selected.sum()), selectedComponentMaskFixed=True)
