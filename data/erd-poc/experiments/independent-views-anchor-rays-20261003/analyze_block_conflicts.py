"""Attribute existing conflicts to graph blocks without proposing geometry."""
import collections,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc');sys.setrecursionlimit(10000)
import numpy as np
from joint_grouped_routes import read_pairs
from learned_global_replay import routes

def blocks(count,edges):
    pairs=sorted({tuple(sorted(map(int,e))) for e in edges if e[0]!=e[1]})
    graph=[[] for _ in range(count)]
    for e,(s,t) in enumerate(pairs):graph[s].append((t,e));graph[t].append((s,e))
    discovery=[-1]*count;low=[0]*count;stack=[];result=[];clock=0
    def visit(node,parent=-1):
        nonlocal clock
        discovery[node]=low[node]=clock;clock+=1
        for other,edge in graph[node]:
            if edge==parent:continue
            if discovery[other]<0:
                stack.append(edge);visit(other,edge);low[node]=min(low[node],low[other])
                if low[other]>=discovery[node]:
                    component=[]
                    while True:
                        item=stack.pop();component.append(item)
                        if item==edge:break
                    result.append(component)
            elif discovery[other]<discovery[node]:
                stack.append(edge);low[node]=min(low[node],discovery[other])
    for n in range(count):
        if discovery[n]<0:visit(n)
    assert not stack and sum(map(len,result))==len(pairs)
    result.sort(key=lambda row:(-len(row),min(row)))
    mapping={pairs[e]:b for b,row in enumerate(result) for e in row}
    vertices=[{n for e in row for n in pairs[e]} for row in result]
    return mapping,vertices,[len(r) for r in result]

fixture=[(0,1),(1,2),(2,0),(2,3),(3,4),(4,2),(4,5),(0,1)]
_,vertices,counts=blocks(7,fixture)
assert {frozenset(row) for row in vertices}=={frozenset([0,1,2]),frozenset([2,3,4]),frozenset([4,5])}
base=Path(__file__).parent;out=base/'block-conflict-analysis1';out.mkdir(exist_ok=False)
report={'positionsProposed':False,'crossingLowerBoundProved':False,'graphFixturePassed':True,'views':{}}
for view in ['individual','overview']:
    directory=base/'separation-dag-validation2'/view;previous=base/(view+'-bounded-trained2')
    source=previous/('candidate.individual.layout.json' if view=='individual' else 'candidate.layout.json')
    audit=json.loads((previous/('individual.audit.json' if view=='individual' else 'product.audit.json')).read_text())
    sha=hashlib.sha256(source.read_bytes()).hexdigest();assert sha==audit['candidateSha256']
    positions=read_pairs(directory/'positions.tsv');ids=list(positions);index={k:i for i,k in enumerate(ids)}
    p=np.array(list(positions.values()));sizes=read_pairs(directory/'nodes.tsv');size=np.array([sizes[k] for k in ids])
    er=[l.split('\t') for l in (directory/'edges.tsv').read_text().splitlines()]
    edges=np.array([[index[s],index[t]] for _,s,t in er]);route_map=routes(directory/'routes.tsv')
    ports=np.array([route_map[e] for e,_,_ in er]);low,high=ports.min(1),ports.max(1);direction=ports[:,1]-ports[:,0]
    box_low=p-size/2-10;box_high=p+size/2+10;crossings=[];hits=[]
    cross=lambda a,b:a[...,0]*b[...,1]-a[...,1]*b[...,0]
    for i in range(len(edges)):
        candidates=np.flatnonzero((np.arange(len(edges))>i)&(low[i]<=high).all(1)&(low<=high[i]).all(1))
        a,b=ports[i];other=ports[candidates];d=direction[candidates]
        good=(cross(b-a,other[:,0]-a)*cross(b-a,other[:,1]-a)<-1e-9)&(cross(d,a-other[:,0])*cross(d,b-other[:,0])<-1e-9)
        crossings.extend((i,int(j)) for j in candidates[good])
        candidates=np.flatnonzero((high[i]>box_low).all(1)&(low[i]<box_high).all(1)&(np.arange(len(p))!=edges[i,0])&(np.arange(len(p))!=edges[i,1]))
        lo=np.zeros(len(candidates));hi=np.ones(len(candidates));good=np.ones(len(candidates),dtype=bool)
        for axis in range(2):
            if abs(direction[i,axis])<1e-9:good&=(a[axis]>box_low[candidates,axis])&(a[axis]<box_high[candidates,axis])
            else:
                first=(box_low[candidates,axis]-a[axis])/direction[i,axis];second=(box_high[candidates,axis]-a[axis])/direction[i,axis]
                lo=np.maximum(lo,np.minimum(first,second));hi=np.minimum(hi,np.maximum(first,second));good&=hi-lo>1e-9
        good&=(hi>1e-9)&(lo<1-1e-9);hits.extend((i,int(n)) for n in candidates[good])
    assert len(crossings)+len(hits)==audit['visualCrossings'],(view,len(crossings),len(hits),audit['visualCrossings'])
    mapping,vertices,unique_counts=blocks(len(p),edges)
    edge_blocks=[mapping[tuple(sorted(map(int,e)))] for e in edges]
    within=collections.Counter();between=collections.Counter();inside_hits=collections.Counter();outside_hits=collections.Counter()
    for a,b in crossings:
        x,y=edge_blocks[a],edge_blocks[b]
        if x==y:within[x]+=1
        else:between[tuple(sorted([x,y]))]+=1
    for e,n in hits:
        block=edge_blocks[e]
        (inside_hits if n in vertices[block] else outside_hits)[block]+=1
    edge_counts=collections.Counter(edge_blocks)
    block_rows=[{'block':b,'nodes':len(nodes),'uniqueEdges':unique_counts[b],'relations':edge_counts[b],
        'withinBlockCrossings':within[b],'hitsOnBlockCards':inside_hits[b],'hitsOnOtherCards':outside_hits[b],
        'nodeIds':[ids[n] for n in sorted(nodes)]} for b,nodes in enumerate(vertices)]
    (out/(view+'-blocks.json')).write_text(json.dumps({'sourceSha256':sha,'blocks':block_rows},indent=2)+'\n')
    report['views'][view]={'sourceSha256':sha,'sourceVisual':audit['visualCrossings'],'crossings':len(crossings),
        'cardHits':len(hits),'exactNativeCountMatches':True,'blocks':len(vertices),
        'withinBlockCrossings':sum(within.values()),'betweenBlockCrossings':sum(between.values()),
        'hitsOnSameBlockCards':sum(inside_hits.values()),'hitsOnOtherBlockCards':sum(outside_hits.values()),
        'largestBlocks':[{k:v for k,v in row.items() if k!='nodeIds'} for row in block_rows[:8]],
        'topBlockPairCrossings':[{'blocks':list(pair),'crossings':count} for pair,count in between.most_common(8)]}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
