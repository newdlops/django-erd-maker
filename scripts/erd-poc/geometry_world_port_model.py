"""Exact source-bound port decoder and learned line-only event delta.

The decoder translates supplied NN-selected perimeter phases into cent-rounded
lines. It performs no event scoring, coordinate search, acceptance or repair.
"""
import numpy as np
from geometry_world_multi_owner import WorldProxy as PairProxy
from single_owner_cached_observer_v2 import rounded, native_hypot
from joint_neural_ports import perimeter_points


def canonical_wire_phase(values):
    return np.array([float(format(float(v), '.12g')) for v in np.asarray(values).ravel()]).reshape(np.asarray(values).shape)


class PortDecoder:
    def __init__(self, observer, zero, view):
        self.observer, self.provider, self.view = observer, observer.provider, view
        self.zero = zero.copy(); self.n = len(observer.positions)
        self.positions, self.full_positions, self.physical, self.full, self.physical_edges = observer.decode(zero)
        self.delta = rounded(canonical_wire_phase(zero[:self.n])); self.zero_phase = canonical_wire_phase(zero[self.n:])
        self.group_index = np.full(len(self.full), -1, dtype=np.int32)
        for i, group in enumerate(observer.groups): self.group_index[group] = i

    def geometry(self, proposed):
        assert np.array_equal(proposed[:self.n], self.zero[:self.n])
        phase = canonical_wire_phase(proposed[self.n:]); changed = np.flatnonzero(np.any(phase != self.zero_phase, axis=1))
        lines = self.full.copy()
        if len(changed):
            points = perimeter_points(self.provider.phase[changed]+phase[changed], self.provider.endpoint_sizes[changed])[0]
            lines[changed] = rounded(self.provider.original_ports[changed]+(points-self.provider.base_boundary[changed])
                +self.delta[self.provider.owner_edges[changed]])
        if self.view == 'individual': return (self.full_positions, lines)
        physical = self.physical.copy()
        for g in np.unique(self.group_index[changed]):
            if g < 0: continue
            group = self.observer.groups[g]
            eligible = [int(e) for e in group if self.provider.owner[self.observer.edges[e,0]] != self.provider.owner[self.observer.edges[e,1]]]
            e = min(eligible, key=lambda e: (native_hypot(*(lines[e,1]-lines[e,0])), self.observer.edge_ids[e]))
            a, b = map(int, self.provider.owner[self.observer.edges[e]]); line = lines[e]; params = []
            for node, exiting in ((a,True),(b,False)):
                if len(self.observer.components[node]) == 1: params.append(0. if exiting else 1.); continue
                enter, leave = 0., 1.
                for axis in (0,1):
                    origin, direction = float(line[0,axis]), float(line[1,axis]-line[0,axis])
                    if abs(direction) < 1e-12: continue
                    left = (float(self.positions[node,axis]-self.observer.sizes[node,axis]/2)-origin)/direction
                    right = (float(self.positions[node,axis]+self.observer.sizes[node,axis]/2)-origin)/direction
                    enter, leave = max(enter,min(left,right)), min(leave,max(left,right))
                params.append(leave if exiting else enter)
            physical[g] = rounded(line[0]+(line[1]-line[0])*np.array(params)[:,None])
        return (self.positions, physical)


class WorldProxy(PairProxy):
    def delta(self, before, after, sizes, edges):
        positions, old_lines = before; new_positions, new_lines = after
        assert np.array_equal(positions, new_positions)
        changed_edges = np.flatnonzero(np.any(new_lines != old_lines, axis=(1,2))).astype(np.int32)
        left = np.repeat(changed_edges, len(old_lines)); right = np.tile(np.arange(len(old_lines), dtype=np.int32), len(changed_edges))
        changed = np.zeros(len(old_lines), dtype=bool); changed[changed_edges] = True
        keep = (left != right) & ((~changed[right]) | (left < right)); left, right = left[keep], right[keep]
        cross_before = self.crosses(old_lines,left,right); cross_after = self.crosses(new_lines,left,right)
        line_ids = np.repeat(changed_edges,len(sizes)); node_ids = np.tile(np.arange(len(sizes),dtype=np.int32),len(changed_edges))
        hit_before = self.hits(old_lines,positions,sizes,edges,line_ids,node_ids)
        hit_after = self.hits(new_lines,positions,sizes,edges,line_ids,node_ids)
        return {'predictedDelta':cross_after+hit_after-cross_before-hit_before,
            'predictedCrossDelta':cross_after-cross_before,'predictedHitDelta':hit_after-hit_before,
            'changedEdges':len(changed_edges),'changedNodes':0}


OFFSETS = np.array([-.1,-.05,-.02,-.01,-.005,.005,.01,.02,.05,.1])
PATTERNS = np.array([np.eye(2)[end]*v for end in (0,1) for v in OFFSETS]
    + [[v,v] for v in [-.1,-.05,-.02,.02,.05,.1]]
    + [[v,-v] for v in [-.1,-.05,-.02,.02,.05,.1]])


def same_face_patterns(decoder, edge):
    provider = decoder.provider
    original = perimeter_points(provider.phase[edge]+decoder.zero_phase[edge],provider.endpoint_sizes[edge])[1]
    result = []
    for i, offset in enumerate(PATTERNS):
        shifted = perimeter_points(provider.phase[edge]+canonical_wire_phase(decoder.zero[decoder.n+edge]+offset),provider.endpoint_sizes[edge])[1]
        if np.array_equal(original,shifted): result.append(i)
    return result
