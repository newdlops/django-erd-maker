"""Read-only conflict attribution and fixed training-mask construction."""
from pathlib import Path
import collections
import hashlib
import json
import sys
import numpy as np
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from joint_grouped_routes import read_pairs
from learned_global_replay import routes

root=Path('.tmp/visualcross-ml-150-750-20261003')
output=root/'localized-conflict-analysis';output.mkdir(exist_ok=False)
reports=[]
for view,stage in [('individual','individual-outward-joint1'),('overview','overview-ray-conditioned1')]:
    directory=root/stage
    source=json.loads((directory/'joint-worker-result.json').read_text())
    positions=read_pairs(directory/'positions.tsv');ids=list(positions);index={name:i for i,name in enumerate(ids)}
    p=np.array(list(positions.values()));sizes=read_pairs(directory/'nodes.tsv');size=np.array([sizes[k] for k in ids])
    edge_rows=[line.split('\t') for line in (directory/'edges.tsv').read_text().splitlines()]
    edges=np.array([[index[s],index[t]] for _,s,t in edge_rows])
    route_map=routes(directory/'routes.tsv');ports=np.array([route_map[e] for e,_,_ in edge_rows])
    low,high=ports.min(1),ports.max(1);direction=ports[:,1]-ports[:,0]
    box_low=p-size/2-10;box_high=p+size/2+10
    crossings=[];hits=[]
    def cross(a,b):return a[...,0]*b[...,1]-a[...,1]*b[...,0]
    for i in range(len(edges)):
        candidates=np.flatnonzero((np.arange(len(edges))>i)&(low[i]<=high).all(1)&(low<=high[i]).all(1))
        a,b=ports[i];other=ports[candidates];d=direction[candidates]
        good=(cross(b-a,other[:,0]-a)*cross(b-a,other[:,1]-a)<-1e-9)&(cross(d,a-other[:,0])*cross(d,b-other[:,0])<-1e-9)
        crossings.extend((i,int(j)) for j in candidates[good])
        candidates=np.flatnonzero((high[i]>box_low).all(1)&(low[i]<box_high).all(1)&(np.arange(len(p))!=edges[i,0])&(np.arange(len(p))!=edges[i,1]))
        lo=np.zeros(len(candidates));hi=np.ones(len(candidates));good=np.ones(len(candidates),dtype=bool)
        for axis in range(2):
            if abs(direction[i,axis])<1e-9:
                good&=(a[axis]>box_low[candidates,axis])&(a[axis]<box_high[candidates,axis])
            else:
                first=(box_low[candidates,axis]-a[axis])/direction[i,axis]
                second=(box_high[candidates,axis]-a[axis])/direction[i,axis]
                lo=np.maximum(lo,np.minimum(first,second));hi=np.minimum(hi,np.maximum(first,second))
                good&=hi-lo>1e-9
        good&=(hi>1e-9)&(lo<1-1e-9)
        hits.extend((i,int(n)) for n in candidates[good])
    assert len(crossings)+len(hits)==source['sourceVisual'],(view,len(crossings),len(hits),source['sourceVisual'])
    node_pressure=collections.Counter();edge_pressure=collections.Counter();incident={n:set() for n in range(len(p))}
    for e,(s,t) in enumerate(edges):incident[int(s)].add(e);incident[int(t)].add(e)
    for a,b in crossings:
        node_pressure.update(map(int,edges[[a,b]].ravel()));edge_pressure.update([a,b])
    for e,n in hits:node_pressure.update([*map(int,edges[e]),n]);edge_pressure[e]+=1
    patches=[]
    for rank,(seed,pressure) in enumerate(edge_pressure.most_common(3)):
        related=collections.Counter()
        for a,b in crossings:
            if seed in (a,b):related.update(map(int,edges[b if a==seed else a]))
        for e,n in hits:
            if e==seed:related[n]+=1
        required=list(map(int,edges[seed]))
        ordered=sorted((n for n in related if n not in required),key=lambda n:(-related[n],-node_pressure[n],n))
        for count in [12,24]:
            selected=required+ordered[:max(0,count-len(required))]
            active=set(selected);active_edges=set().union(*(incident[n] for n in active))
            covered=sum(bool({a,b}&active_edges) for a,b in crossings)+sum(e in active_edges or n in active for e,n in hits)
            specification={'view':view,'sourceSha256':source['sourceSha256'],'sourceVisual':source['sourceVisual'],
                'nodeIds':[ids[n] for n in selected],'seedEdge':edge_rows[seed][0],
                'selection':'fixed endpoints and most frequent counterpart cards of a measured conflict edge',
                'coveredSourceConflicts':covered,'positionsProposed':False,'coordinatesAsTargets':False}
            filename=f'{view}-edge{rank}-n{count}.json'
            (output/filename).write_text(json.dumps(specification,indent=2)+'\n')
            patches.append({'file':filename,'activeNodes':len(selected),'activeEdges':len(active_edges),'coveredSourceConflicts':covered})
    result={'view':view,'sourceSha256':source['sourceSha256'],'sourceVisual':source['sourceVisual'],
        'crossings':len(crossings),'cardHits':len(hits),'exactCountMatchesNativeSource':True,
        'topNodes':[(ids[n],v) for n,v in node_pressure.most_common(12)],
        'topEdges':[(edge_rows[e][0],v) for e,v in edge_pressure.most_common(6)],'patches':patches}
    reports.append(result);print(json.dumps(result))
(output/'analysis.json').write_text(json.dumps(reports,indent=2)+'\n')
