"""Frozen graph contexts for neural translations; never computes positions."""
import collections
import hashlib
import json
from pathlib import Path
from learned_global_replay import INPUT_FILES


def graph_branches(ids, edges):
    graph={node:set() for node in ids}
    for source,target in edges:
        assert source in graph and target in graph
        if source!=target:graph[source].add(target);graph[target].add(source)
    core=set(ids);degree={node:len(row) for node,row in graph.items()}
    queue=collections.deque(node for node in ids if degree[node]<2)
    while queue:
        node=queue.popleft()
        if node not in core:continue
        core.remove(node)
        for other in graph[node]&core:
            degree[other]-=1
            if degree[other]<2:queue.append(other)
    groups={node:{node} for node in ids};owners={}
    for root in ids:
        if root not in core:continue
        pending=[root]
        while pending:
            node=pending.pop()
            for other in graph[node]-core-groups[root]:
                assert other not in owners
                owners[other]=root;groups[root].add(other);pending.append(other)
    return groups,core


def cut_branches(ids, edges):
    """Keep a root and every component except the largest after removing it."""
    graph={node:set() for node in ids};order={node:i for i,node in enumerate(ids)}
    for source,target in edges:
        assert source in graph and target in graph
        if source!=target:graph[source].add(target);graph[target].add(source)
    def flood(start, allowed):
        found={start};pending=[start]
        for node in pending:
            for other in (graph[node]&allowed)-found:
                found.add(other);pending.append(other)
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


def prepare_branch_map(directory, source_sha, mode='core'):
    assert mode in ('core','cut','pair-cut')
    ids=[row.split('\t')[0] for row in (directory/'nodes.tsv').read_text().splitlines()]
    edges=[row.split('\t')[1:] for row in (directory/'edges.tsv').read_text().splitlines()]
    groups,core=graph_branches(ids,edges)
    if mode=='cut':groups=cut_branches(ids,edges)
    if mode=='pair-cut':
        from learned_pair_contexts import pair_separator_contexts
        groups={f'context:{i}':row for i,row in enumerate(pair_separator_contexts(ids,edges))}
    order={node:i for i,node in enumerate(ids)}
    path=directory/'branches.tsv'
    with path.open('x') as output:
        for node in groups:output.write('\t'.join([node,*sorted(groups[node],key=order.get)])+'\n')
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report={'schema':'source-bound-pair-contexts-v1' if mode=='pair-cut' else 'source-bound-graph-branches-v2' if mode=='cut' else 'source-bound-graph-branches-v1',
            'branchMode':mode,'sourceSha256':source_sha,
            'branchMapSha256':digest(path),'inputHashes':{name:digest(directory/name) for name in INPUT_FILES},
            'nodes':len(ids),'twoCoreNodes':len(core),'multiCardContexts':sum(len(row)>1 for row in groups.values()),
            'maximumContextCards':max(map(len,groups.values()),default=0),'positionsProposed':False}
    if mode=='pair-cut':
        report.update(contextIndexSchema='independent-context-index-v1',contexts=len(groups),
                      anchorLimit=32,allPairsExhaustive=False)
    (directory/'branch-map.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def self_test():
    ids=list('abcdefghi')
    groups,core=graph_branches(ids,[('a','b'),('b','c'),('c','a'),('a','d'),('d','e'),
                                  ('b','f'),('b','f'),('h','i')])
    assert core==set('abc') and groups['a']==set('ade') and groups['b']==set('bf')
    assert all(groups[node]=={node} for node in 'cdefghi')
    edges=[(0,1),(1,2),(2,0),(2,3),(3,4),(4,5),(5,3),(3,6),(7,8),(3,4),(5,5)]
    cuts=cut_branches(list(range(9)),edges)
    assert cuts=={0:{0},1:{1},2:{0,1,2},3:{3,4,5,6},4:{4},5:{5},6:{6},7:{7},8:{8}}
    assert cuts==cut_branches(list(range(9)),list(reversed(edges)))
    assert cut_branches(list(range(4)),[(0,1),(0,2),(0,3)])[0]=={0,2,3}
    assert cut_branches(list(range(5)),[(0,1),(1,2),(2,3),(3,4)])[2]=={2,3,4}
    print(json.dumps({'graphBranchFixture':'pass','cyclesTreesParallelEdgesAndDisconnectedForest':True,
                      'cutContextsCyclesDisconnectedGraphsAndTies':True}))


if __name__=='__main__':self_test()
