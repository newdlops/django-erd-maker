"""Source-conflict gating is immutable; NN still owns every translation."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from pressure_radial_leaf_policy import PressureRadialLeafPolicy
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder
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
outputs = []
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    assert native.initial['visual'] > 0
    for view in ('individual', 'overview'):
        model = PressureRadialLeafPolicy(nodes, decoder, 119507, 4096., view)
        assert not np.any(model.forward(nodes)[0])
        assert model.buffers['eligible'][7]
        assert not model.buffers['source_pressure'][7]
        assert model.buffers['source_pressure'][1]
        for bias in (.7, 50.):
            model.p['bo'][:] = bias
            action, info = model.forward(nodes)
            assert not np.any(action[8:])
            assert not np.any(action[7])
            assert not np.any(action[:8][~model.buffers['source_pressure']])
            assert info['movedOwners'] > 0
            assert info['maximumDisplacementPixels'] > 100.
            # Inference cannot react to an injected future feature/cost row.
            altered = nodes.copy()
            altered[:,4:8] = 99.
            np.testing.assert_array_equal(model.forward(altered)[0], action)
            result = native.request(wire(action, 'MEASURE'))
            assert result['legal'] and result['hard'] == result['individualHard'] == result['spacing'] == 0, result
            outputs.append(dict(view=view, bias=bias, result=result, info=info))
    audit = dict(status='pass', sourceFeaturesFromNative=True, immutableSourcePressureMask=True,
        zeroHeadSourceIdentity=True, inactiveCardsAndParentEndpointsUnchanged=True,
        fullNativeMeasurements=4, nativeTryCalls=0, candidatePromoted=False,
        modelCodeSha256=digest('scripts/erd-poc/pressure_radial_leaf_policy.py'),
        radialCodeSha256=digest('scripts/erd-poc/radial_leaf_policy.py'),
        codeSha256=digest(__file__), outputs=outputs)
    (args.out / 'audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps(audit), flush=True)
finally:
    native.close()
