import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, 'scripts/erd-poc')
from joint_grouped_routes import GroupedRoutes, read_pairs
from joint_hard_geometry import FullHardGeometry
from joint_layout_proxy import JointPolicy
from learned_global_replay import rounded

root = Path('.tmp/visualcross-ml-150-750-20261003')
reports = []
for view, stage in [('overview', 'overview-attached1'), ('individual', 'individual-attached1')]:
    directory = root / stage
    positions_map = read_pairs(directory / 'positions.tsv')
    sizes_map = read_pairs(directory / 'nodes.tsv')
    ids = list(positions_map)
    by_id = {key: i for i, key in enumerate(ids)}
    positions = np.array(list(positions_map.values()))
    sizes = np.array([sizes_map[key] for key in ids])
    edges = np.array([[by_id[s], by_id[t]] for _, s, t in
                      (line.split('\t') for line in (directory / 'edges.tsv').read_text().splitlines())])
    provider = GroupedRoutes(directory, attached=True)
    proxy = FullHardGeometry(provider, positions, sizes, edges, 1024., 1000.)
    baseline, baseline_gradient = proxy.loss(positions)
    assert baseline['total'] < 1e-12, baseline
    assert np.abs(baseline_gradient).max() < 1e-6
    batch = json.loads((directory / 'joint-batches.jsonl').read_text().splitlines()[-1])
    assert hashlib.sha256(Path(batch['checkpoint']).read_bytes()).hexdigest() == batch['checkpointSha256']
    features = np.array([row['features'] for row in json.loads((directory / 'joint-observations.json').read_text())['nodes']])
    model = JointPolicy.load(batch['checkpoint'])
    delta = model.forward(features)[0]
    delta = np.array([[rounded(float(f'{value:.12g}')) for value in row] for row in delta])
    rejected = positions + delta
    active, gradient = proxy.loss(rejected)
    assert active['total'] > 0
    count = lambda part: 2*part['binaryAdjacentCrossings']+part['binaryOwnCardHits']
    assert count(active.get('full', active)) == batch['result']['individualHard']
    assert count(active.get('projected', active)) == batch['result']['hard']
    checked = 0
    for node in np.argsort(np.sum(gradient**2, axis=1))[-10:]:
        for axis in range(2):
            hi, lo = rejected.copy(), rejected.copy()
            hi[node, axis] += 1e-4
            lo[node, axis] -= 1e-4
            numeric = (proxy.loss(hi)[0]['total'] - proxy.loss(lo)[0]['total']) / 2e-4
            np.testing.assert_allclose(gradient[node, axis], numeric, rtol=3e-4, atol=3e-4)
            checked += 1
    report = {'view': view, 'zeroBaselinePenalty': baseline['total'], 'baseline': baseline,
              'active': active, 'gradientComparisons': checked, 'gradientsVerified': True,
              'nativeRejectedBatch': batch['result'], 'checkpointSha256': batch['checkpointSha256'],
              'fullAdjacentPairs': len(proxy.proxy.cross_pairs), 'fullOwnPairs': len(proxy.proxy.hit_pairs),
              'includesProjectedView': proxy.projected is not None}
    report['nativeHardCountDecompositionVerified'] = True
    print(json.dumps(report), flush=True)
    reports.append(report)
result = {'views': reports, 'hardWeight': 1000,
          'boundaryContactLossImplemented': False, 'nativeGeometryRemainsAuthoritative': True,
          'gradientsAreForContinuousLoss': True,
          'quantizedTrainingUsesStraightThroughEstimator': True,
          'implementationHashes': {name: hashlib.sha256((Path('scripts/erd-poc') / name).read_bytes()).hexdigest()
          for name in ['joint_layout_proxy.py', 'joint_grouped_routes.py', 'joint_hard_geometry.py']}}
(root / 'hard-geometry-validation.json').write_text(json.dumps(result, indent=2) + '\n')
