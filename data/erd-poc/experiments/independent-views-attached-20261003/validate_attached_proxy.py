import hashlib
import json
from pathlib import Path
import subprocess
import sys
import numpy as np
sys.path.insert(0, 'scripts/erd-poc')
from joint_grouped_routes import GroupedRoutes, read_pairs
from joint_layout_proxy import JointPolicy, LayoutProxy

root = Path('.tmp/visualcross-ml-150-750-20261003')
reports = []
for view, stage, expected in [('overview', 'overview-grouped2', 299),
                              ('individual', 'individual-annealed1', 1996)]:
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
    proxy = LayoutProxy(positions, sizes, edges, 512., route_provider=provider)
    value, gradient = proxy.loss(positions)
    node_indices = np.argsort(np.sum(gradient ** 2, axis=1))[-8:]
    leaf_ids = np.flatnonzero(provider.is_leaf)
    if len(leaf_ids):
        leaf_hot = leaf_ids[np.argsort(np.sum(gradient[leaf_ids] ** 2, axis=1))[-4:]]
        node_indices = np.unique(np.concatenate([node_indices, leaf_hot]))
    checked = 0
    for node in node_indices:
        for axis in range(2):
            hi, lo = positions.copy(), positions.copy()
            hi[node, axis] += 1e-4
            lo[node, axis] -= 1e-4
            numeric = (proxy.loss(hi)[0]['total'] - proxy.loss(lo)[0]['total']) / 2e-4
            np.testing.assert_allclose(gradient[node, axis], numeric, rtol=2e-4, atol=2e-4)
            checked += 1
    process = subprocess.Popen([str(root / 'joint-batch-environment-v3'), '--directory', str(directory),
                                '--out', str(root / 'unused-attached-validation.tsv'),
                                '--overview-only', '1' if view == 'overview' else '0', '--attached-ports', '1'],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        initial = json.loads(process.stdout.readline())
        features = np.array([row['features'] for row in initial['nodes']])
        control = JointPolicy(features, positions, sizes, 512., 530)
        command = 'TRY ' + ' '.join(f'{float(v):.12g}' for v in control.forward(features)[0].ravel())
        process.stdin.write(command + '\n')
        process.stdin.flush()
        native = json.loads(process.stdout.readline())
        process.stdin.write('QUIT\n')
        process.stdin.flush()
        assert process.wait(timeout=5) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    binary = value['binaryCrossings'] + value['binaryCardHits']
    report = {'view': view, 'gradientComparisons': checked, 'gradientsVerified': True,
              'proxyBinaryVisual': binary, 'nativeVisual': native['visual'],
              'sourceVisual': initial['visual'], 'baselineScorePreserved': native['visual'] == expected,
              'countsEqual': binary == native['visual'], 'nativeResult': native,
              'savedGeometry': False, 'promoted': False}
    print(json.dumps(report), flush=True)
    assert binary == native['visual'] == initial['visual'] == expected
    assert native['legal'] and native['hard'] == native['individualHard'] == native['spacing'] == 0
    reports.append(report)
result = {'views': reports, 'sourceHashes': {name: hashlib.sha256((Path('scripts/erd-poc') / name).read_bytes()).hexdigest()
          for name in ['joint_grouped_routes.py', 'joint_layout_proxy.py', 'ml_joint_batch_environment.cpp']}}
(root / 'attached-proxy-validation.json').write_text(json.dumps(result, indent=2) + '\n')
