#!/usr/bin/env python3
"""Simulated annealing on cluster centroids to minimize edge crossings.

Operates at the cluster granularity: pick a random cluster, translate all
its nodes by a small (Δx, Δy), reroute via C++ rigid binary, accept if
cross decreases (or with Boltzmann probability if it increases).

Usage:
  python scripts/sa_cluster.py <input.json> <output.json>
    [--iters 600] [--init-temp 50] [--cooling 0.995]
    [--step-max 800]
"""
import argparse
import json
import math
import random
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "bin/ogdf/darwin-arm64/django-erd-ogdf-layout"
NODES = ROOT / "data/erd-poc/graphs/real-main/nodes.tsv"
EDGES = ROOT / "data/erd-poc/graphs/real-main/edges.tsv"


def reroute(positions_tsv: Path, out_json: Path) -> int:
    """Run C++ rigid reroute, return edgeCrossings count."""
    res = subprocess.run(
        [str(BINARY), "layout",
         "--mode", "hierarchical_barycenter",
         "--nodes-file", str(NODES),
         "--edges-file", str(EDGES),
         "--edge-routing", "straight",
         "--cluster-graph", "1",
         "--positions-tsv", str(positions_tsv),
         "--rigid-positions", "1"],
        capture_output=True, text=True,
    )
    if res.returncode != 0:
        raise RuntimeError(f"binary failed: {res.stderr[:500]}")
    out_json.write_text(res.stdout)
    d = json.loads(res.stdout)
    return int(d["engineMetadata"]["edgeCrossings"])


def main():
    p = argparse.ArgumentParser()
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--iters", type=int, default=600)
    p.add_argument("--init-temp", type=float, default=50.0)
    p.add_argument("--cooling", type=float, default=0.995)
    p.add_argument("--step-max", type=float, default=800.0)
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    random.seed(args.seed)
    work = Path(tempfile.mkdtemp(prefix="sa-"))
    # Load layout, build cluster → [node modelIds]
    layout = json.loads(args.input.read_text())
    nodes = layout["nodes"]
    cluster_members = {}
    for n in nodes:
        cid = n.get("clusterId") or ""
        if not cid:
            continue
        cluster_members.setdefault(cid, []).append(n["modelId"])
    cluster_ids = list(cluster_members.keys())
    print(f"Loaded {args.input.name}: {len(nodes)} nodes, "
          f"{len(cluster_ids)} clusters")
    # Initial positions (centers)
    positions = {
        n["modelId"]: (
            n["position"]["x"] + n["size"]["width"] / 2.0,
            n["position"]["y"] + n["size"]["height"] / 2.0,
        )
        for n in nodes
    }
    def write_tsv(path: Path):
        with path.open("w") as f:
            for mid, (x, y) in positions.items():
                f.write(f"{mid}\t{x:.3f}\t{y:.3f}\n")
    cur_tsv = work / "cur.tsv"
    write_tsv(cur_tsv)
    cur_json = work / "cur.json"
    cur_cross = reroute(cur_tsv, cur_json)
    best_cross = cur_cross
    print(f"Initial cross={cur_cross}")
    best_positions = dict(positions)
    temp = args.init_temp
    t0 = time.time()
    accepted = 0
    improved = 0
    for it in range(args.iters):
        # Pick a cluster (weighted toward larger clusters: more impact)
        cid = random.choice(cluster_ids)
        members = cluster_members[cid]
        # Random translation
        step = args.step_max * (0.4 + 0.6 * random.random())
        angle = random.random() * 2 * math.pi
        dx = step * math.cos(angle)
        dy = step * math.sin(angle)
        # Apply
        saved = {m: positions[m] for m in members}
        for m in members:
            x, y = positions[m]
            positions[m] = (x + dx, y + dy)
        write_tsv(cur_tsv)
        try:
            new_cross = reroute(cur_tsv, cur_json)
        except Exception as e:
            # Revert
            for m, p_ in saved.items():
                positions[m] = p_
            continue
        delta = new_cross - cur_cross
        accept = delta < 0 or random.random() < math.exp(-delta / max(1.0, temp))
        if accept:
            cur_cross = new_cross
            accepted += 1
            if new_cross < best_cross:
                best_cross = new_cross
                best_positions = dict(positions)
                improved += 1
                print(f"iter {it+1:3d} T={temp:5.1f} cid={cid} "
                      f"step=({dx:+.0f},{dy:+.0f}) cross={new_cross} ★")
        else:
            for m, p_ in saved.items():
                positions[m] = p_
        if (it + 1) % 50 == 0:
            elapsed = time.time() - t0
            print(f"iter {it+1:3d}/{args.iters} T={temp:5.1f} "
                  f"cur={cur_cross} best={best_cross} "
                  f"acc={accepted}/{it+1} improved={improved} "
                  f"elapsed={elapsed:.0f}s")
        temp *= args.cooling

    # Write best back
    positions = best_positions
    write_tsv(cur_tsv)
    final_cross = reroute(cur_tsv, args.output)
    print(f"\nDone in {time.time()-t0:.0f}s. "
          f"Best cross = {final_cross} (was {cur_cross})")
    print(f"  saved to {args.output}")
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
