"""Immutable capacity for translations keeping old endpoint anchors inside."""
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from compact_patch_neural_policy import patch_buffers
from run_anchor_pair_walk import WalkDecoder
from geometry_world_model import digest

root = Path('.tmp/visualcross-ml-150-750-20261004')
out = root/'source-ray-containment1'
out.mkdir(exist_ok=False)
binding_path = root/'geometry-world1/source-binding.json'
binding = json.loads(binding_path.read_text())
assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json')==binding['promotedCandidateSha256']
views = []
for view in ('overview','individual'):
    spec = binding['viewSources'][view]
    source = Path(spec['directory'])
    assert {name:digest(source/name) for name in spec['inputs']}==spec['inputs']
    observations = root/'radial-leaf1'/f'{view}-learning1'/'observations.npz'
    old_report = json.loads((observations.parent/'report.json').read_text())
    assert digest(observations)==old_report['observationsSha256']
    with np.load(observations,allow_pickle=False) as saved:
        nodes = saved['nodes'].copy()
    decoder = WalkDecoder(source)
    p = decoder.provider
    n = len(decoder.positions)
    degree = np.bincount(p.owner_edges.ravel(),minlength=n)
    index = 4 if view=='overview' else 6
    pressure = np.expm1(nodes[:,index])+np.expm1(nodes[:,index+1])
    active = np.flatnonzero((degree>=2)&(pressure>0))
    ids = [line.split('\t')[0] for line in (source/'nodes.tsv').read_text().splitlines()]
    rows = []
    for node in active:
        bounds = patch_buffers(decoder.positions,decoder.sizes,[node],2048.)
        low,high = bounds['dag_lower'][0]/100,bounds['dag_upper'][0]/100
        incident = p.owner_edges==node
        ports = p.base_boundary[incident]
        half = p.endpoint_sizes[incident]/2
        # Every original ray start lies inside its own translated effective
        # rectangle. Sub-cent source bias is represented by that rectangle.
        # Signed bounds contain zero and retain all endpoint starts.
        contain_low = np.minimum(0.,np.max(ports-half,axis=0))
        contain_high = np.maximum(0.,np.min(ports+half,axis=0))
        low = np.maximum(low,np.ceil(contain_low*100-1e-7)/100)
        high = np.minimum(high,np.floor(contain_high*100+1e-7)/100)
        assert (low<=0).all() and (high>=0).all()
        capacity = float(np.maximum(abs(low),abs(high)).max())
        ties = {(int(card),*point) for card,point in zip(p.full_edges[incident],p.original_ports[incident])}
        rows.append(dict(node=int(node),id=ids[node],incidentCount=int(degree[node]),
            sourcePressure=float(pressure[node]),low=low.tolist(),high=high.tolist(),
            capacityPixels=capacity,originalEndpointGroups=len(ties)))
    rows.sort(key=lambda row:(-row['sourcePressure'],row['node']))
    useful = [row for row in rows if row['capacityPixels']>1]
    views.append(dict(view=view,sourceVisual=spec['expectedVisual'],corePressureOwners=len(rows),
        positiveCapacityOwners=sum(row['capacityPixels']>0 for row in rows),
        aboveOnePixelOwners=len(useful),aboveHundredPixelsOwners=sum(row['capacityPixels']>100 for row in rows),
        owners=rows,sourceInputs=spec['inputs'],observationsSha256=digest(observations)))
    print(json.dumps({key:views[-1][key] for key in ('view','sourceVisual','corePressureOwners','positiveCapacityOwners',
        'aboveOnePixelOwners','aboveHundredPixelsOwners')}|dict(largestPressureUseful=useful[:12])),flush=True)
result = dict(kind='source-ray-anchor-containment-capacity-v1',views=views,
    sourceBindingSha256=digest(binding_path),codeSha256=digest(__file__),nativeMeasurements=0,
    noFutureGeometryMeasured=True,noCoordinatesSelected=True,noModelTrained=True)
(out/'diagnostic.json').write_text(json.dumps(result,indent=2)+'\n')
