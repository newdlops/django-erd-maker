"""Read-only source feasibility cells, with no candidate scoring or selection."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from compact_patch_neural_policy import patch_buffers
from compact_ordered_source_port_policy import adjacent_planes
from run_anchor_pair_walk import WalkDecoder


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(65536), b''):
            h.update(block)
    return h.hexdigest()


def clip(polygon, normal, budget):
    if not len(polygon):
        return polygon
    result = []
    for a, b in zip(polygon, np.roll(polygon, -1, axis=0)):
        fa, fb = float(a @ normal - budget), float(b @ normal - budget)
        if fa <= 1e-10:
            result.append(a)
        if (fa < -1e-10 and fb > 1e-10) or (fb < -1e-10 and fa > 1e-10):
            result.append(a + (b - a) * (fa / (fa - fb)))
    if not result:
        # Zero is always in the source cell, including a zero-dimensional cell.
        return np.zeros((1, 2))
    result = np.array(result)
    result[np.abs(result) < 1e-10] = 0.
    return result


def source_cell(decoder, node):
    bounds = patch_buffers(decoder.positions, decoder.sizes, [node], 2048.)
    low, high = bounds['dag_lower'][0] / 100, bounds['dag_upper'][0] / 100
    poly = np.array([[low[0], low[1]], [high[0], low[1]], [high[0], high[1]], [low[0], high[1]]])
    box_capacity = float(abs(poly).max(initial=0))
    p = decoder.provider
    planes = adjacent_planes(p.original_ports, p.full_edges, p.owner_edges, [node])
    for left, right, axis, budget in zip(planes['star_first'], planes['star_second'], planes['star_axis'], planes['star_budget']):
        coefficient = axis if left == node else -axis
        assert (left == node) != (right == node)
        poly = clip(poly, coefficient, float(budget))
    star_capacity = float(abs(poly).max(initial=0))
    outward_count = 0
    directions = [np.array([-1., 0.]), np.array([1., 0.]), np.array([0., -1.]), np.array([0., 1.])]
    for edge, owners in enumerate(p.owner_edges):
        if node not in owners or owners[0] == owners[1]:
            continue
        for end in (0, 1):
            boundary = p.port_offsets[edge, end]
            half = p.endpoint_sizes[edge, end] / 2
            offsets = [abs(boundary[0] + half[0]), abs(boundary[0] - half[0]),
                abs(boundary[1] + half[1]), abs(boundary[1] - half[1])]
            diff = p.original_ports[edge, 1-end] - p.original_ports[edge, end]
            available = [i for i, offset in enumerate(offsets) if offset <= .011 + 1e-8]
            assert available
            face = max(available, key=lambda i:float(diff @ directions[i]))
            normal = directions[face]
            margin = float(diff @ normal)
            assert margin >= -.011 - 1e-8
            coefficient = normal if owners[end] == node else -normal
            poly = clip(poly, coefficient, max(0., margin - .03))
            outward_count += 1
    return dict(node=int(node), sourceSeparationBox=[low.tolist(), high.tolist()],
        boxCapacityPixels=box_capacity, starCellCapacityPixels=star_capacity,
        outwardStarCellCapacityPixels=float(abs(poly).max(initial=0)),
        vertices=poly.tolist(), sourceAdjacentConstraints=len(planes['star_gap']),
        sourceOutwardConstraints=outward_count,
        smallSourceStarMargins=int(np.count_nonzero(planes['star_budget'] < .01)))


root = Path('.tmp/visualcross-ml-150-750-20261004/radial-leaf1')
out = Path('.tmp/visualcross-ml-150-750-20261004/source-star-cell-capacity1')
out.mkdir(exist_ok=False)
binding_path = Path('.tmp/visualcross-ml-150-750-20261004/geometry-world1/source-binding.json')
binding = json.loads(binding_path.read_text())
assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
views = []
for view in ('individual', 'overview'):
    stage = root / f'{view}-learning1'
    report = json.loads((stage / 'report.json').read_text())
    assert digest(stage / 'observations.npz') == report['observationsSha256']
    directory = Path(report['sourceDirectory'])
    assert {name: digest(directory / name) for name in binding['viewSources'][view]['inputs']} == binding['viewSources'][view]['inputs']
    decoder = WalkDecoder(directory)
    with np.load(stage / 'observations.npz', allow_pickle=False) as f:
        nodes = f['nodes'].copy()
    ids = [line.split('\t')[0] for line in (directory / 'nodes.tsv').read_text().splitlines()]
    degree = np.bincount(decoder.provider.owner_edges.ravel(), minlength=len(ids))
    index = 4 if view == 'overview' else 6
    pressure = np.expm1(nodes[:, index]) + np.expm1(nodes[:, index+1])
    eligible = np.flatnonzero((degree >= 2) & (pressure > 0))
    order = eligible[np.lexsort((eligible, -pressure[eligible]))][:12]
    cells = []
    for node in order:
        cell = source_cell(decoder, int(node))
        cell.update(id=ids[node], incidentCount=int(degree[node]), sourcePressure=float(pressure[node]))
        cells.append(cell)
        print(json.dumps(dict(view=view, **{k:cell[k] for k in ('node','id','incidentCount','sourcePressure',
            'boxCapacityPixels','starCellCapacityPixels','outwardStarCellCapacityPixels','smallSourceStarMargins')})), flush=True)
    views.append(dict(view=view, corePressureOwners=len(eligible), sourceVisual=report['initialVisual'], cells=cells,
        sourceInputs=binding['viewSources'][view]['inputs'], observationsSha256=report['observationsSha256']))
result = dict(kind='immutable-source-star-cell-diagnostic-v1', views=views,
    sourceBindingSha256=digest(binding_path), codeSha256=digest(__file__),
    nativeMeasurements=0, noFutureGeometryMeasured=True, noActionsSubmitted=True,
    noCandidateSelected=True, noModelTrained=True)
(out / 'diagnostic.json').write_text(json.dumps(result, indent=2) + '\n')
