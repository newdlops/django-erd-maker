#!/usr/bin/env python3
"""Coordinate-move groups only: every original card and edge stays in the scene."""
from collections import deque
from pathlib import Path
import networkx as nx


def topological_groups(graph, strategy):
    groups = set()
    counts = {}

    def add(members):
        if 2 <= len(members) <= 300:
            groups.add(tuple(sorted(members)))

    if strategy in ('separators', 'all'):
        pairs = set()
        for block in nx.biconnected_components(graph):
            if len(block) < 4:
                continue
            subgraph = graph.subgraph(block).copy()
            for first in sorted(block):
                remainder = subgraph.subgraph(block - {first})
                for second in nx.articulation_points(remainder):
                    pairs.add(tuple(sorted((first, second))))
        for first, second in sorted(pairs):
            seen = {first, second}
            for seed in sorted(set(graph[first]) | set(graph[second])):
                if seed in seen:
                    continue
                queue = [seed]
                seen.add(seed)
                for n in queue:
                    for other in graph[n]:
                        if other not in seen:
                            seen.add(other)
                            queue.append(other)
                add(queue)
        counts['separatorPairs'] = len(pairs)
        counts['separatorGroups'] = len(groups)

    if strategy in ('communities', 'all'):
        before = len(groups)
        for resolution in (.35, .7, 1.4, 2.8, 5.6):
            for seed in (42, 73):
                for community in nx.community.louvain_communities(graph, resolution=resolution, seed=seed):
                    add(community)
        counts['communityGroups'] = len(groups) - before

    if strategy in ('voronoi', 'all'):
        before = len(groups)
        order = sorted(graph, key=lambda n: (-graph.degree(n), n))
        for threshold in (8, 12, 20, 32):
            hubs = [n for n in order if graph.degree(n) >= threshold]
            owners = {n: n for n in hubs}
            queue = deque(hubs)
            while queue:
                n = queue.popleft()
                for other in sorted(graph[n]):
                    if other not in owners:
                        owners[other] = owners[n]
                        queue.append(other)
            partitions = {n: [] for n in hubs}
            for n, owner in owners.items():
                partitions[owner].append(n)
            for members in partitions.values():
                add(members)
        counts['voronoiGroups'] = len(groups) - before
    return sorted(groups), counts


def write_groups(graph, strategy, output):
    groups, counts = topological_groups(graph, strategy)
    Path(output).write_text(''.join(f'{i}\t{n}\n' for i, group in enumerate(groups) for n in group))
    return counts | {'total': len(groups)}
