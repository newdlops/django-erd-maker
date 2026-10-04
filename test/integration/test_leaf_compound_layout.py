import copy
import importlib.util
import math
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('leaf_compound_layout', Path(__file__).parents[2] / 'scripts/erd-poc/leaf_compound_layout.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    def node(mid, x, y, width=236, height=74):
        return {'modelId': mid, 'position': {'x': x, 'y': y}, 'size': {'width': width, 'height': height}}
    nodes = [node('parent', 100, 100), node('second-root', 1000, 100), node('fixed', 100, 1200),
             node('leaf1', 300, 500), node('leaf2', 900, 500, 396, 90), node('leaf3', 1400, 800, 248, 82)]
    by_id = {n['modelId']: n for n in nodes}
    routes = []
    for eid, source, target in [('a', 'parent', 'leaf1'), ('b', 'parent', 'leaf2'),
                                 ('c', 'second-root', 'leaf2'), ('d', 'leaf1', 'leaf3'), ('e', 'parent', 'fixed')]:
        routes.append({'edgeId': eid, 'sourceModelId': source, 'targetModelId': target,
                       'points': [module.boundary(by_id[source], module.center(by_id[target])), module.boundary(by_id[target], module.center(by_id[source]))]})
    layout = {'nodes': nodes, 'routedEdges': routes, 'engineMetadata': {'renderedCarrierRoutes': [
        {'carrierId': 'leaf', 'memberEdgeIds': ['a', 'b', 'c', 'd'], 'points': routes[0]['points']},
        {'carrierId': 'e', 'memberEdgeIds': ['e'], 'points': routes[-1]['points']}]}}
    memberships = {'leafBundles': [{'parentModelId': 'parent', 'leafModelIds': ['leaf1', 'leaf2', 'leaf3'],
                                    'sharedRootModelIds': ['parent', 'second-root']}]}
    return layout, memberships


class LeafCompoundContractTest(unittest.TestCase):
    def test_uncontracted_control_preserves_input_geometry_before_search(self):
        layout, _ = fixture()
        graph = module.coarsen(layout, {'leafBundles': []})
        positions = {node['modelId']: module.center(node) for node in graph['actors']}
        result = module.expand(layout, graph, positions)
        self.assertEqual(graph['compoundCount'], 0)
        self.assertEqual(len(graph['actors']), len(layout['nodes']))
        self.assertEqual(result['nodes'], layout['nodes'])
        self.assertEqual(result['routedEdges'], layout['routedEdges'])
        self.assertEqual(result['engineMetadata']['strategy'], 'june-overview-coordinate-control')

    def test_coarsening_preserves_secondary_root_and_internal_relationships(self):
        layout, membership = fixture()
        graph = module.coarsen(layout, membership)
        self.assertEqual(len(graph['actors']), 4)
        self.assertEqual(graph['leafCount'], 3)
        ids = [mid for edge in graph['edges'] for mid in edge['memberEdgeIds']] + graph['internalEdgeIds']
        self.assertCountEqual(ids, [route['edgeId'] for route in layout['routedEdges']])
        self.assertEqual(graph['internalEdgeIds'], ['d'])
        self.assertTrue(any({edge['sourceModelId'], edge['targetModelId']} == {'second-root', '@leaf:0'} for edge in graph['edges']))

    def test_rectangle_contains_actual_sizes_with_clearance(self):
        layout, membership = fixture()
        graph = module.coarsen(layout, membership)
        actor = next(node for node in graph['actors'] if 'members' in node)
        self.assertGreater(actor['size']['width'] * actor['size']['height'], sum(m['size']['width'] * m['size']['height'] for m in actor['members']))
        for member in actor['members']:
            self.assertGreaterEqual(member['offset']['x'], 24)
            self.assertGreaterEqual(member['offset']['y'], 24)
            for dimension, axis in [('width', 'x'), ('height', 'y')]:
                self.assertLessEqual(member['offset'][axis] + member['size'][dimension], actor['size'][dimension] - 24)
        for i, a in enumerate(actor['members']):
            for b in actor['members'][i + 1:]:
                dx = abs(a['offset']['x'] + a['size']['width'] / 2 - b['offset']['x'] - b['size']['width'] / 2) - (a['size']['width'] + b['size']['width']) / 2
                dy = abs(a['offset']['y'] + a['size']['height'] / 2 - b['offset']['y'] - b['size']['height'] / 2) - (a['size']['height'] + b['size']['height']) / 2
                self.assertTrue(dx >= 56 or dy >= 42)

    def test_expand_moves_members_rigidly_and_preserves_fixed_ports(self):
        layout, membership = fixture()
        before = copy.deepcopy(layout)
        graph = module.coarsen(layout, membership)
        positions = {node['modelId']: module.center(node) for node in graph['actors']}
        first = module.expand(layout, graph, positions)
        x, y = positions['@leaf:0']
        positions['@leaf:0'] = (x + 800, y + 900)
        second = module.expand(layout, graph, positions)
        self.assertEqual(layout, before)
        self.assertEqual(second['routedEdges'][-1], layout['routedEdges'][-1])
        for a, b in zip(first['nodes'], second['nodes']):
            self.assertEqual(a['modelId'], b['modelId'])
            self.assertEqual(a['size'], b['size'])
            if a['modelId'].startswith('leaf'):
                self.assertEqual(b['position']['x'] - a['position']['x'], 800)
                self.assertEqual(b['position']['y'] - a['position']['y'], 900)
            else:
                self.assertEqual(a, b)
        self.assertEqual([r['edgeId'] for r in second['routedEdges']], [r['edgeId'] for r in layout['routedEdges']])
        self.assertTrue(all(len(route['points']) == 2 for route in second['routedEdges']))
        nodes = {n['modelId']: n for n in second['nodes']}
        for route in second['routedEdges']:
            for point, key in zip(route['points'], ['sourceModelId', 'targetModelId']):
                node = nodes[route[key]]
                x, y = node['position']['x'], node['position']['y']
                distance = min(abs(point['x'] - x), abs(point['x'] - x - node['size']['width']),
                               abs(point['y'] - y), abs(point['y'] - y - node['size']['height']))
                self.assertLessEqual(distance, .01)

    def test_ambiguous_ownership_and_incomplete_positions_are_rejected(self):
        layout, membership = fixture()
        graph = module.coarsen(layout, membership)
        with self.assertRaises(AssertionError):
            module.expand(layout, graph, {})
        positions = {node['modelId']: module.center(node) for node in graph['actors']}
        positions['@leaf:0'] = (math.nan, 100)
        with self.assertRaises(AssertionError):
            module.expand(layout, graph, positions)
        membership['leafBundles'].append(copy.deepcopy(membership['leafBundles'][0]))
        with self.assertRaises(AssertionError):
            module.coarsen(layout, membership)


if __name__ == '__main__':
    unittest.main()
