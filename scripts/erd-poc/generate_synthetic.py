#!/usr/bin/env python3
"""Generate synthetic ERD-like graphs that mimic real Django ERD patterns.

Patterns used:
  1. Hubs: 1-3 high-degree nodes (User, Account, Organization-like)
  2. Clusters: 5-15 nodes per "app domain", densely connected internally
  3. Leaves: address/file/log-like terminal nodes attached to cluster members
  4. FK chains: linear chain of length 2-5 (workflow-like models)
  5. Cross-cluster FKs: sparse links between clusters (10-20%)

Output format mirrors what the C++ binary expects (.tsv files):
  nodes.tsv: <modelId>\t<width>\t<height>\t<x>\t<y>\t<appLabel>
  edges.tsv: <edgeId>\t<sourceId>\t<targetId>\t<kind>\t<provenance>

Run:
  python scripts/erd-poc/generate_synthetic.py --num 50 --out data/erd-poc/graphs
"""

import argparse
import json
import random
from pathlib import Path
from typing import List, Tuple


def gen_one(seed: int, target_nodes: int = None) -> dict:
    """Generate a single synthetic ERD graph.

    target_nodes: approximate node count (default: random 30-300).
    Returns dict with `nodes`, `edges`, `name` (used for filenames).
    """
    rng = random.Random(seed)
    if target_nodes is None:
        # Skewed distribution emphasising 80-300 nodes (where real crossings
        # appear AND C++ pipeline finishes in <30s). Avoid >400 — those take
        # several minutes per graph in the dense pattern.
        bucket = rng.random()
        if bucket < 0.2:
            target_nodes = rng.randint(30, 80)
        elif bucket < 0.95:
            target_nodes = rng.randint(80, 300)
        else:
            target_nodes = rng.randint(300, 500)

    nodes: List[dict] = []
    edges: List[Tuple[str, str, str, str]] = []  # (id, src, tgt, kind, prov)
    edge_id = 0

    def add_edge(src: str, tgt: str, kind: str = "foreign_key"):
        nonlocal edge_id
        edges.append((
            f"edge:{kind}:{edge_id}",
            src,
            tgt,
            kind,
            "declared",
        ))
        edge_id += 1

    # Decide app structure.
    num_apps = max(2, min(20, target_nodes // 12 + rng.randint(-1, 2)))
    apps = [f"app{i}" for i in range(num_apps)]

    # 1. HUBS: 0-3 cross-app hubs (User, Account-like).
    num_hubs = rng.choice([0, 1, 1, 2, 2, 3])
    hub_ids = []
    for i in range(num_hubs):
        mid = f"core.Hub{i}"
        nodes.append({
            "modelId": mid,
            "appLabel": "core",
            "width": 220 + rng.randint(0, 80),
            "height": 60 + rng.randint(0, 40),
        })
        hub_ids.append(mid)

    # 2. PER-APP CLUSTERS: 3-15 models per app.
    cluster_members: dict = {}
    for app in apps:
        size = rng.randint(3, 15)
        members = []
        for i in range(size):
            mid = f"{app}.M{i}"
            nodes.append({
                "modelId": mid,
                "appLabel": app,
                "width": 180 + rng.randint(0, 100),
                "height": 50 + rng.randint(0, 60),
            })
            members.append(mid)
        cluster_members[app] = members

        # Intra-cluster edges (FK chains + cross-links).
        # Pick a "primary" model and have others FK to it (star within cluster).
        if len(members) >= 2:
            primary = members[0]
            for m in members[1:]:
                if rng.random() < 0.7:  # 70% directly FK to primary
                    add_edge(m, primary)
            # Random cross-links within cluster.
            for _ in range(max(1, len(members) // 4)):
                a, b = rng.sample(members, 2)
                if a != b:
                    add_edge(a, b)

        # Each cluster's primary may FK to each hub.
        for hub in hub_ids:
            if rng.random() < 0.5:
                add_edge(members[0], hub)

    # 3. LEAVES: address/file/log-like, attached to existing nodes.
    num_leaves = max(0, target_nodes - len(nodes))
    # Spread leaves across clusters, biased toward larger ones.
    if num_leaves > 0:
        all_attachable = [n["modelId"] for n in nodes]
        leaf_kinds = ["File", "Address", "Log", "Note", "Tag", "Asset"]
        for i in range(num_leaves):
            parent = rng.choice(all_attachable)
            # Place in same app as parent for realism.
            parent_app = parent.split(".")[0]
            leaf_kind = rng.choice(leaf_kinds)
            mid = f"{parent_app}.{leaf_kind}{i}"
            nodes.append({
                "modelId": mid,
                "appLabel": parent_app,
                "width": 160 + rng.randint(0, 80),
                "height": 40 + rng.randint(0, 30),
            })
            add_edge(mid, parent)

    # 4. FK CHAINS: pick a few (2-5 long) running through cluster members.
    num_chains = rng.randint(0, max(1, num_apps // 3))
    for _ in range(num_chains):
        chain_len = rng.randint(2, 5)
        chain_app = rng.choice(apps)
        if len(cluster_members[chain_app]) < chain_len:
            continue
        chain = rng.sample(cluster_members[chain_app], chain_len)
        for a, b in zip(chain[:-1], chain[1:]):
            add_edge(a, b)

    # 5. CROSS-CLUSTER FKs — force real crossings, but keep density moderate
    # so C++ pipeline finishes fast. ~2x cluster_count cross edges + 1-2
    # chord links per cluster (vs v1.5's 4-5x density that exploded runtime).
    num_cross = max(2, len(apps) * 2 + rng.randint(-1, 3))
    for _ in range(num_cross):
        a1, a2 = rng.sample(apps, 2) if len(apps) >= 2 else (apps[0], apps[0])
        if a1 == a2:
            continue
        s = rng.choice(cluster_members[a1])
        t = rng.choice(cluster_members[a2])
        if rng.random() < 0.3:
            add_edge(s, t, kind="many_to_many")
        else:
            add_edge(s, t)
    for app in apps:
        chord_targets = rng.sample(apps, min(rng.randint(1, 2), len(apps)))
        for ct in chord_targets:
            if ct == app:
                continue
            s = rng.choice(cluster_members[app])
            t = rng.choice(cluster_members[ct])
            add_edge(s, t)

    # Dedupe edges by (src, tgt) — pick first occurrence.
    seen = set()
    deduped: List[Tuple[str, str, str, str, str]] = []
    for eid, s, t, k, p in edges:
        key = (s, t)
        if key in seen:
            continue
        if s == t:
            continue
        seen.add(key)
        deduped.append((eid, s, t, k, p))

    # Add coordinates (placeholder — will be ignored by C++; positions come
    # from layout). Set 0,0 for now.
    for nd in nodes:
        nd["x"] = 0.0
        nd["y"] = 0.0

    name = f"synth-{seed:04d}-n{len(nodes)}-e{len(deduped)}"
    return {
        "name": name,
        "seed": seed,
        "nodes": nodes,
        "edges": [
            {"edgeId": e[0], "sourceModelId": e[1], "targetModelId": e[2],
             "kind": e[3], "provenance": e[4]}
            for e in deduped
        ],
    }


def write_tsv(graph: dict, out_dir: Path):
    """Write nodes.tsv + edges.tsv that the C++ binary can consume."""
    g_dir = out_dir / graph["name"]
    g_dir.mkdir(parents=True, exist_ok=True)
    with (g_dir / "nodes.tsv").open("w") as f:
        for nd in graph["nodes"]:
            f.write(
                f"{nd['modelId']}\t{nd['width']}\t{nd['height']}\t"
                f"{nd['x']}\t{nd['y']}\t{nd['appLabel']}\n"
            )
    with (g_dir / "edges.tsv").open("w") as f:
        for e in graph["edges"]:
            f.write(
                f"{e['edgeId']}\t{e['sourceModelId']}\t{e['targetModelId']}\t"
                f"{e['kind']}\t{e['provenance']}\n"
            )
    with (g_dir / "graph.json").open("w") as f:
        json.dump(graph, f, indent=2)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--num", type=int, default=50)
    p.add_argument("--out", type=Path, default=Path("data/erd-poc/graphs"))
    p.add_argument("--seed-start", type=int, default=1)
    args = p.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    sizes = []
    edge_counts = []
    for i in range(args.num):
        g = gen_one(seed=args.seed_start + i)
        write_tsv(g, args.out)
        sizes.append(len(g["nodes"]))
        edge_counts.append(len(g["edges"]))
        print(f"  generated {g['name']}: {len(g['nodes'])} nodes, "
              f"{len(g['edges'])} edges")

    print(f"\nGenerated {args.num} graphs in {args.out}")
    print(f"  node count: min={min(sizes)}, "
          f"med={sorted(sizes)[len(sizes)//2]}, max={max(sizes)}")
    print(f"  edge count: min={min(edge_counts)}, "
          f"med={sorted(edge_counts)[len(edge_counts)//2]}, "
          f"max={max(edge_counts)}")


if __name__ == "__main__":
    main()
