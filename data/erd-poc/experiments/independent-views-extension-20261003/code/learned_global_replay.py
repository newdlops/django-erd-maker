"""Replay neural spacing actions without evaluating scores or choosing geometry."""
import math
from pathlib import Path

INPUT_FILES = [prefix + name for prefix in ['', 'individual.']
               for name in ['nodes.tsv', 'edges.tsv', 'positions.tsv', 'routes.tsv']]
INPUT_FILES += ['components.tsv', 'groups.tsv']


def pairs(path):
    return {row[0]: list(map(float, row[1:]))
            for row in (line.split('\t') for line in path.read_text().splitlines())}


def routes(path):
    return {key: [list(map(float, p.split(','))) for p in text.split()]
            for key, text in (line.split('\t') for line in path.read_text().splitlines())}


def rounded(value):
    return math.copysign(math.floor(abs(value)*100+.5)/100, value)


def bounds(positions, sizes):
    return [min(p[0]-sizes[key][0]/2 for key, p in positions.items()),
            max(p[0]+sizes[key][0]/2 for key, p in positions.items()),
            min(p[1]-sizes[key][1]/2 for key, p in positions.items()),
            max(p[1]+sizes[key][1]/2 for key, p in positions.items())]


def port(a, size, b):
    dx, dy = b[0]-a[0], b[1]-a[1]
    if abs(dx) < 1e-9 and abs(dy) < 1e-9:
        dx = 1.
    fraction = min(1e100 if abs(dx) < 1e-12 else size[0]*.5/abs(dx),
                   1e100 if abs(dy) < 1e-12 else size[1]*.5/abs(dy))
    return [rounded(a[0]+dx*fraction), rounded(a[1]+dy*fraction)]


class GlobalReplay:
    def __init__(self, directory, area_limit, attached=False):
        assert 0 < area_limit <= 1.5e9
        self.limit = area_limit
        self.attached = attached
        self.positions = [pairs(directory/(prefix+'positions.tsv')) for prefix in ['', 'individual.']]
        self.sizes = [pairs(directory/(prefix+'nodes.tsv')) for prefix in ['', 'individual.']]
        self.components = {row[0]: row[1:] for row in
                           (line.split('\t') for line in (directory/'components.tsv').read_text().splitlines())}
        assert self.components.keys() == self.positions[0].keys()
        members = [node for row in self.components.values() for node in row]
        assert len(members) == len(set(members)) and set(members) == self.positions[1].keys()
        self.edges = [line.split('\t') for line in (directory/'individual.edges.tsv').read_text().splitlines()]
        self.routes = routes(directory/'individual.routes.tsv')

    def apply(self, action):
        old_positions = {key: value[:] for key, value in self.positions[1].items()}
        scale = [math.exp(max(-.5, min(.5, float(f'{value:.12g}')))) for value in action]
        left, right, top, bottom = bounds(self.positions[0], self.sizes[0])
        center = [(left+right)/2, (top+bottom)/2]
        for key, position in self.positions[0].items():
            target = [rounded(center[k]+(position[k]-center[k])*scale[k]) for k in range(2)]
            delta = [target[k]-position[k] for k in range(2)]
            self.positions[0][key] = target
            for member in self.components[key]:
                for k in range(2):
                    self.positions[1][member][k] += delta[k]
        for positions, sizes in zip(self.positions, self.sizes):
            left, right, top, bottom = bounds(positions, sizes)
            assert (right-left)*(bottom-top) <= self.limit+.01
        positions, sizes = self.positions[1], self.sizes[1]
        for key, source, target in self.edges:
            if self.attached:
                self.routes[key] = [[rounded(p[k]+positions[node][k]-old_positions[node][k]) for k in range(2)]
                                    for p, node in zip(self.routes[key], [source, target])]
            else:
                self.routes[key] = [port(positions[source], sizes[source], positions[target]),
                                    port(positions[target], sizes[target], positions[source])]

    def verify(self, output):
        for expected, suffix in zip(self.positions, ['', '.individual']):
            actual = pairs(Path(str(output)+suffix))
            assert actual.keys() == expected.keys()
            assert all(abs(actual[key][k]-p[k]) < 1e-6 for key, p in expected.items() for k in range(2))
        actual = routes(Path(str(output)+'.individual.routes.tsv'))
        assert actual.keys() == self.routes.keys()
        assert all(abs(actual[key][j][k]-points[j][k]) < 1e-8
                   for key, points in self.routes.items() for j in range(2) for k in range(2))


class NeighborReplay(GlobalReplay):
    def __init__(self, directory):
        import numpy as np
        super().__init__(directory, 1.5e9, True)
        self.np = np
        self.ids = list(self.positions[0])
        self.centers = np.array(list(self.positions[0].values()))
        self.extents = np.array([self.sizes[0][key] for key in self.ids])

    def apply_record(self, record):
        np, node = self.np, record['node']
        differences = self.centers-self.centers[node]
        gaps = np.maximum(0., np.abs(differences)-(self.extents+self.extents[node])/2)
        distances = np.floor(np.hypot(*gaps.T)*1e6+.5)
        centers = np.floor(np.hypot(*differences.T)*1e6+.5)
        distances[node] = float('inf')
        neighbor = int(np.lexsort((np.arange(len(self.ids)), centers, distances))[0])
        assert neighbor == record['result']['decodedTarget']
        if not record['result']['accepted']:
            return
        delta = [rounded(float(f'{value:.12g}')) for value in record['action']]
        moved = set()
        for index in [node, neighbor]:
            key = self.ids[index]
            for k in range(2):
                self.positions[0][key][k] += delta[k]
            self.centers[index] = self.positions[0][key]
            for member in self.components[key]:
                moved.add(member)
                for k in range(2):
                    self.positions[1][member][k] += delta[k]
        for key, source, target in self.edges:
            for point, member in zip(self.routes[key], [source, target]):
                if member in moved:
                    for k in range(2):
                        point[k] = rounded(point[k]+delta[k])
