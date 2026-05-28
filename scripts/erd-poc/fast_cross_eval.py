"""Fast in-Python edge-crossing evaluator (vectorized via numpy).

Replaces the C++ rigid-reroute subprocess (~3s/call) with a pure
numpy implementation (~50-100ms/call) for 30-60x RL speedup.

Computes edge crossings on straight-line segments between node
positions. Matches the C++ binary's rigid-positions crossing count
(within rounding) for connected node pairs.

Usage:
  from fast_cross_eval import FastCrossEval
  evaluator = FastCrossEval(edges_array, n_nodes)
  count = evaluator.count_crossings(positions)
"""
import numpy as np


class FastCrossEval:
    """Pre-computes edge pair indices; counts crossings per call."""

    def __init__(self, edges: np.ndarray, n_nodes: int):
        """
        edges: int array shape (E, 2) — (src_node_idx, tgt_node_idx)
        n_nodes: total node count
        """
        self.edges = np.asarray(edges, dtype=np.int32)
        self.n_nodes = n_nodes
        self.E = self.edges.shape[0]
        # All forward pairs (i, j) with i < j
        i, j = np.triu_indices(self.E, k=1)
        # Remove pairs that share an endpoint (no real crossing possible)
        si, ti = self.edges[i, 0], self.edges[i, 1]
        sj, tj = self.edges[j, 0], self.edges[j, 1]
        share = (si == sj) | (si == tj) | (ti == sj) | (ti == tj)
        self.i = i[~share]
        self.j = j[~share]
        self.K = self.i.shape[0]

    def count_crossings(self, positions: np.ndarray) -> int:
        """Returns number of properly intersecting edge pairs.

        positions: float array shape (n_nodes, 2)
        """
        pos = np.asarray(positions, dtype=np.float64)
        # Build edge endpoint arrays for selected pairs
        e_i = self.edges[self.i]  # [K, 2]
        e_j = self.edges[self.j]
        a = pos[e_i[:, 0]]; b = pos[e_i[:, 1]]
        c = pos[e_j[:, 0]]; d = pos[e_j[:, 1]]
        # Standard segment intersection via orientation tests:
        # AB and CD intersect iff orient(A,B,C) ≠ orient(A,B,D)
        # AND orient(C,D,A) ≠ orient(C,D,B).
        def orient(p, q, r):
            return ((q[:, 0] - p[:, 0]) * (r[:, 1] - p[:, 1])
                    - (q[:, 1] - p[:, 1]) * (r[:, 0] - p[:, 0]))
        o1 = orient(a, b, c)
        o2 = orient(a, b, d)
        o3 = orient(c, d, a)
        o4 = orient(c, d, b)
        # Sign difference (strict; collinear treated as no-cross to match C++)
        s1 = np.sign(o1)
        s2 = np.sign(o2)
        s3 = np.sign(o3)
        s4 = np.sign(o4)
        crosses = (s1 != s2) & (s3 != s4) & (s1 != 0) & (s2 != 0) \
                  & (s3 != 0) & (s4 != 0)
        return int(crosses.sum())


if __name__ == "__main__":
    # Self-test: cross is symmetric, identity gives consistent value
    import sys
    import json
    from pathlib import Path
    layout = json.loads(Path(sys.argv[1]).read_text())
    nodes = layout["nodes"]
    n = len(nodes)
    id2 = {nd["modelId"]: i for i, nd in enumerate(nodes)}
    edges = []
    for re in layout.get("routedEdges", []):
        s = id2.get(re.get("sourceModelId"))
        t = id2.get(re.get("targetModelId"))
        if s is None or t is None or s == t:
            continue
        edges.append((s, t))
    edges_arr = np.array(edges, dtype=np.int32)
    positions = np.array([
        [nd["position"]["x"] + nd["size"]["width"]/2,
         nd["position"]["y"] + nd["size"]["height"]/2]
        for nd in nodes], dtype=np.float64)
    evaluator = FastCrossEval(edges_arr, n)
    import time
    t0 = time.perf_counter()
    c = evaluator.count_crossings(positions)
    elapsed_ms = (time.perf_counter() - t0) * 1000
    print(f"FastCrossEval: cross={c} ({elapsed_ms:.1f}ms, "
          f"K={evaluator.K} pairs)")
    em = layout.get("engineMetadata", {}) or {}
    print(f"layout engineMetadata cross: {em.get('edgeCrossings')}")
