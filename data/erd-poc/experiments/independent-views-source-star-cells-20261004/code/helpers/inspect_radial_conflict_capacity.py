"""Measure immutable source movement capacity; this never selects geometry."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from radial_leaf_policy import RadialLeafPolicy
from run_anchor_pair_walk import WalkDecoder


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(65536), b''):
            h.update(block)
    return h.hexdigest()


def capacity(decoder, buffers, moving, ids):
    positions, sizes = decoder.positions, decoder.sizes
    velocity = buffers['velocity'].copy()
    velocity[~moving] = 0.
    magnitude = abs(velocity).max(1)
    limit = np.minimum(.95, np.divide(float(buffers['max_step']), magnitude,
        out=np.zeros(len(positions)), where=magnitude > 1e-9))
    limit[~moving] = 0.
    details = {}
    for node in np.flatnonzero(moving):
        v = velocity[node]
        binding = dict(kind='step-or-fraction')
        for axis in (0, 1):
            if abs(v[axis]) < 1e-12:
                continue
            room = ((buffers['frame_high'][axis] - sizes[node, axis] / 2 - positions[node, axis])
                if v[axis] > 0 else
                (positions[node, axis] - sizes[node, axis] / 2 - buffers['frame_low'][axis]))
            budget = max(0., room - .02) / abs(v[axis])
            if budget < limit[node]:
                limit[node] = budget
                binding = dict(kind='frame', axis=axis, sourceRoom=float(room))
        others = np.flatnonzero(np.arange(len(positions)) != node)
        delta = positions[others] - positions[node]
        gap = abs(delta) - (sizes[others] + sizes[node]) / 2 - [55.99, 41.99]
        allowed = gap >= -1e-8
        assert allowed.any(1).all()
        sign = np.sign(delta)
        own = np.maximum(0., sign * v)
        peer = np.maximum(0., -sign * velocity[others])
        total = own + peer
        safe = np.maximum(0., gap - .03)
        ratio = np.divide(safe, total, out=np.full_like(gap, np.inf), where=total > 1e-12)
        axes = np.argmax(np.where(allowed, ratio, -np.inf), 1)
        row = np.arange(len(others))
        closing = own[row, axes]
        closers = (own[row, axes] > 1e-12).astype(int) + (peer[row, axes] > 1e-12).astype(int)
        constrained = np.flatnonzero(closing > 1e-12)
        if len(constrained):
            budgets = safe[row, axes][constrained] / (closing[constrained] * closers[constrained])
            local = int(np.argmin(budgets))
            i = int(constrained[local])
            budget = float(budgets[local])
            if budget < limit[node]:
                limit[node] = budget
                binding = dict(kind='source-pair-spacing', peer=int(others[i]), peerId=ids[others[i]],
                    axis=int(axes[i]), sourceGap=float(gap[i, axes[i]]), closers=int(closers[i]),
                    peerMoves=bool(moving[others[i]]), ownClosing=float(closing[i]))
        details[int(node)] = binding
    return limit, details


def distribution(value):
    return dict(count=len(value), maximum=float(value.max(initial=0)),
        quantiles=np.quantile(value, [0, .25, .5, .75, 1]).tolist() if len(value) else [],
        aboveOnePixel=int(np.count_nonzero(value > 1)),
        aboveHundredPixels=int(np.count_nonzero(value > 100)))


root = Path('.tmp/visualcross-ml-150-750-20261004/radial-leaf1')
out = Path('.tmp/visualcross-ml-150-750-20261004/radial-conflict-capacity1')
out.mkdir(exist_ok=False)
binding_path = Path('.tmp/visualcross-ml-150-750-20261004/geometry-world1/source-binding.json')
binding = json.loads(binding_path.read_text())
canonical = Path('data/erd-poc/candidates/captain-ml-independent-views.layout.json')
assert digest(canonical) == binding['promotedCandidateSha256']
results = []
for view in ('individual', 'overview'):
    stage = root / f'{view}-learning1'
    report = json.loads((stage / 'report.json').read_text())
    directory = Path(report['sourceDirectory'])
    assert digest(stage / 'observations.npz') == report['observationsSha256']
    spec = binding['viewSources'][view]
    assert {name: digest(directory / name) for name in spec['inputs']} == spec['inputs']
    decoder = WalkDecoder(directory)
    ids = [line.split('\t')[0] for line in (directory / 'nodes.tsv').read_text().splitlines()]
    assert len(ids) == len(decoder.positions)
    with np.load(stage / 'observations.npz', allow_pickle=False) as saved:
        features = saved['nodes'].copy()
    model = RadialLeafPolicy(features, decoder, report['seed'], report['maxStep'])
    index = 4 if view == 'overview' else 6
    eligible = model.buffers['eligible']
    pressure = eligible & np.any(features[:, index:index + 2] > 0, axis=1)
    old, old_details = capacity(decoder, model.buffers, eligible, ids)
    assert np.max(abs(old - model.buffers['fraction_limit'])) < 1e-12
    isolated, new_details = capacity(decoder, model.buffers, pressure, ids)
    lengths = abs(model.buffers['velocity']).max(1)
    old_px, new_px = old * lengths, isolated * lengths
    rows = [dict(node=int(node), id=ids[node], sourcePressureFeatures=features[node, index:index+2].tolist(),
        velocity=model.buffers['velocity'][node].tolist(), oldCapacityPixels=float(old_px[node]),
        onlyPressureMovingCapacityPixels=float(new_px[node]), originalBinding=old_details[int(node)],
        pressureOnlyBinding=new_details[int(node)]) for node in np.flatnonzero(pressure)]
    results.append(dict(view=view, sourceVisual=spec['expectedVisual'],
        sourceInputHashes=spec['inputs'], observationsSha256=report['observationsSha256'],
        eligibleOwners=int(eligible.sum()), pressureOwners=int(pressure.sum()),
        originalPressureCapacity=distribution(old_px[pressure]),
        originalNoPressureCapacity=distribution(old_px[eligible & ~pressure]),
        pressureOnlyCapacity=distribution(new_px[pressure]), owners=rows))
    print(json.dumps({key: results[-1][key] for key in ('view', 'sourceVisual', 'eligibleOwners',
        'pressureOwners', 'originalPressureCapacity', 'originalNoPressureCapacity', 'pressureOnlyCapacity')}), flush=True)
result = dict(kind='immutable-source-radial-conflict-capacity-diagnostic-v1', views=results,
    sourceBindingSha256=digest(binding_path), canonicalCandidateSha256=digest(canonical),
    nativeMeasurements=0, noFutureGeometryMeasured=True, noActionsSubmitted=True,
    noModelTrained=True, noCandidateSelected=True,
    codeSha256=digest(__file__))
(out / 'diagnostic.json').write_text(json.dumps(result, indent=2) + '\n')
