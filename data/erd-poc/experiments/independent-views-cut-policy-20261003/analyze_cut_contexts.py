"""Read-only graph-cut context analysis; no coordinates are proposed."""
import collections
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0,'scripts/erd-poc')
from learned_branch_map import graph_branches


def cut_branches(ids,edges):
    graph={node:set() for node in ids};order={node:i for i,node in enumerate(ids)}
    for s,t in edges:
        if s!=t:graph[s].add(t);graph[t].add(s)
    def flood(start,allowed):
        found={start};pending=[start]
        for node in pending:
            for other in graph[node]&allowed-found:found.add(other);pending.append(other)
        return found
    components={};unseen=set(ids)
    for node in ids:
        if node in unseen:
            row=flood(node,unseen);unseen-=row
            for member in row:components[member]=row
    groups={node:{node} for node in ids}
    for node in ids:
        if len(graph[node])<2:continue
        unseen=components[node]-{node};parts=[]
        while unseen:
            part=flood(min(unseen,key=order.get),unseen);parts.append(part);unseen-=part
        if len(parts)<2:continue
        trunk=min(parts,key=lambda row:(-len(row),min(order[n] for n in row)))
        groups[node]=components[node]-trunk
    return groups


def main():
    base=Path(__file__).parent;reports=[]
    for view,stage,source in [
        ('individual','individual-branch-trained2','individual-branch-trained2/candidate.individual.layout.json'),
        ('overview','overview-branch-trained1','overview-branch-policy2/candidate.layout.json')]:
        directory=base/stage
        ids=[s.split('\t')[0] for s in (directory/'nodes.tsv').read_text().splitlines()]
        edges=[s.split('\t')[1:] for s in (directory/'edges.tsv').read_text().splitlines()]
        trees,core=graph_branches(ids,edges);cuts=cut_branches(ids,edges)
        changed=[n for n in ids if cuts[n]!=trees[n]]
        cyclic=[n for n in changed if len(cuts[n]&core)>1]
        rows=[{'root':n,'treeCards':len(trees[n]),'cutCards':len(cuts[n]),
               'twoCoreCards':len(cuts[n]&core),'newMembers':sorted(cuts[n]-trees[n])} for n in cyclic]
        rows.sort(key=lambda r:(-r['twoCoreCards'],-r['cutCards'],r['root']))
        report={'view':view,'sourceSha256':hashlib.sha256((base/source).read_bytes()).hexdigest(),
            'nodes':len(ids),'coreNodes':len(core),'changedContexts':len(changed),'cyclicChangedContexts':len(cyclic),
            'multiCardContexts':sum(len(r)>1 for r in cuts.values()),'maximumContextCards':max(map(len,cuts.values())),
            'cyclicCoreCardsCovered':len(set().union(*(cuts[n]&core for n in cyclic))),
            'cyclicContexts':rows,'positionsProposed':False}
        reports.append(report)
    out=base/'cut-context-analysis.json'
    with out.open('x') as stream:json.dump(reports,stream,indent=2);stream.write('\n')
    print(json.dumps(reports))


if __name__=='__main__':main()
