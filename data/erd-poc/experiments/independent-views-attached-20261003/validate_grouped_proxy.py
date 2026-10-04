import hashlib
import json
from pathlib import Path
import subprocess
import sys
import numpy as np

sys.path.insert(0, 'scripts/erd-poc')
from joint_grouped_routes import GroupedRoutes, anchored_ports, read_pairs
from joint_layout_proxy import JointPolicy, LayoutProxy, propagate_port_gradients

root = Path('.tmp/visualcross-ml-150-750-20261003')
directory = root / 'overview-joint1'
checked = 0
rng = np.random.default_rng(527)
positions = rng.normal(size=(12, 2)) * 500
sizes = rng.uniform(20, 90, (12, 2))
edges = np.array([[0, 1], [2, 3], [4, 5], [6, 7], [8, 9], [10, 11]])
offsets = rng.uniform(-.4, .4, (6, 2, 2)) * sizes[edges]
weights = rng.normal(size=(6, 2, 2))
ports, cache = anchored_ports(positions, sizes, edges, offsets)
gradient = np.zeros_like(positions)
propagate_port_gradients(gradient, weights, edges, cache)
for node in range(len(positions)):
    for axis in range(2):
        hi, lo = positions.copy(), positions.copy()
        hi[node, axis] += 1e-4
        lo[node, axis] -= 1e-4
        numeric = np.sum((anchored_ports(hi, sizes, edges, offsets)[0]
                          - anchored_ports(lo, sizes, edges, offsets)[0]) * weights) / 2e-4
        np.testing.assert_allclose(gradient[node, axis], numeric, rtol=1e-6, atol=1e-6)
        checked += 1

pos_map = read_pairs(directory / 'positions.tsv')
size_map = read_pairs(directory / 'nodes.tsv')
ids = list(pos_map)
by_id = {key: i for i, key in enumerate(ids)}
positions = np.array(list(pos_map.values()))
sizes = np.array([size_map[key] for key in ids])
edges = np.array([[by_id[s], by_id[t]] for _, s, t in
                  (line.split('\t') for line in (directory / 'edges.tsv').read_text().splitlines())])
provider = GroupedRoutes(directory)
proxy = LayoutProxy(positions, sizes, edges, 1024., route_provider=provider)
value, gradient = proxy.loss(positions)
# Full loss finite differences cover high-pressure cards, including Leaf cards.
node_indices = np.argsort(np.sum(gradient ** 2, axis=1))[-10:]
for node in node_indices:
    for axis in range(2):
        hi, lo = positions.copy(), positions.copy()
        hi[node, axis] += 1e-4
        lo[node, axis] -= 1e-4
        numeric = (proxy.loss(hi)[0]['total'] - proxy.loss(lo)[0]['total']) / 2e-4
        np.testing.assert_allclose(gradient[node, axis], numeric, rtol=2e-4, atol=2e-4)
        checked += 1

observation = json.loads((directory / 'joint-observations.json').read_text())
features = np.array([row['features'] for row in observation['nodes']])
rejected = JointPolicy.load(root / 'overview-grouped1/joint-policy-0512.npz')
rejected_positions = positions + rejected.forward(features)[0]
penalty, gradient = provider.constraint_loss(rejected_positions)
assert penalty > 0
for node in np.argsort(np.sum(gradient ** 2, axis=1))[-6:]:
    for axis in range(2):
        hi, lo = rejected_positions.copy(), rejected_positions.copy()
        hi[node, axis] += 1e-4
        lo[node, axis] -= 1e-4
        numeric = (provider.constraint_loss(hi)[0] - provider.constraint_loss(lo)[0]) / 2e-4
        np.testing.assert_allclose(gradient[node, axis], numeric, rtol=2e-4, atol=2e-4)
        checked += 1

process = subprocess.Popen([str(root / 'joint-batch-environment-v2'), '--directory', str(directory),
                            '--out', str(root / 'unused-grouped-proxy-validation.tsv'), '--overview-only', '1'],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    initial = json.loads(process.stdout.readline())
    features = np.array([row['features'] for row in initial['nodes']])
    control = JointPolicy(features, positions, sizes, 1024., 527)
    # A zero-head neural control diagnoses route decoding; nothing is saved.
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
report = {'gradientComparisons': checked, 'gradientsVerified': True,
          'activeRepresentationPenaltyVerified': penalty,
          'proxyBinaryVisual': binary, 'nativeCanonicalVisual': native['visual'],
          'canonicalCountsEqual': binary == native['visual'], 'nativeResult': native,
          'proxy': value, 'proposalSource': 'zero-head neural diagnostic control',
          'savedGeometry': False, 'promoted': False,
          'candidatePairs': {key: len(getattr(proxy, key)) for key in ['cross_pairs', 'hit_pairs', 'near_pairs']},
          'sourceHashes': {name: hashlib.sha256((Path('scripts/erd-poc') / name).read_bytes()).hexdigest()
                           for name in ['joint_grouped_routes.py', 'joint_layout_proxy.py']}}
(root / 'grouped-proxy-validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
assert binary == native['visual']
