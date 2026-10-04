"""Source-ray clipping moves a parent and its ports without rerouting the graph."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
assert importlib.util.find_spec('source_ray_clip_policy') is not None,'source-ray joint neural decoder is missing'
from source_ray_clip_policy import SourceRayClipPolicy
from run_anchor_pair_walk import WalkDecoder
from run_anchor_pair_policy import Native,wire
from single_owner_cached_observer_v2 import CachedObserver
from geometry_world_model import digest

parser = argparse.ArgumentParser()
parser.add_argument('--out',type=Path,required=True)
args = parser.parse_args()
args.out.mkdir(parents=True,exist_ok=False)
source = args.out/'source'
source.mkdir()
positions = [(0,0),(500,-200),(1100,200),(1700,0),(140,-800),(140,500),(2100,0),(2600,400)]
sizes = [(120,100)]+[(100,100)]*7
edges = [(0,1),(0,2),(4,5),(6,7)]
routes = ['60,-10 450,-200','60,-10 1050,200','140,-750 140,450','2150,0 2550,400']
values = dict(nodes=''.join(f'n{i}\t{w}\t{h}\n' for i,(w,h) in enumerate(sizes)),
    positions=''.join(f'n{i}\t{x}\t{y}\n' for i,(x,y) in enumerate(positions)),
    edges=''.join(f'e{i}\tn{a}\tn{b}\n' for i,(a,b) in enumerate(edges)),
    routes=''.join(f'e{i}\t{line}\n' for i,line in enumerate(routes)))
for name,value in values.items():
    for prefix in ('','individual.'):
        (source/(prefix+name+'.tsv')).write_text(value)
(source/'components.tsv').write_text(''.join(f'n{i}\tn{i}\n' for i in range(8)))
(source/'groups.tsv').write_text(''.join(f'e{i}\te{i}\n' for i in range(4)))
decoder = WalkDecoder(source)
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
native = Native(environment,source,args.out/'unused.tsv',False,False)
results = []
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    model = SourceRayClipPolicy(nodes,decoder,[0],2048.,127107)
    zero,info = model.forward(nodes)
    assert not np.any(zero)
    baseline = native.request(wire(zero,'MEASURE'))
    assert baseline['legal'] and baseline['visual']==2
    model.p['bo'][1] = 3.
    action,info = model.forward(nodes)
    assert info['selectedOwner']==0 and info['movedOwners']==1
    assert np.max(abs(action[0]))>100
    assert np.any(action[8:]),'the NN body motion must move boundary ports too'
    assert not np.any(action[1:8])
    assert not np.any(action[8:][decoder.provider.owner_edges!=0])
    changed = nodes.copy()
    changed[:,4:8] = 99.
    np.testing.assert_array_equal(model.forward(changed)[0],action)
    observed = CachedObserver(source,decoder)
    lines = observed.decode(action)[2]
    original = decoder.provider.original_ports
    for edge in (0,1):
        np.testing.assert_array_equal(lines[edge,1],original[edge,1])
        vector = original[edge,1]-original[edge,0]
        shift = lines[edge,0]-original[edge,0]
        parameter = float(shift@vector/(vector@vector))
        assert 0<parameter<1
        assert np.linalg.norm(shift-parameter*vector)<.011
    np.testing.assert_array_equal(lines[2:],original[2:])
    result = native.request(wire(action,'MEASURE'))
    assert result['legal'] and result['visual']==1,result
    assert result['hard']==result['individualHard']==result['spacing']==0
    results.append(dict(result=result,info=info))
    audit = dict(status='pass',zeroHeadSourceIdentity=True,originalSourceVisual=2,
        resultingVisual=1,multiRelationshipBodyAndPortsMoved=True,
        originalRaysShortenedWithinCentTolerance=True,unchangedPeerEndpointsVerified=True,
        noWholeComponentPortRemapping=True,noFutureFeaturesUsed=True,
        fullNativeMeasurements=2,nativeTryCalls=0,noCandidatePromoted=True,
        codeSha256=digest(__file__),modelCodeSha256=digest('scripts/erd-poc/source_ray_clip_policy.py'),outputs=results)
    (args.out/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit),flush=True)
finally:
    native.close()
