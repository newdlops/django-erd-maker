"""Losses for full-view adjacent crossings and own-card reentry.

These gradients update network weights, never positions or decoder outputs.
Exact native hard geometry, including boundary contacts, remains the gate.
"""
import numpy as np
from joint_layout_proxy import LayoutProxy


class FixedPorts:
    def __init__(self, positions, edges, ports):
        self.edges = edges
        self.offsets = ports - positions[edges]
        self.low, self.high = ports.min(1), ports.max(1)

    def envelopes(self, max_step):
        return self.low-max_step, self.high+max_step

    def forward(self, positions, sizes):
        ports = positions[self.edges] + self.offsets
        zeros = np.zeros(len(self.edges))
        cache = (np.zeros((len(self.edges), 2)), [zeros, zeros],
                 [np.zeros((len(self.edges), 2)), np.zeros((len(self.edges), 2))])
        return ports, cache, self.edges


class FullHardGeometry:
    def __init__(self, grouped, physical_positions, physical_sizes, physical_edges, max_step, weight):
        assert grouped.attached
        self.grouped = grouped
        positions = physical_positions[grouped.owner] + grouped.offsets
        provider = FixedPorts(positions, grouped.full_edges, grouped.original_ports)
        self.proxy = LayoutProxy(positions, grouped.sizes, grouped.full_edges, max_step,
                                 route_provider=provider, hard_only=True, hard_weight=weight)
        # Equal endpoints at the same card remain equal under attached moves.
        # Straight segments sharing that endpoint cannot cross properly again.
        cp = self.proxy.cross_pairs
        first, second = grouped.full_edges[cp[:, 0]], grouped.full_edges[cp[:, 1]]
        ports = grouped.original_ports
        shared_point = np.zeros(len(cp), dtype=bool)
        for a in range(2):
            for b in range(2):
                shared_point |= ((first[:, a] == second[:, b])
                                 & (ports[cp[:, 0], a] == ports[cp[:, 1], b]).all(1))
        self.proxy.cross_pairs = cp[~shared_point]
        self.projected = None
        if len(physical_positions) != len(positions) or len(physical_edges) != len(grouped.full_edges):
            self.projected = LayoutProxy(physical_positions, physical_sizes, physical_edges, max_step,
                                         route_provider=grouped, hard_only=True, hard_weight=weight)

    def loss(self, physical_positions):
        positions = physical_positions[self.grouped.owner] + self.grouped.offsets
        values, full_gradient = self.proxy.loss(positions)
        gradient = np.zeros_like(physical_positions)
        np.add.at(gradient, self.grouped.owner, full_gradient)
        if self.projected is not None:
            physical_values, physical_gradient = self.projected.loss(physical_positions)
            gradient += physical_gradient
            values = {'total': values['total'] + physical_values['total'],
                      'full': values, 'projected': physical_values}
        return values, gradient
