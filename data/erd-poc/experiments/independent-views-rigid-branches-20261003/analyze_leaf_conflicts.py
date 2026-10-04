"""Measure degree-one conflict contexts; never propose coordinates."""
import collections
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,'scripts/erd-poc')
from joint_grouped_routes import read_pairs
from learned_global_replay import routes

root=Path(__file__).parent
output=root/'leaf-conflict-analysis';output.mkdir(exist_ok=False)
reports=[]
for view,stage in [('individual','individual-outward-joint1'),('overview','overview-ray-conditioned1')]:
    directory=root/stage
    source=json.loads((directory/'joint-worker-result.json').read_text())
    positions=read_pairs(directory/'positions.tsv');ids=list(positions);index={key:i for i,key in enumerate(ids)}
    p=np.array(list(positions.values()));sizes=read_pairs(directory/'nodes.tsv');size=np.array([sizes[k] for k in ids])
    edge_rows=[line.split('\t') for line in (directory/'edges.tsv').read_text().splitlines()]
    edges=np.array([[index[s],index[t]] for _,s,t in edge_rows])
    route_map=routes(directory/'routes.tsv');ports=np.array([route_map[e] for e,_,_ in edge_rows])
    low,high=ports.min(1),ports.max(1);direction=ports[:,1]-ports[:,0]
    box_low=p-size/2-10;box_high=p+size/2+10
    edge_pressure=collections.Counter();node_hits=collections.Counter();crossings=hits=0
    def cross(a,b):return a[...,0]*b[...,1]-a[...,1]*b[...,0]
    for i in range(len(edges)):
        candidates=np.flatnonzero((np.arange(len(edges))>i)&(low[i]<=high).all(1)&(low<=high[i]).all(1))
        a,b=ports[i];other=ports[candidates];d=direction[candidates]
        good=(cross(b-a,other[:,0]-a)*cross(b-a,other[:,1]-a)<-1e-9)&(cross(d,a-other[:,0])*cross(d,b-other[:,0])<-1e-9)
        matches=candidates[good];crossings+=len(matches);edge_pressure[i]+=len(matches);edge_pressure.update(map(int,matches))
        candidates=np.flatnonzero((high[i]>box_low).all(1)&(low[i]<box_high).all(1)&(np.arange(len(p))!=edges[i,0])&(np.arange(len(p))!=edges[i,1]))
        lo=np.zeros(len(candidates));hi=np.ones(len(candidates));good=np.ones(len(candidates),dtype=bool)
        for axis in range(2):
            if abs(direction[i,axis])<1e-9:
                good&=(a[axis]>box_low[candidates,axis])&(a[axis]<box_high[candidates,axis])
            else:
                first=(box_low[candidates,axis]-a[axis])/direction[i,axis]
                second=(box_high[candidates,axis]-a[axis])/direction[i,axis]
                lo=np.maximum(lo,np.minimum(first,second));hi=np.minimum(hi,np.maximum(first,second));good&=hi-lo>1e-9
        good&=(hi>1e-9)&(lo<1-1e-9)
        matches=candidates[good];hits+=len(matches);edge_pressure[i]+=len(matches);node_hits.update(map(int,matches))
    assert crossings+hits==source['sourceVisual']
    incident={n:[] for n in range(len(p))}
    for e,(s,t) in enumerate(edges):incident[int(s)].append(e);incident[int(t)].append(e)
    ranked=sorted([n for n in incident if len(incident[n])==1],
                  key=lambda n:(-edge_pressure[incident[n][0]]-node_hits[n],ids[n]))
    rows=[]
    for rank,n in enumerate(ranked[:12]):
        edge=incident[n][0];pressure=edge_pressure[edge]+node_hits[n]
        record={'node':ids[n],'edge':edge_rows[edge][0],'conflicts':pressure,
                'edgeConflicts':edge_pressure[edge],'cardHits':node_hits[n]}
        rows.append(record)
        patch={'view':view,'sourceSha256':source['sourceSha256'],'sourceVisual':source['sourceVisual'],
               'nodeIds':[ids[n]],'seedEdge':edge_rows[edge][0],
               'selection':'one-edge nodes ranked by exact incident-route conflicts plus unrelated hits on their cards',
               'coveredSourceConflicts':pressure,'positionsProposed':False,'coordinatesAsTargets':False}
        (output/f'{view}-leaf{rank}.json').write_text(json.dumps(patch,indent=2)+'\n')
    reports.append({'view':view,'crossings':crossings,'cardHits':hits,'degreeOneNodes':len(ranked),
                    'exactCountMatchesNativeSource':True,'ranked':rows})
(output/'analysis.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps(reports))
