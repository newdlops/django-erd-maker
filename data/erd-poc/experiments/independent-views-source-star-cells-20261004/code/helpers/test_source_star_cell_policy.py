"""A neural source-cell output can move a multi-edge owner without hard faults."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
assert importlib.util.find_spec('source_star_cell_policy') is not None, 'source-cell neural decoder is missing'
from source_star_cell_policy import SourceStarCellPolicy
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder
from single_owner_cached_observer_v2 import CachedObserver
from geometry_world_model import digest

parser = argparse.ArgumentParser()
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
source = args.out / 'source'
source.mkdir()
positions = [(0,0),(500,-200),(1100,200),(1700,0),(250,-800),(250,500),(2100,0),(2600,400)]
sizes = [(120,100)] + [(100,100)] * 7
edges = [(0,1),(0,2),(4,5),(6,7)]
routes = ['60,-10 450,-200', '60,-10 1050,200', '250,-750 250,450', '2150,0 2550,400']
values = dict(nodes=''.join(f'n{i}\t{w}\t{h}\n' for i,(w,h) in enumerate(sizes)),
    positions=''.join(f'n{i}\t{x}\t{y}\n' for i,(x,y) in enumerate(positions)),
    edges=''.join(f'e{i}\tn{a}\tn{b}\n' for i,(a,b) in enumerate(edges)),
    routes=''.join(f'e{i}\t{line}\n' for i,line in enumerate(routes)))
for name, value in values.items():
    for prefix in ('', 'individual.'):
        (source / (prefix + name + '.tsv')).write_text(value)
(source / 'components.tsv').write_text(''.join(f'n{i}\tn{i}\n' for i in range(8)))
(source / 'groups.tsv').write_text(''.join(f'e{i}\te{i}\n' for i in range(4)))
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
decoder = WalkDecoder(source)
native = Native(environment, source, args.out / 'unused.tsv', False, False)
results = []
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    model = SourceStarCellPolicy(nodes, decoder, np.array([0,4],dtype=np.int32), 1024., 121107)
    zero, _ = model.forward(nodes)
    assert not np.any(zero), 'zero NN head must retain original coordinates and ports'
    baseline = native.request(wire(zero, 'MEASURE'))
    assert baseline['legal'] and baseline['visual'] == native.initial['visual']
    # A shared NN selector, not a per-card coordinate parameter, selects n0.
    difference = model.embedding[0] - model.embedding[1]
    model.p['wo'][:,0] = difference
    model.p['bo'][1] = .7
    action, info = model.forward(nodes)
    assert info['selectedOwner'] == 0
    assert np.max(abs(action[0])) > 10, 'the multi-edge owner must actually move'
    assert np.count_nonzero(np.any(action[:8] != 0,axis=1)) == 1
    assert not np.any(action[8:]), 'original card-relative port phases must be retained'
    altered = nodes.copy()
    altered[:,4:8] = 99.
    np.testing.assert_array_equal(model.forward(altered)[0], action)
    observed = CachedObserver(source, decoder)
    full = observed.decode(action)[2]
    np.testing.assert_array_equal(full[0,0], full[1,0])
    for bias in (.7, 50.):
        model.p['bo'][1] = bias
        proposed, info = model.forward(nodes)
        result = native.request(wire(proposed, 'MEASURE'))
        assert result['legal'] and result['hard'] == result['individualHard'] == result['spacing'] == 0, result
        results.append(dict(bias=bias, result=result, info=info))
    # Switch NN selection to a different card without changing decoder buffers.
    model.p['wo'][:,0] = -difference
    model.p['bo'][1] = .7
    second, info = model.forward(nodes)
    assert info['selectedOwner'] == 4 and not np.any(second[0])
    assert np.any(second[4])
    result = native.request(wire(second, 'MEASURE'))
    assert result['legal'] and result['hard'] == result['individualHard'] == result['spacing'] == 0, result
    results.append(dict(selector='second-owner',result=result,info=info))
    audit = dict(status='pass', zeroHeadSourceIdentity=True, multiRelationshipOwnerMoved=True,
        selectionControlledBySharedNeuralWeights=True, originalPortTiesRetained=True,
        fullNativeMeasurements=4, nativeTryCalls=0, noFutureFeaturesUsed=True,
        codeSha256=digest(__file__), actorSha256=digest('scripts/erd-poc/source_star_cell_policy.py'), outputs=results)
    (args.out / 'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit),flush=True)
finally:
    native.close()
