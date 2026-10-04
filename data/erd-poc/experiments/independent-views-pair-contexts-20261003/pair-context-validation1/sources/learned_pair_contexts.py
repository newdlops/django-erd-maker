"""Source-bound two-vertex separator contexts; no positions or proposals.

Context indices are independent of physical-card indices. Membership uses the
largest biconnected edge block and at most 32 high-degree separator candidates.
"""


def pair_separator_contexts(ids, edges, anchor_limit=32):
    index = {key: i for i, key in enumerate(ids)}
    assert len(index) == len(ids) and 2 <= anchor_limit <= 32
    pairs = sorted({tuple(sorted((index[a], index[b]))) for a, b in edges if a != b})
    graph = [set() for _ in ids]
    adjacency = [[] for _ in ids]
    for edge, (a, b) in enumerate(pairs):
        graph[a].add(b)
        graph[b].add(a)
        adjacency[a].append((b, edge))
        adjacency[b].append((a, edge))
    discovery = [-1] * len(ids)
    low = [0] * len(ids)
    edge_stack, blocks = [], []
    clock = 0
    # Iterative Tarjan traversal also handles long chains without recursion.
    for root in range(len(ids)):
        if discovery[root] >= 0:
            continue
        discovery[root] = low[root] = clock
        clock += 1
        pending = [(root, -1, -1, iter(adjacency[root]))]
        while pending:
            node, parent, parent_edge, neighbors = pending[-1]
            item = next(neighbors, None)
            if item is None:
                pending.pop()
                if parent >= 0:
                    low[parent] = min(low[parent], low[node])
                    if low[node] >= discovery[parent]:
                        block = []
                        while True:
                            edge = edge_stack.pop()
                            block.append(edge)
                            if edge == parent_edge:
                                break
                        blocks.append(block)
                continue
            other, edge = item
            if edge == parent_edge:
                continue
            if discovery[other] < 0:
                edge_stack.append(edge)
                discovery[other] = low[other] = clock
                clock += 1
                pending.append((other, node, edge, iter(adjacency[other])))
            elif discovery[other] < discovery[node]:
                edge_stack.append(edge)
                low[node] = min(low[node], discovery[other])
    assert not edge_stack and sum(map(len, blocks)) == len(pairs)
    if not blocks:
        return []
    largest = min(blocks, key=lambda row: (-len(row), min(row)))
    core = {node for edge in largest for node in pairs[edge]}
    anchors = sorted(core, key=lambda n: (-len(graph[n] & core), n))[:anchor_limit]

    def flood(seed, allowed):
        found, pending = {seed}, [seed]
        for node in pending:
            for other in (graph[node] & allowed) - found:
                found.add(other)
                pending.append(other)
        return found

    contexts = set()
    for i, a in enumerate(anchors):
        for b in anchors[i + 1:]:
            unseen, parts = core - {a, b}, []
            while unseen:
                part = flood(min(unseen), unseen)
                parts.append(part)
                unseen -= part
            if len(parts) < 2:
                continue
            trunk = min(parts, key=lambda part: (-len(part), min(part)))
            for part in parts:
                if part is trunk:
                    continue
                full = flood(min(part), set(range(len(ids))) - {a, b})
                assert full & core == part
                if len(full) > 1:
                    contexts.add(tuple(sorted(full)))
    return [[ids[n] for n in row] for row in sorted(contexts)]


def self_test():
    ids = list(range(11))
    edges = [(0, 2), (2, 3), (3, 1), (0, 4), (4, 5), (5, 1),
             (0, 6), (6, 7), (7, 8), (8, 1), (3, 9)]
    groups = pair_separator_contexts(ids, edges)
    assert [2, 3, 9] in groups and [4, 5] in groups
    assert groups == pair_separator_contexts(ids, list(reversed(edges)) + [(0, 2), (5, 5)])
    renamed = [f'card-{n}' for n in ids]
    assert [[renamed[n] for n in group] for group in groups] == pair_separator_contexts(
        renamed, [(renamed[a], renamed[b]) for a, b in edges])
    assert pair_separator_contexts([0, 1], []) == []
    assert pair_separator_contexts(list(range(1500)), [(n, n + 1) for n in range(1499)]) == []
    return {'pairContextFixture': 'pass', 'contexts': len(groups),
            'outsideAttachmentsIncluded': True, 'longChainWithoutRecursion': True}


if __name__ == '__main__':
    import json
    print(json.dumps(self_test()))
