#!/usr/bin/env python3
"""Audit a layout JSON for visual quality issues:
  1. Disconnected edge ends (route endpoint not at node bbox)
  2. Non-leaf bundle overlaps (bundle bbox overlaps non-member node)
  3. Cluster cohesion (member positions vs centroid spread)
"""
import json, math, sys
from collections import defaultdict
from pathlib import Path

inp = Path(sys.argv[1])
data = json.loads(inp.read_text())
nodes = data['nodes']
edges = data['routedEdges']
bundles = data['engineMetadata'].get('leafBundles', [])
nbi = {n['modelId']: n for n in nodes}

# 1) Disconnected edges
weird = []
for e in edges:
    pts = e.get('points') or []
    if len(pts) < 2: continue
    src = nbi[e['sourceModelId']]; tgt = nbi[e['targetModelId']]
    def gap_to_box(p, n):
        nx, ny = n['position']['x'], n['position']['y']
        hw, hh = n['size']['width']/2, n['size']['height']/2
        return max(0, abs(p['x']-nx)-hw) + max(0, abs(p['y']-ny)-hh)
    front, back = pts[0], pts[-1]
    g1 = gap_to_box(front, src) + gap_to_box(back, tgt)
    g2 = gap_to_box(front, tgt) + gap_to_box(back, src)
    direct = min(g1, g2)
    if direct > 50:
        weird.append((e['edgeId'][:50], direct, len(pts)))
weird.sort(key=lambda t: t[1], reverse=True)
print(f'[1] Disconnected edges (gap >50 from bbox edge): {len(weird)}/{len(edges)}')
for w in weird[:5]:
    print(f'    {w[0]:<50} gap={w[1]:.0f} pts={w[2]}')

# 2) Non-leaf bundle overlaps
print()
overlaps = []
for bi, b in enumerate(bundles):
    bx0, by0 = b['bbox']['x'], b['bbox']['y']
    bx1, by1 = bx0+b['bbox']['width'], by0+b['bbox']['height']
    members = set([b['parentModelId']]) | set(b['leafModelIds']) | set(b.get('sharedRootModelIds') or [])
    for n in nodes:
        if n['modelId'] in members: continue
        nx, ny = n['position']['x'], n['position']['y']
        nw, nh = n['size']['width']/2, n['size']['height']/2
        if (nx+nw) >= bx0 and (nx-nw) <= bx1 and (ny+nh) >= by0 and (ny-nh) <= by1:
            overlaps.append((bi, b['parentModelId'][:30], n['modelId'][:40]))
print(f'[2] Non-leaf bundle overlaps: {len(overlaps)}')
for o in overlaps[:8]:
    print(f'    bundle{o[0]:>2} ({o[1]:<30}) ∩ {o[2]}')

# 3) Cluster cohesion
print()
cluster_members = defaultdict(list)
for n in nodes:
    cid = n.get('clusterId')
    if cid: cluster_members[cid].append(n)
spread = []
for cid, mems in cluster_members.items():
    if len(mems) < 2: continue
    cx = sum(m['position']['x'] for m in mems) / len(mems)
    cy = sum(m['position']['y'] for m in mems) / len(mems)
    avgr = sum(math.hypot(m['position']['x']-cx, m['position']['y']-cy) for m in mems) / len(mems)
    maxr = max(math.hypot(m['position']['x']-cx, m['position']['y']-cy) for m in mems)
    spread.append((cid, len(mems), avgr, maxr))
spread.sort(key=lambda t: t[2], reverse=True)
print(f'[3] Cluster spread (top 10 by avg radius from centroid):')
for c in spread[:10]:
    print(f'    {c[0]:<12} members={c[1]:<3} avg_r={c[2]:.0f} max_r={c[3]:.0f}')
print(f'    ... median avg_r among {len(spread)} multi-node clusters: '
      f'{sorted(s[2] for s in spread)[len(spread)//2]:.0f}')
