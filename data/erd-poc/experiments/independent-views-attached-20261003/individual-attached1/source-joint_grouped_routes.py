"""Differentiable grouped center-ray routes for the coordinated layout loss.

This mirrors representative selection and clips through member anchors inside
rigid Leaf cards. Continuous clipping deliberately omits output quantization;
the independent native/product audits remain the acceptance authority.
"""
import numpy as np
from joint_layout_proxy import clipped_ports, propagate_port_gradients
from learned_global_replay import routes


def read_pairs(path):
    return {row[0]: [float(row[1]), float(row[2])]
            for row in (line.split('\t') for line in path.read_text().splitlines())}


def anchored_ports(positions, sizes, edges, offsets, clip=None):
    source = positions[edges[:, 0]] + offsets[:, 0]
    target = positions[edges[:, 1]] + offsets[:, 1]
    direction = target - source
    safe = np.maximum(np.abs(direction), 1e-12)
    parameters, derivatives = [], []
    row = np.arange(len(edges))
    for endpoint in range(2):
        half = sizes[edges[:, endpoint]] / 2
        sign = -1 if endpoint == 0 else 1
        ratios = (half + sign * np.sign(direction) * offsets[:, endpoint]) / safe
        axis = np.argmin(ratios, 1)
        value = ratios[row, axis]
        derivative = np.zeros_like(direction)
        derivative[row, axis] = -value / direction[row, axis]
        if clip is not None:
            value = np.where(clip[:, endpoint], value, 0.)
            derivative[~clip[:, endpoint]] = 0.
        parameters.append(value)
        derivatives.append(derivative)
    points = np.stack([source + parameters[0][:, None] * direction,
                       target - parameters[1][:, None] * direction], 1)
    return points, (direction, parameters, derivatives)


class GroupedRoutes:
    def __init__(self, directory, attached=False):
        self.attached = attached
        physical = read_pairs(directory / 'positions.tsv')
        individual = read_pairs(directory / 'individual.positions.tsv')
        sizes = read_pairs(directory / 'individual.nodes.tsv')
        self.physical_ids, self.individual_ids = list(physical), list(individual)
        physical_index = {key: i for i, key in enumerate(physical)}
        individual_index = {key: i for i, key in enumerate(individual)}
        owner = {}
        for line in (directory / 'components.tsv').read_text().splitlines():
            card, *members = line.split('\t')
            for member in members:
                assert member not in owner
                owner[member] = physical_index[card]
        assert set(owner) == set(individual)
        self.owner = np.array([owner[key] for key in individual], dtype=np.int32)
        original = np.array(list(physical.values()))
        full_positions = np.array(list(individual.values()))
        self.offsets = full_positions - original[self.owner]
        self.sizes = np.array([sizes[key] for key in individual])
        edge_rows = [line.split('\t') for line in (directory / 'individual.edges.tsv').read_text().splitlines()]
        full_index = {row[0]: i for i, row in enumerate(edge_rows)}
        self.full_edges = np.array([[individual_index[s], individual_index[t]] for _, s, t in edge_rows])
        self.owner_edges = self.owner[self.full_edges]
        route_rows = routes(directory / 'individual.routes.tsv')
        self.original_ports = np.array([route_rows[row[0]] for row in edge_rows]).reshape(-1, 2, 2)
        self.port_offsets = self.original_ports - full_positions[self.full_edges]
        external = self.owner_edges[:, 0] != self.owner_edges[:, 1]
        groups, group_ids = [], []
        for line in (directory / 'groups.tsv').read_text().splitlines():
            key, *members = line.split('\t')
            group_ids.append(key)
            # Lexical ordering resolves equal route lengths exactly as native.
            group = np.array([full_index[name] for name in sorted(members)
                              if external[full_index[name]]], dtype=np.int32)
            assert len(group)
            groups.append(group)
        physical_edges = [line.split('\t') for line in (directory / 'edges.tsv').read_text().splitlines()]
        edge_ids = [row[0] for row in physical_edges]
        assert group_ids == edge_ids
        self.groups = groups
        is_leaf = np.bincount(self.owner, minlength=len(physical)) > 1
        self.is_leaf = is_leaf
        self.competitors = []
        for group, (_, source, target) in zip(groups, physical_edges):
            s, t = physical_index[source], physical_index[target]
            candidate_edges = self.owner_edges[group]
            if is_leaf[s] or is_leaf[t]:
                allowed = ((np.min(candidate_edges, axis=1) == min(s, t))
                           & (np.max(candidate_edges, axis=1) == max(s, t)))
            else:
                allowed = ~is_leaf[candidate_edges].any(axis=1)
            assert allowed.any()
            if (~allowed).any():
                self.competitors.append((group[allowed], group[~allowed]))
        self.low = np.array([full_positions[self.full_edges[group]].reshape(-1, 2).min(0) for group in groups])
        self.high = np.array([full_positions[self.full_edges[group]].reshape(-1, 2).max(0) for group in groups])
        if attached:
            self.low = np.array([self.original_ports[group].reshape(-1, 2).min(0) for group in groups])
            self.high = np.array([self.original_ports[group].reshape(-1, 2).max(0) for group in groups])

    def envelopes(self, max_step):
        return self.low - max_step, self.high + max_step

    def forward(self, positions, sizes):
        full_positions = positions[self.owner] + self.offsets
        full_ports = (full_positions[self.full_edges] + self.port_offsets if self.attached
                      else clipped_ports(full_positions, self.sizes, self.full_edges)[0])
        rounded = np.copysign(np.floor(np.abs(full_ports) * 100 + .5), full_ports) / 100
        lengths = np.sqrt(np.sum((rounded[:, 1] - rounded[:, 0]) ** 2, axis=1))
        selected = np.array([group[np.argmin(lengths[group])] for group in self.groups])
        edges = self.owner_edges[selected]
        offsets = self.offsets[self.full_edges[selected]]
        if self.attached:
            offsets = offsets + self.port_offsets[selected]
        ports, cache = anchored_ports(positions, sizes, edges, offsets,
                                     self.is_leaf[edges] if self.attached else None)
        return ports, cache, edges

    def constraint_loss(self, positions):
        """Teach the model to preserve the native Leaf-pair representation."""
        full_positions = positions[self.owner] + self.offsets
        if self.attached:
            ports = full_positions[self.full_edges] + self.port_offsets
            cache = None
        else:
            ports, cache = clipped_ports(full_positions, self.sizes, self.full_edges)
        direction = ports[:, 1] - ports[:, 0]
        lengths = np.sqrt(np.sum(direction * direction, axis=1) + 1e-12)
        route_gradient = np.zeros_like(ports)
        total = 0.
        for allowed, other in self.competitors:
            good, bad = allowed[np.argmin(lengths[allowed])], other[np.argmin(lengths[other])]
            # Four pixels absorb endpoint rounding without changing validation.
            depth = max(0., lengths[good] + 4. - lengths[bad])
            total += 20. * (depth / 8.) ** 2
            if depth:
                for index, sign in [(good, 1.), (bad, -1.)]:
                    gradient = direction[index] / lengths[index] * (sign * 20. * depth / 32.)
                    route_gradient[index, 0] -= gradient
                    route_gradient[index, 1] += gradient
        full_gradient = np.zeros_like(full_positions)
        if self.attached:
            np.add.at(full_gradient, self.full_edges[:, 0], route_gradient[:, 0])
            np.add.at(full_gradient, self.full_edges[:, 1], route_gradient[:, 1])
        else:
            propagate_port_gradients(full_gradient, route_gradient, self.full_edges, cache)
        gradient = np.zeros_like(positions)
        np.add.at(gradient, self.owner, full_gradient)
        return float(total), gradient
