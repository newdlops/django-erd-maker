"""Read-only graph/geometry diagnosis; never proposes or changes coordinates."""
import collections
import json
from pathlib import Path
import numpy as np

ROOT = Path('.tmp/visualcross-ml-targets-20261003')

def analyze(name, source, suffix):
    sizes = [row.split('\t') for row in (source/'nodes.tsv').read_text().splitlines()]
    ids = {row[0]: i for i, row in enumerate(sizes)}
    wh = np.array([[float(x) for x in row[1:]] for row in sizes])
    positions = {r[0]: list(map(float, r[1:])) for r in
                 (row.split('\t') for row in (source/suffix).read_text().splitlines())}
    xy = np.array([positions[row[0]] for row in sizes])
    edges = [row.split('\t') for row in (source/'edges.tsv').read_text().splitlines()]
    endpoints = np.array([[ids[row[1]], ids[row[2]]] for row in edges])
    routes = {key: list(map(float, text.replace(',', ' ').split())) for key,text in
              (row.split('\t') for row in (source/(suffix+'.routes.tsv')).read_text().splitlines())}
    lines = np.array([routes[row[0]] for row in edges]).reshape(-1, 2, 2)
    adj = [set() for _ in ids]
    for u,v in endpoints:
        adj[u].add(v); adj[v].add(u)
    degree = np.array(list(map(len, adj)))
    remaining=degree.copy(); fringe=np.zeros(len(ids),dtype=bool)
    queue=collections.deque(np.flatnonzero(remaining<2))
    while queue:
        u=queue.popleft()
        if fringe[u]: continue
        fringe[u]=True
        for v in adj[u]:
            remaining[v]-=1
            if not fringe[v] and remaining[v]<2: queue.append(v)
    seen=set(); components=[]
    for u in range(len(ids)):
        if u in seen: continue
        stack=[u];seen.add(u);count=0
        while stack:
            v=stack.pop();count+=1
            for w in adj[v]:
                if w not in seen: seen.add(w);stack.append(w)
        components.append(count)
    def orient(a,b,c):
        return (b[...,0]-a[...,0])*(c[...,1]-a[...,1])-(b[...,1]-a[...,1])*(c[...,0]-a[...,0])
    cross=collections.Counter();hit=collections.Counter();node_pressure=np.zeros(len(ids),dtype=int)
    edge_fringe=fringe[endpoints].any(axis=1)
    low,high=xy-wh/2-10,xy+wh/2+10
    for i,(a,b) in enumerate(lines):
        rest=lines[i+1:]
        mask=(orient(a,b,rest[:,0])*orient(a,b,rest[:,1]) < -1e-9)&(orient(rest[:,0],rest[:,1],a)*orient(rest[:,0],rest[:,1],b) < -1e-9)
        for j in np.flatnonzero(mask)+i+1:
            key='touches_fringe' if edge_fringe[i] or edge_fringe[j] else 'core_only'
            cross[key]+=1
            np.add.at(node_pressure, endpoints[[i,j]].ravel(), 1)
        lo=np.zeros(len(ids));hi=np.ones(len(ids));valid=np.ones(len(ids),dtype=bool)
        for k in range(2):
            d=b[k]-a[k]
            if abs(d)<1e-9:
                valid &= (a[k]>low[:,k])&(a[k]<high[:,k])
            else:
                first=(low[:,k]-a[k])/d;second=(high[:,k]-a[k])/d
                lo=np.maximum(lo,np.minimum(first,second));hi=np.minimum(hi,np.maximum(first,second))
        valid &= (hi-lo>1e-9)&(hi>1e-9)&(lo<1-1e-9)
        valid[endpoints[i]]=False
        for j in np.flatnonzero(valid):
            hit['card_fringe' if fringe[j] else 'card_core']+=1
            hit['edge_fringe' if edge_fringe[i] else 'edge_core']+=1
            node_pressure[j]+=3;np.add.at(node_pressure,endpoints[i],1)
    return dict(view=name,nodes=len(ids),edges=len(edges),simpleEdges=int(degree.sum()/2),
                components=sorted(components,reverse=True),coreNodes=int((~fringe).sum()),fringeNodes=int(fringe.sum()),
                degreeHistogram=dict(collections.Counter(map(int,degree))),edgeCrossings=dict(cross),nodeHits=dict(hit),
                topPressure=[dict(index=int(i),degree=int(degree[i]),fringe=bool(fringe[i]),pressure=int(node_pressure[i])) for i in np.argsort(-node_pressure)[:12]])

reports=[analyze('individual',ROOT/'individual43','learned.tsv'),
         analyze('overview',ROOT/'overview-final-moving1','learned.tsv')]
out=Path('.tmp/visualcross-ml-150-750-20261003/structure.json')
out.write_text(json.dumps(reports,indent=2)+'\n')
for row in reports:
    row['components']=row['components'][:8]
    print(json.dumps(row))
