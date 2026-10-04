"""Unbiased sampled visual loss with exact active spacing constraints.

Sampling only reduces training-loss work. Every frozen proposed layout still
goes through the unchanged full native geometry and product validators.
"""
import numpy as np
from joint_layout_proxy import LayoutProxy


class SampledLayoutProxy(LayoutProxy):
    def __init__(self, positions, sizes, edges, spacing_weight, route_provider, samples, seed):
        self.origin, self.sizes, self.edges = positions, sizes, edges
        self.spacing_weight, self.route_provider = spacing_weight, route_provider
        self.hard_only, self.hard_weight = False, 0.
        self.samples = samples
        self.rng = np.random.default_rng(seed)
        self.cross_weight = len(edges)*(len(edges)-1)/2/samples
        self.hit_weight = len(edges)*len(positions)/samples
        self.refresh(positions)

    def refresh(self, positions):
        count, edge_count = len(positions), len(self.edges)
        first = self.rng.integers(edge_count, size=self.samples)
        second = self.rng.integers(edge_count-1, size=self.samples)
        second += second >= first
        cp = np.sort(np.stack([first, second], axis=1), axis=1)
        hp = np.stack([self.rng.integers(edge_count, size=self.samples),
                       self.rng.integers(count, size=self.samples)], axis=1)
        if self.route_provider is None:
            a, b = self.edges[cp[:, 0]], self.edges[cp[:, 1]]
            cp = cp[(a[:, 0] != b[:, 0]) & (a[:, 0] != b[:, 1])
                    & (a[:, 1] != b[:, 0]) & (a[:, 1] != b[:, 1])]
            hp = hp[(self.edges[hp[:, 0], 0] != hp[:, 1]) & (self.edges[hp[:, 0], 1] != hp[:, 1])]
        self.cross_pairs, self.hit_pairs = cp, hp
        # Only overlapping spacing envelopes have a nonzero spacing gradient.
        # Checking all pairs here costs bounded N-by-N boolean/axis arrays,
        # not unbounded Python pair lists or crossing derivative tensors.
        near = np.abs(positions[:, None, 0]-positions[None, :, 0]) < (self.sizes[:, None, 0]+self.sizes[None, :, 0])/2+58
        near &= np.abs(positions[:, None, 1]-positions[None, :, 1]) < (self.sizes[:, None, 1]+self.sizes[None, :, 1])/2+44
        self.near_pairs = np.stack(np.nonzero(np.triu(near, 1)), axis=1)

    def loss(self, positions, temperature=1.):
        values, gradient = super().loss(positions, temperature)
        values['sampleCrossings'] = values.pop('binaryCrossings')
        values['sampleCardHits'] = values.pop('binaryCardHits')
        values['estimatedCrossings'] = values['sampleCrossings']*self.cross_weight
        values['estimatedCardHits'] = values['sampleCardHits']*self.hit_weight
        return values, gradient


if __name__ == '__main__':
    rng = np.random.default_rng(533)
    positions = rng.uniform(-400, 400, (12, 2))
    sizes = rng.uniform(30, 90, (12, 2))
    edges = np.array([[0, 1], [2, 3], [4, 5], [6, 7], [8, 9], [10, 11], [0, 8], [3, 7]])
    proxy = SampledLayoutProxy(positions, sizes, edges, 20., None, 1024, 534)
    values, gradient = proxy.loss(positions, 3.)
    for node in range(len(positions)):
        for axis in range(2):
            hi, lo = positions.copy(), positions.copy()
            hi[node, axis] += 1e-4
            lo[node, axis] -= 1e-4
            numeric = (proxy.loss(hi, 3.)[0]['total']-proxy.loss(lo, 3.)[0]['total'])/2e-4
            np.testing.assert_allclose(gradient[node, axis], numeric, rtol=2e-5, atol=2e-5)
    # Enumerating the sampling population must equal the unsampled loss.
    exact = LayoutProxy(positions, sizes, edges, 1000.)
    exhaustive = SampledLayoutProxy(positions, sizes, edges, 20., None, 1024, 534)
    exhaustive.cross_pairs = exact.cross_pairs.copy()
    exhaustive.hit_pairs = exact.hit_pairs.copy()
    exhaustive.cross_weight = exhaustive.hit_weight = 1.
    expected, expected_gradient = exact.loss(positions, 3.)
    actual, actual_gradient = exhaustive.loss(positions, 3.)
    np.testing.assert_allclose(actual['total'], expected['total'], rtol=1e-12, atol=1e-12)
    np.testing.assert_allclose(actual_gradient, expected_gradient, rtol=1e-12, atol=1e-12)
    print({'sampledGradientComparisons': 24, 'passed': True, 'fullPopulationLossAndGradientMatch': True})
