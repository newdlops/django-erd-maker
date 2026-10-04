"""NN ray translations retain zero-head geometry, ties, frame and spacing."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from radial_leaf_policy import RadialLeafPolicy
from geometry_world_model import digest
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder

parser = argparse.ArgumentParser()
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
source = args.out / 'source'
source.mkdir()
values = dict(nodes='n0\t120\t100\nn1\t100\t100\nn2\t100\t100\nn3\t100\t100\n',
    positions='n0\t0\t0\nn1\t500\t-200\nn2\t1100\t200\nn3\t1700\t0\n',
    edges='e0\tn0\tn1\ne1\tn0\tn2\n', routes='e0\t60,-10 450,-200\ne1\t60,-10 1050,200\n')
for kind, content in values.items():
    for prefix in ('', 'individual.'):
        (source / (prefix + kind + '.tsv')).write_text(content)
(source / 'components.tsv').write_text('n0\tn0\nn1\tn1\nn2\tn2\nn3\tn3\n')
(source / 'groups.tsv').write_text('e0\te0\ne1\te1\n')
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
decoder = WalkDecoder(source)
native = Native(environment, source, args.out / 'unused.tsv', False, False)
results = []
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    model = RadialLeafPolicy(nodes, decoder, 119107, 4096.)
    initial = model.forward(nodes)[0]
    assert not np.any(initial)
    assert model.buffers['eligible'].tolist() == [False, True, True, False]
    source_low = (decoder.positions - decoder.sizes / 2).min(0)
    source_high = (decoder.positions + decoder.sizes / 2).max(0)
    for bias in (.7, 50.):
        model.p['bo'][:] = bias
        action, info = model.forward(nodes)
        assert not np.any(action[4:])
        assert not np.any(action[[0, 3]])
        assert float(abs(action[:4]).max()) > 100.
        centers = decoder.positions + action[:4]
        assert ((centers - decoder.sizes / 2).min(0) >= source_low - 1e-8).all()
        assert ((centers + decoder.sizes / 2).max(0) <= source_high + 1e-8).all()
        for edge in range(2):
            leaf = edge + 1
            assert np.dot(action[leaf], model.buffers['velocity'][leaf]) > 0
        result = native.request(wire(action, 'MEASURE'))
        assert result['legal'] and result['hard'] == result['individualHard'] == result['spacing'] == 0, result
        results.append(dict(bias=bias, result=result, info=info))
    audit = dict(status='pass', zeroHeadSourceIdentity=True, onlySingleRelationshipOwnersMoved=True,
        sourceStarParentEndpointsUnchanged=True, originalCardDimensionsAndFrameRetained=True,
        fullNativeMeasurements=2, nativeTryCalls=0, candidatePromoted=False,
        modelCodeSha256=digest('scripts/erd-poc/radial_leaf_policy.py'), codeSha256=digest(__file__), outputs=results)
    (args.out / 'audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps(audit), flush=True)
finally:
    native.close()
