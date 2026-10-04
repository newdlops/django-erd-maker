"""Find topology-only contexts inside the dominant block; no layout proposals."""
import collections,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
from learned_branch_map import cut_branches

base=Path(__file__).parent;out=base/'pair-separator-analysis1';out.mkdir(exist_ok=False)
report={'positionsProposed':False,'anchorsConsideredPerView':32,'allPairsExhaustive':False,'views':{}}
for view in ['individual','overview']:
    directory=base/'separation-dag-validation2'/view
    ids=[l.split('\t')[0] for l in (directory/'nodes.tsv').read_text().splitlines()];index={n:i for i,n in enumerate(ids)}
    edges=[tuple(l.split('\t')[1:]) for l in (directory/'edges.tsv').read_text().splitlines()]
    graph={n:set() for n in ids}
    for s,t in edges:
        if s!=t:graph[s].add(t);graph[t].add(s)
    block_data=json.loads((base/'block-conflict-analysis1'/(view+'-blocks.json')).read_text())
    block=set(block_data['blocks'][0]['nodeIds'])
    anchors=sorted(block,key=lambda n:(-len(graph[n]&block),index[n]))[:32]
    prior={frozenset(g) for g in cut_branches(ids,edges).values()}
    def flood(seed,allowed):
        found={seed};pending=[seed]
        for n in pending:
            for other in (graph[n]&allowed)-found:found.add(other);pending.append(other)
        return found
    contexts={};split_pairs=0
    for i,a in enumerate(anchors):
        for b in anchors[i+1:]:
            unseen=block-{a,b};parts=[]
            while unseen:
                part=flood(min(unseen,key=index.get),unseen);parts.append(part);unseen-=part
            if len(parts)<2:continue
            split_pairs+=1
            trunk=min(parts,key=lambda row:(-len(row),min(index[n] for n in row)))
            for part in parts:
                if part is trunk:continue
                full=flood(min(part,key=index.get),set(ids)-{a,b})
                assert full&block==part
                if len(full)<2:continue
                key=frozenset(full)
                if key not in contexts:
                    contexts[key]={'nodeIds':sorted(full,key=index.get),'mainBlockNodes':len(part),
                        'internalEdges':sum(s in full and t in full for s,t in edges),
                        'boundaryEdges':sum((s in full)!=(t in full) for s,t in edges),
                        'singleCutContextAlreadyExists':key in prior,'separatorPairs':[]}
                contexts[key]['separatorPairs'].append([a,b])
    ordered=sorted(contexts.values(),key=lambda r:(-r['mainBlockNodes'],-len(r['nodeIds']),index[r['nodeIds'][0]]))
    new=[r for r in ordered if not r['singleCutContextAlreadyExists']]
    (out/(view+'-contexts.json')).write_text(json.dumps({'sourceSha256':block_data['sourceSha256'],'positionsProposed':False,
        'anchors':anchors,'contexts':ordered},indent=2)+'\n')
    report['views'][view]={'sourceSha256':block_data['sourceSha256'],'dominantBlockNodes':len(block),
        'testedPairs':len(anchors)*(len(anchors)-1)//2,'separatingPairs':split_pairs,
        'distinctMultiCardContexts':len(ordered),'newBeyondSingleCutContexts':len(new),
        'mainBlockNodesCoveredByNewContexts':len(set().union(*(set(r['nodeIds'])&block for r in new))) if new else 0,
        'largestNewContexts':[{k:v for k,v in r.items() if k!='nodeIds'}|{'nodes':len(r['nodeIds'])} for r in new[:6]]}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
