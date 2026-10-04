"""NN translations parameterized by immutable adjacent-segment separation planes.

Planes describe the source's existing hard-valid order, not visual-crossing
costs. A common scale applies inside every forward before Native observation.
No rejected pose, Native metric, coordinate search or per-rejection repair
enters this actor.
"""
import numpy as np
from compact_source_port_policy import SourcePortPatchActor


def adjacent_planes(ports, edges, owners, active):
    ports = np.copysign(np.floor(abs(ports) * 100 + .5), ports) / 100
    active_set = set(map(int, active))
    incident = {}
    for edge, endpoints in enumerate(edges):
        for node in set(map(int, endpoints)):
            incident.setdefault(node, []).append(edge)
    pairs = set()
    for rows in incident.values():
        for i, first in enumerate(rows):
            for second in rows[i + 1:]:
                if active_set.intersection(map(int, owners[[first, second]].ravel())):
                    pairs.add((first, second))
    firsts, seconds, axes, gaps, original_pairs = [], [], [], [], []
    for first, second in sorted(pairs):
        a, b = ports[first], ports[second]
        ae, be = edges[first], edges[second]
        shared = any(ae[i] == be[j] and np.all(abs(a[i] - b[j]) < .001)
                     for i in (0, 1) for j in (0, 1))
        if shared:
            # Fixed card-relative coordinates retain this shared endpoint exactly.
            continue
        da, db = a[1] - a[0], b[1] - b[0]
        vectors = [np.array([1., 0.]), np.array([0., 1.]), da, db,
                   np.array([-da[1], da[0]]), np.array([-db[1], db[0]])]
        options = []
        for vector in vectors:
            length = np.linalg.norm(vector)
            if length <= 1e-9:
                continue
            axis = vector / length
            pa, pb = a @ axis, b @ axis
            forward, backward = pb.min() - pa.max(), pa.min() - pb.max()
            options.append((float(forward), axis))
            options.append((float(backward), -axis))
        gap, axis = max(options, key=lambda row: row[0])
        assert gap >= -1e-8, (first, second, gap)
        for i in (0, 1):
            for j in (0, 1):
                left, right = int(owners[first, i]), int(owners[second, j])
                if left == right or not active_set.intersection((left, right)):
                    continue
                margin = float((b[j] - a[i]) @ axis)
                assert margin >= -1e-8
                firsts.append(left)
                seconds.append(right)
                axes.append(axis)
                gaps.append(max(0., margin))
                original_pairs.append((first, second))
    return dict(star_first=np.array(firsts, dtype=np.int32), star_second=np.array(seconds, dtype=np.int32),
                star_axis=np.array(axes, dtype=np.float64).reshape(-1, 2),
                star_gap=np.array(gaps, dtype=np.float64),
                star_budget=np.maximum(0., np.array(gaps, dtype=np.float64) - .02),
                star_source_pairs=np.array(original_pairs, dtype=np.int32).reshape(-1, 2))


def decode_adjacent(delta, buffers):
    motion = np.sum((delta[buffers['star_second']] - delta[buffers['star_first']]) * buffers['star_axis'], axis=1)
    negative = motion < -1e-12
    scale = 1.
    if negative.any():
        scale = min(1., float(np.min(buffers['star_budget'][negative] / -motion[negative])))
    # Translation-invariant cent rounding retains the original integer DAG lags.
    decoded = np.floor(delta * scale * 100 + .5) / 100
    return decoded, scale


class OrderedSourcePortPatchActor(SourcePortPatchActor):
    def __init__(self, features, decoder, active, max_step, seed):
        assert np.array_equal(decoder.provider.owner, np.arange(len(decoder.positions)))
        super().__init__(features, decoder, active, max_step, seed)
        self.buffers.update(adjacent_planes(decoder.provider.original_ports, decoder.provider.full_edges,
                                           decoder.provider.owner_edges, self.active))

    def forward(self, features):
        output, _ = super().forward(features)
        n = len(self.decoder.positions)
        output[:n], scale = decode_adjacent(output[:n], self.buffers)
        return output, dict(sourceAdjacentScale=scale, sourceAdjacentConstraints=len(self.buffers['star_gap']))
