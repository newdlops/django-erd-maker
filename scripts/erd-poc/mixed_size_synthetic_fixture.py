"""Seeded mixed-size cards and Leaf groups, created before any geometry score."""
import json
from pathlib import Path
import numpy as np
from joint_neural_ports import PerimeterRoutes
from learned_global_replay import INPUT_FILES, port, rounded
from run_anchor_pair_policy import digest


def fixture(directory, seed):
    directory.mkdir(parents=True, exist_ok=False); rng = np.random.default_rng(seed)
    count = 28+seed%9; slots = rng.permutation(count)
    widths = rng.choice([180, 220, 260], count); heights = rng.choice([120, 240, 360, 480, 600], count)
    physical_ids = [f'c{i:03}' for i in range(count)]; positions = []; sizes = []; members = []
    full_positions = []; full_sizes = []; full_ids = []
    leaf = [i for i in range(count) if i%8 == 7]; ordinary = [i for i in range(count) if i not in leaf]
    for i, slot in enumerate(slots):
        w, h = int(widths[i]), int(heights[i]); p = np.array([1600.+2000*(int(slot)%6), 1200.+2000*(int(slot)//6)])
        p += np.array([rounded(v) for v in rng.uniform(-90, 90, 2)])
        number = (2 if seed%2 else 4) if i in leaf else 1; columns = 2 if number > 1 else 1
        rows = (number+columns-1)//columns
        size = np.array([columns*w+(columns-1)*56+(48 if number > 1 else 0),
                         rows*h+(rows-1)*42+(48 if number > 1 else 0)])
        positions.append(p); sizes.append(size); group = []
        for k in range(number):
            key = f'n{len(full_ids):03}'; full_ids.append(key); group.append(key); full_sizes.append([w, h])
            full_positions.append(p-size/2+(24 if number > 1 else 0)+np.array([w/2+(k%columns)*(w+56),
                                                                                  h/2+(k//columns)*(h+42)]))
        members.append(group)
    edges = set(); order = rng.permutation(ordinary)
    for a, b in zip(order, np.roll(order, -1)): edges.add(tuple(sorted((int(a), int(b)))))
    while len(edges) < int(len(ordinary)*2.1): edges.add(tuple(sorted(map(int, rng.choice(ordinary, 2, replace=False)))))
    if seed%3 == 0: edges.update(tuple(sorted((ordinary[0], i))) for i in ordinary[1::2])
    for i in leaf:
        parent = ordinary[0] if seed%2 else int(rng.choice(ordinary)); edges.add(tuple(sorted((i, parent))))
    edges = sorted(edges); full_index = {key: i for i, key in enumerate(full_ids)}
    full_edges = []; full_routes = []; groups = []
    for a, b in edges:
        group = []
        for source in members[a]:
            for target in members[b]:
                edge = f'e{len(full_edges):04}'; group.append(edge); full_edges.append((edge, source, target))
                s, t = full_index[source], full_index[target]; p, q = full_positions[s], full_positions[t]
                full_routes.append([port(p, full_sizes[s], q), port(q, full_sizes[t], p)])
        groups.append(group)
    def rows(name, values):
        with (directory/name).open('x') as stream:
            for row in values: stream.write('\t'.join(map(str, row))+'\n')
    def route_rows(keys, routes):
        return [(key, ' '.join(','.join(f'{float(v):.2f}' for v in point) for point in points)) for key, points in zip(keys, routes)]
    physical_edges = [(f'g{i:04}', physical_ids[a], physical_ids[b]) for i, (a, b) in enumerate(edges)]
    rows('nodes.tsv', [(key, *size) for key, size in zip(physical_ids, sizes)])
    rows('positions.tsv', [(key, *p) for key, p in zip(physical_ids, positions)]); rows('edges.tsv', physical_edges)
    rows('individual.nodes.tsv', [(key, *size) for key, size in zip(full_ids, full_sizes)])
    rows('individual.positions.tsv', [(key, *p) for key, p in zip(full_ids, full_positions)]); rows('individual.edges.tsv', full_edges)
    rows('individual.routes.tsv', route_rows([row[0] for row in full_edges], full_routes))
    rows('components.tsv', [(key, *ids) for key, ids in zip(physical_ids, members)])
    rows('groups.tsv', [(row[0], *ids) for row, ids in zip(physical_edges, groups)])
    provider = PerimeterRoutes(directory); projected, _, endpoints = provider.forward(np.array(positions), np.array(sizes))
    assert np.array_equal(endpoints, np.array(edges)); projected = np.copysign(np.floor(abs(projected)*100+.5), projected)/100
    rows('routes.tsv', route_rows([row[0] for row in physical_edges], projected))
    low = (np.array(positions)-np.array(sizes)/2).min(0); high = (np.array(positions)+np.array(sizes)/2).max(0)
    report = {'seed': seed, 'physicalNodes': count, 'fullNodes': len(full_ids), 'physicalEdges': len(edges), 'fullEdges': len(full_edges),
        'leafCards': len(leaf), 'frameArea': float(np.prod(high-low)), 'randomSizesTopologyAndGridBeforeScores': True,
        'distinctPhysicalSizes': len(set(map(tuple, sizes))), 'distinctFullSizes': len(set(map(tuple, full_sizes))),
        'coordinatesOptimized': False, 'sourceInputs': {n: digest(directory/n) for n in INPUT_FILES}}
    assert report['frameArea'] <= 1.5e9 and report['distinctFullSizes'] > 1
    (directory/'fixture.json').write_text(json.dumps(report, indent=2)+'\n'); return report
