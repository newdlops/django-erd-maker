"""Memory-bounded learned pair-event delta for one or two moving owners."""
import numpy as np
from geometry_world_model import WorldProxy as SingleProxy, cross_eligible, cross_features, hit_eligible, hit_features


class WorldProxy(SingleProxy):
    def crosses(self, lines, left, right):
        total = 0.
        for first in range(0, len(left), 4096):
            a, b = lines[left[first:first+4096]], lines[right[first:first+4096]]
            keep = cross_eligible(a, b)
            if keep.any(): total += float(self.models['cross'].probability(cross_features(a[keep], b[keep])).sum())
        return total

    def hits(self, lines, positions, sizes, edges, line_ids, node_ids):
        total = 0.
        for first in range(0, len(line_ids), 4096):
            a, b = line_ids[first:first+4096], node_ids[first:first+4096]
            keep = np.all(edges[a] != b[:, None], axis=1) & hit_eligible(lines[a], positions[b], sizes[b])
            if keep.any(): total += float(self.models['hit'].probability(hit_features(lines[a[keep]], positions[b[keep]], sizes[b[keep]])).sum())
        return total

    def delta(self, before, after, sizes, edges):
        old_positions, old_lines = before; new_positions, new_lines = after
        changed_edges = np.flatnonzero(np.any(new_lines != old_lines, axis=(1, 2))).astype(np.int32)
        changed_nodes = np.flatnonzero(np.any(new_positions != old_positions, axis=1)).astype(np.int32)
        assert 1 <= len(changed_nodes) <= 2
        left = np.repeat(changed_edges, len(old_lines)); right = np.tile(np.arange(len(old_lines), dtype=np.int32), len(changed_edges))
        changed = np.zeros(len(old_lines), dtype=bool); changed[changed_edges] = True
        keep = (left != right) & ((~changed[right]) | (left < right)); left, right = left[keep], right[keep]
        cross_before = self.crosses(old_lines, left, right); cross_after = self.crosses(new_lines, left, right)
        line_ids = np.r_[np.repeat(changed_edges, len(sizes)), np.repeat(np.flatnonzero(~changed).astype(np.int32), len(changed_nodes))]
        node_ids = np.r_[np.tile(np.arange(len(sizes), dtype=np.int32), len(changed_edges)), np.tile(changed_nodes, np.count_nonzero(~changed))]
        hit_before = self.hits(old_lines, old_positions, sizes, edges, line_ids, node_ids)
        hit_after = self.hits(new_lines, new_positions, sizes, edges, line_ids, node_ids)
        return {'predictedDelta': cross_after+hit_after-cross_before-hit_before,
                'predictedCrossDelta': cross_after-cross_before, 'predictedHitDelta': hit_after-hit_before,
                'changedEdges': len(changed_edges), 'changedNodes': len(changed_nodes)}
