"""Shared NN selects one original owner and a motion inside its source cell.

The cell is decoded once from source spacing, adjacent-line separation and
outward endpoint inequalities. Its vertices are immutable feasibility buffers,
not trained coordinates. Native metrics never enter inference or cell creation.
Original card-relative endpoints, dimensions and the complete frame are kept.
"""
import json
import numpy as np
from compact_patch_neural_policy import patch_buffers
from compact_ordered_source_port_policy import adjacent_planes


def clip_source_cell(polygon, normal, budget):
    assert budget >= 0 and len(polygon)
    result = []
    for a, b in zip(polygon, np.roll(polygon, -1, axis=0)):
        fa, fb = float(a @ normal - budget), float(b @ normal - budget)
        if fa <= 1e-10:
            result.append(a.copy())
        if (fa < -1e-10 and fb > 1e-10) or (fb < -1e-10 and fa > 1e-10):
            result.append(a + (b - a) * fa / (fa - fb))
    assert result, 'source cell lost its original zero displacement'
    cleaned = []
    for point in result:
        point[np.abs(point) < 1e-10] = 0.
        if not cleaned or np.linalg.norm(point - cleaned[-1]) > 1e-9:
            cleaned.append(point)
    if len(cleaned) > 1 and np.linalg.norm(cleaned[-1] - cleaned[0]) <= 1e-9:
        cleaned.pop()
    return np.array(cleaned)


def source_cell(decoder, node, max_step):
    bounds = patch_buffers(decoder.positions, decoder.sizes, [node], max_step)
    low, high = bounds['dag_lower'][0] / 100, bounds['dag_upper'][0] / 100
    polygon = np.array([[low[0], low[1]], [high[0], low[1]],
        [high[0], high[1]], [low[0], high[1]]])
    p = decoder.provider
    planes = adjacent_planes(p.original_ports, p.full_edges, p.owner_edges, [node])
    normals, budgets = [], []
    for left, right, axis, budget in zip(planes['star_first'], planes['star_second'],
            planes['star_axis'], planes['star_budget']):
        assert (left == node) != (right == node)
        normals.append(axis if left == node else -axis)
        budgets.append(float(budget))
    directions = np.array([[-1., 0.], [1., 0.], [0., -1.], [0., 1.]])
    for edge, owners in enumerate(p.owner_edges):
        if node not in owners or owners[0] == owners[1]:
            continue
        for end in (0, 1):
            boundary, half = p.port_offsets[edge, end], p.endpoint_sizes[edge, end] / 2
            face_distance = [abs(boundary[0] + half[0]), abs(boundary[0] - half[0]),
                abs(boundary[1] + half[1]), abs(boundary[1] - half[1])]
            diff = p.original_ports[edge, 1-end] - p.original_ports[edge, end]
            faces = [i for i, value in enumerate(face_distance) if value <= .011 + 1e-8]
            assert faces
            face = max(faces, key=lambda i: float(diff @ directions[i]))
            normal = directions[face]
            margin = float(diff @ normal)
            assert margin >= -.011 - 1e-8
            normals.append(normal if owners[end] == node else -normal)
            budgets.append(max(0., margin - .03))
    for normal, budget in zip(normals, budgets):
        polygon = clip_source_cell(polygon, normal, budget)
    normals = np.array(normals).reshape(-1, 2)
    budgets = np.array(budgets)
    assert np.all(polygon @ normals.T <= budgets + 1e-7)
    assert np.all(polygon >= low - 1e-7) and np.all(polygon <= high + 1e-7)
    arc = np.r_[0., np.cumsum(np.linalg.norm(np.roll(polygon, -1, axis=0) - polygon, axis=1))]
    return polygon, arc, len(planes['star_gap'])


class SourceStarCellPolicy:
    def __init__(self, features, decoder, active, max_step, seed):
        self.decoder = decoder
        self.active = np.asarray(active, dtype=np.int32)
        assert 1 <= len(self.active) <= 16 and len(set(self.active.tolist())) == len(self.active)
        assert 0 < max_step <= 2048
        polygons, arcs, counts = [], [], []
        for node in self.active:
            polygon, arc, count = source_cell(decoder, int(node), max_step)
            polygons.append(polygon)
            arcs.append(arc)
            counts.append(count)
        width = max(map(len, polygons))
        vertices = np.zeros((len(self.active), width, 2))
        distances = np.zeros((len(self.active), width + 1))
        for row, (polygon, arc) in enumerate(zip(polygons, arcs)):
            vertices[row, :len(polygon)] = polygon
            vertices[row, len(polygon):] = polygon[-1]
            distances[row, :len(arc)] = arc
            distances[row, len(arc):] = arc[-1]
        self.buffers = dict(cell_vertices=vertices, cell_arc=distances,
            cell_counts=np.array(list(map(len, polygons)), dtype=np.int32),
            source_adjacent_constraint_counts=np.array(counts, dtype=np.int32),
            max_step=np.array(max_step))
        features = np.asarray(features, dtype=np.float32)
        assert features.shape == (len(decoder.positions), 64)
        self.mean = features.mean(0, dtype=np.float64)
        self.scale = np.maximum(.2, features.std(0, dtype=np.float64))
        extent = abs(vertices).max(1)
        extra = np.concatenate([np.log1p(extent) / np.log1p(max_step),
            (np.log1p(counts) / 12)[:,None], (distances[:,-1] / (8 * max_step))[:,None]], axis=1)
        inputs = np.concatenate([np.clip((features[self.active] - self.mean) / self.scale, -8, 8), extra], axis=1)
        rng = np.random.default_rng(seed)
        self.w1 = rng.normal(0, 1 / np.sqrt(68), (68, 12))
        self.w2 = rng.normal(0, 1 / np.sqrt(12), (12, 8))
        self.embedding = np.tanh(np.tanh(np.tanh(inputs @ self.w1) @ self.w2))
        self.p = dict(wo=np.zeros((8, 3)), bo=np.zeros(3))

    def forward(self, features):
        logits = self.embedding @ self.p['wo'] + self.p['bo']
        selected = int(np.argmax(logits[:,0]))
        node = int(self.active[selected])
        amplitude = max(0., float(np.tanh(logits[selected,1])))
        phase = (float(np.tanh(logits[selected,2])) + 1.) / 2
        count = int(self.buffers['cell_counts'][selected])
        vertices = self.buffers['cell_vertices'][selected,:count]
        arc = self.buffers['cell_arc'][selected,:count+1]
        distance = (phase % 1.) * arc[-1]
        index = min(count-1, int(np.searchsorted(arc[1:], distance, side='right')))
        span = arc[index+1] - arc[index]
        amount = (distance - arc[index]) / span if span > 1e-12 else 0.
        point = vertices[index] + amount * (vertices[(index+1) % count] - vertices[index])
        delta = np.zeros_like(self.decoder.positions)
        raw = amplitude * point
        delta[node] = np.copysign(np.floor(abs(raw) * 100 + .5), raw) / 100
        action = np.concatenate([delta, np.zeros_like(self.decoder.provider.phase)])
        return action, dict(selectedOwner=node, selectedVocabularyIndex=selected,
            movedOwners=int(np.any(delta != 0,axis=1).sum()), maximumDisplacementPixels=float(abs(delta).max()),
            amplitude=amplitude, sourceCellPhase=phase, sourceCellCapacityPixels=float(abs(vertices).max()),
            vocabularySize=len(self.active), originalCardRelativePortsRetained=True)

    def save(self, path, metadata):
        np.savez_compressed(path, **self.p, active=self.active, embedding=self.embedding,
            w1=self.w1, w2=self.w2, mean=self.mean, scale=self.scale, **self.buffers,
            metadata=np.array(json.dumps(metadata)))
