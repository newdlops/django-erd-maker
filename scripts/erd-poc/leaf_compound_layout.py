#!/usr/bin/env python3
"""Reversible leaf-card coarsening for a straight-layout experiment.

Only layout actors are contracted. Every original card, its size, and every
canonical relationship survives expansion. In particular, secondary roots are
real edges in the quotient graph, not discarded leaf decoration.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
from statistics import median


def center(node):
    return (node['position']['x'] + node['size']['width'] / 2,
            node['position']['y'] + node['size']['height'] / 2)


def boundary(node, target):
    x, y = center(node)
    dx, dy = target[0] - x, target[1] - y
    if abs(dx) + abs(dy) < 1e-9:
        dx = 1
    factor = min(node['size']['width'] / 2 / abs(dx) if dx else math.inf,
                 node['size']['height'] / 2 / abs(dy) if dy else math.inf)
    return {'x': round(x + dx * factor, 2), 'y': round(y + dy * factor, 2)}


def route_length(route):
    a, b = route['points']
    return math.hypot(b['x'] - a['x'], b['y'] - a['y'])


def pack_members(members, columns=None):
    """Actual card extents plus the existing 56 x 42 table clearance."""
    members = sorted(members, key=lambda node: node['modelId'])
    max_w = max(node['size']['width'] for node in members)
    max_h = max(node['size']['height'] for node in members)
    columns = columns or max(1, math.ceil(math.sqrt(len(members) * (max_h + 42) / (max_w + 56))))
    columns = min(columns, len(members))
    rows = math.ceil(len(members) / columns)
    widths = [max(node['size']['width'] for node in members[col::columns]) for col in range(columns)]
    heights = [max(node['size']['height'] for node in members[row * columns:(row + 1) * columns]) for row in range(rows)]
    width, height = sum(widths) + 56 * (columns - 1) + 48, sum(heights) + 42 * (rows - 1) + 48
    slots = []
    for index, node in enumerate(members):
        col, row = index % columns, index // columns
        slots.append({'modelId': node['modelId'], 'size': node['size'], 'offset': {
            'x': 24 + sum(widths[:col]) + 56 * col + (widths[col] - node['size']['width']) / 2,
            'y': 24 + sum(heights[:row]) + 42 * row + (heights[row] - node['size']['height']) / 2}})
    return {'width': width, 'height': height}, slots


def coarsen(layout, memberships):
    nodes = {node['modelId']: node for node in layout['nodes']}
    assert len(nodes) == len(layout['nodes']), 'duplicate model ID'
    owners, compounds = {}, []
    for index, bundle in enumerate(memberships['leafBundles']):
        ids = [mid for mid in bundle['leafModelIds'] if mid in nodes]
        if len(ids) < 2:
            continue
        actor_id = f'@leaf:{index}'
        assert actor_id not in nodes, 'synthetic ID collision'
        assert len(ids) == len(set(ids)), 'duplicate member in bundle'
        assert bundle['parentModelId'] not in ids, 'parent cannot be absorbed as a leaf'
        for mid in ids:
            assert mid not in owners, 'overlapping leaf bundles'
            owners[mid] = actor_id
        members = [nodes[mid] for mid in ids]
        size, slots = pack_members(members)
        cx, cy = (median(center(node)[axis] for node in members) for axis in (0, 1))
        compounds.append({'modelId': actor_id, 'size': size,
                          'position': {'x': cx - size['width'] / 2, 'y': cy - size['height'] / 2},
                          'parentModelId': bundle['parentModelId'],
                          'sharedRootModelIds': bundle.get('sharedRootModelIds', []),
                          'members': slots})
    assert not any(bundle['parentModelId'] in owners for bundle in compounds), 'nested parents need a hierarchy'
    actors = [copy.deepcopy(node) for node in layout['nodes'] if node['modelId'] not in owners] + compounds
    actors_by_id = {node['modelId']: node for node in actors}
    quotient, internal = {}, []
    route_by_id = {route['edgeId']: route for route in layout['routedEdges']}
    assert len(route_by_id) == len(layout['routedEdges']), 'duplicate canonical route'
    edge_to_pair = {}
    for route in layout['routedEdges']:
        source, target = (owners.get(route[key], route[key]) for key in ('sourceModelId', 'targetModelId'))
        assert source in actors_by_id and target in actors_by_id, 'missing endpoint'
        if source == target:
            internal.append(route['edgeId'])
            continue
        pair = tuple(sorted((source, target)))
        edge_to_pair[route['edgeId']] = pair
        quotient.setdefault(pair, []).append(route['edgeId'])
    edges, pair_to_edge = [], {}
    for index, (pair, members) in enumerate(quotient.items()):
        source, target = pair
        points = [boundary(actors_by_id[source], center(actors_by_id[target])),
                  boundary(actors_by_id[target], center(actors_by_id[source]))]
        if source not in owners.values() and target not in owners.values():
            assert len(members) == 1, 'input must already consolidate model pairs'
            original = route_by_id[members[0]]
            points = original['points'] if original['sourceModelId'] == source else list(reversed(original['points']))
        edge = {'edgeId': f'@pair:{index}', 'sourceModelId': source, 'targetModelId': target,
                'points': points, 'memberEdgeIds': members}
        edges.append(edge)
        pair_to_edge[pair] = edge['edgeId']
    # Fix the previous overview's group membership for the controlled comparison.
    # All quotient edges, including other roots of an overview group, also enter
    # the placement cost; this subset is only the separately reported overview.
    selected = set()
    for group in layout['engineMetadata']['renderedCarrierRoutes']:
        available = [route_by_id[mid] for mid in group['memberEdgeIds'] if mid in edge_to_pair]
        if available:
            representative = min(available, key=lambda route: (
                route_length(route), route['edgeId']))
            selected.add(pair_to_edge[edge_to_pair[representative['edgeId']]])
    return {'schema': 'leaf-compound-layout-v1', 'actors': actors, 'edges': edges,
            'overviewEdgeIds': sorted(selected), 'internalEdgeIds': internal,
            'ownership': owners, 'originalCards': len(nodes), 'canonicalRelationships': len(route_by_id),
            'compoundCount': len(compounds), 'leafCount': len(owners)}


def expand(layout, graph, positions):
    result = copy.deepcopy(layout)
    original = {node['modelId']: node for node in layout['nodes']}
    actual = {node['modelId']: node for node in result['nodes']}
    assert set(positions) == {node['modelId'] for node in graph['actors']}, 'positions must cover every actor exactly'
    for actor in graph['actors']:
        cx, cy = positions[actor['modelId']]
        assert math.isfinite(cx) and math.isfinite(cy), 'non-finite actor position'
        x, y = cx - actor['size']['width'] / 2, cy - actor['size']['height'] / 2
        if 'members' in actor:
            for member in actor['members']:
                actual[member['modelId']]['position'] = {'x': x + member['offset']['x'], 'y': y + member['offset']['y']}
        else:
            # Center -> top-left float conversion must not reroute fixed core
            # cards whose boundary ports have already been refined.
            if math.dist((cx, cy), center(original[actor['modelId']])) > 1e-6:
                actual[actor['modelId']]['position'] = {'x': x, 'y': y}
    changed = {mid for mid, node in actual.items() if node['position'] != original[mid]['position']}
    for route in result['routedEdges']:
        source, target = route['sourceModelId'], route['targetModelId']
        if source in changed or target in changed:
            route['points'] = [boundary(actual[source], center(actual[target])), boundary(actual[target], center(actual[source]))]
    route_by_id = {route['edgeId']: route for route in result['routedEdges']}
    metadata = result['engineMetadata']
    for group in metadata['renderedCarrierRoutes']:
        members = [route_by_id[mid] for mid in group['memberEdgeIds']]
        shortest = min(members, key=lambda route: (route_length(route), route['edgeId']))
        group['points'] = shortest['points']
    # Scores on the old coordinates must never masquerade as this result's score.
    for key in ('visualCrossings', 'edgeCrossings', 'edgeNodeIntersections', 'nodeOverlaps', 'boundingBoxArea', 'visualCrossingsScope'):
        metadata.pop(key, None)
    metadata['strategy'] = 'leaf-compound-layout-expanded' if graph['compoundCount'] else 'june-overview-coordinate-control'
    return result


def write_graph(graph, directory):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'compound-graph.json').write_text(json.dumps(graph, separators=(',', ':')))
    with (directory / 'nodes.tsv').open('w') as nodes, (directory / 'positions.tsv').open('w') as positions:
        for actor in graph['actors']:
            mid = actor['modelId']
            nodes.write(f"{mid}\t{actor['size']['width']}\t{actor['size']['height']}\t{int('members' in actor)}\n")
            positions.write(f"{mid}\t{center(actor)[0]}\t{center(actor)[1]}\n")
    selected = set(graph['overviewEdgeIds'])
    with (directory / 'edges.tsv').open('w') as edges, (directory / 'routes.tsv').open('w') as routes:
        for edge in graph['edges']:
            edges.write(f"{edge['edgeId']}\t{edge['sourceModelId']}\t{edge['targetModelId']}\t{int(edge['edgeId'] in selected)}\n")
            a, b = edge['points']
            routes.write(f"{edge['edgeId']}\t{a['x']},{a['y']} {b['x']},{b['y']}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('coarsen', 'expand'))
    parser.add_argument('--layout', type=Path, required=True)
    parser.add_argument('--memberships', type=Path)
    parser.add_argument('--without-compounds', action='store_true', help='same graph export without contraction, for the control')
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--positions', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.operation == 'coarsen' and not args.without_compounds and args.memberships is None:
        parser.error('coarsen requires --memberships or --without-compounds')
    if args.operation == 'expand' and (args.positions is None or args.output is None):
        parser.error('expand requires --positions and --output')
    layout = json.loads(args.layout.read_text())
    if args.operation == 'coarsen':
        memberships = {'leafBundles': []} if args.without_compounds else json.loads(args.memberships.read_text())
        graph = coarsen(layout, memberships)
        graph['sourceSha256'] = hashlib.sha256(args.layout.read_bytes()).hexdigest()
        write_graph(graph, args.directory)
        print(json.dumps({key: graph[key] for key in ('originalCards', 'canonicalRelationships', 'compoundCount', 'leafCount')} | {
            'actors': len(graph['actors']), 'quotientEdges': len(graph['edges']), 'internalEdges': len(graph['internalEdgeIds']), 'overviewEdges': len(graph['overviewEdgeIds'])}))
    else:
        graph = json.loads((args.directory / 'compound-graph.json').read_text())
        assert hashlib.sha256(args.layout.read_bytes()).hexdigest() == graph['sourceSha256'], 'source layout changed'
        positions = {}
        for line in args.positions.read_text().splitlines():
            mid, x, y = line.split('\t')
            assert mid not in positions
            positions[mid] = (float(x), float(y))
        result = expand(layout, graph, positions)
        args.output.write_text(json.dumps(result, separators=(',', ':')))
        print(json.dumps({'output': str(args.output), 'cards': len(result['nodes']), 'relationships': len(result['routedEdges'])}))


if __name__ == '__main__':
    main()
