"""Read-only diagnosis of frozen neural endpoint constraints."""
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from joint_neural_ports import JointPortPolicy, PerimeterRoutes
from joint_layout_proxy import signed_distance
from joint_hard_geometry import FullHardGeometry
from joint_grouped_routes import read_pairs
from learned_global_replay import rounded

root=Path('.tmp/visualcross-ml-150-750-20261003')
reports=[]
for name in ['individual-active-depth1', 'overview-active-depth1']:
    directory=root/name
    report=json.loads((directory/'joint-worker-result.json').read_text())
    checkpoint=directory/f"joint-policy-{report['iterations']:04d}.npz"
    provider=PerimeterRoutes(directory)
    positions=np.array(list(read_pairs(directory/'positions.tsv').values()))
    sizes=np.array(list(read_pairs(directory/'nodes.tsv').values()))
    indices={key:i for i,key in enumerate(provider.physical_ids)}
    edges=np.array([[indices[s],indices[t]] for _,s,t in (line.split('\t') for line in (directory/'edges.tsv').read_text().splitlines())])
    model=JointPortPolicy.load(checkpoint)
    actions=model.forward(np.load(directory/'joint-input-features.npy'))[0]
    decoded=np.array([[rounded(float(f'{v:.12g}')) for v in row] for row in actions[:len(positions)]])
    moved=positions+decoded
    phases=np.array([[float(f'{v:.12g}') for v in row] for row in actions[len(positions):]])
    provider.set_actions(phases,quantized=True,positions=moved)
    hard=FullHardGeometry(provider,positions,sizes,edges,report['maxStep'],report['hardGeometryWeight'])
    full_positions=moved[provider.owner]+provider.offsets
    ports=provider.full_provider.forward(full_positions,provider.sizes)[0]
    cp=hard.proxy.cross_pairs
    a,b=provider.full_edges[cp[:,0]],provider.full_edges[cp[:,1]]
    shared=np.zeros(len(cp),dtype=bool)
    for first in range(2):
        for second in range(2):
            shared|=(a[:,first]==b[:,second]) & (provider.original_ports[cp[:,0],first]==provider.original_ports[cp[:,1],second]).all(1)
    first,second=ports[cp[:,0]],ports[cp[:,1]]
    margins=[]
    for points,line in [(first,second),(second,first)]:
        u=signed_distance(points[:,0],line[:,0],line[:,1])[0]
        v=signed_distance(points[:,1],line[:,0],line[:,1])[0]
        margins.append(-u*v/(abs(u)+abs(v)+1e-6))
    for axis in range(2):
        margins.append(np.minimum(first[:,:,axis].max(1)-second[:,:,axis].min(1),second[:,:,axis].max(1)-first[:,:,axis].min(1)))
    depth=np.maximum(0,np.stack(margins,1).min(1))
    positive=depth>0
    groups={}
    for i,(owner,point) in enumerate(zip(provider.full_edges.ravel(),provider.original_ports.reshape(-1,2))):
        groups.setdefault((int(owner),*point),[]).append(i)
    tied=[g for g in groups.values() if len(g)>1]
    results={'stage':name,'checkpoint':str(checkpoint),'sourceEndpointGroups':len(groups),
             'sharedGroups':len(tied),'sharedEndpoints':sum(map(len,tied)),
             'largestSharedGroup':max(map(len,tied),default=0),
             'fullAdjacentPairs':len(cp),'sourceSharedPairs':int(shared.sum()),
             'positiveAdjacentPairs':int(positive.sum()),
             'positiveFromSharedPairs':int((positive&shared).sum()),
             'positiveDepths':depth[positive].tolist(),
             'finalNative':report['history'][-1]['geometry'],
             'coordinatesModified':False}
    reports.append(results)
    print(json.dumps(results))
(root/'active-depth-constraint-diagnosis.json').write_text(json.dumps(reports,indent=2)+'\n')
