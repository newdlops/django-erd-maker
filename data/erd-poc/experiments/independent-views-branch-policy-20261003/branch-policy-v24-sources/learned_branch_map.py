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


def prepare_branch_map(directory, source_sha):
    ids=[row.split('\t')[0] for row in (directory/'nodes.tsv').read_text().splitlines()]
    edges=[row.split('\t')[1:] for row in (directory/'edges.tsv').read_text().splitlines()]
    groups,core=graph_branches(ids,edges)
    order={node:i for i,node in enumerate(ids)}
    path=directory/'branches.tsv'
    with path.open('x') as output:
        for node in ids:output.write('\t'.join([node,*sorted(groups[node],key=order.get)])+'\n')
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report={'schema':'source-bound-graph-branches-v1','sourceSha256':source_sha,
            'branchMapSha256':digest(path),'inputHashes':{name:digest(directory/name) for name in INPUT_FILES},
            'nodes':len(ids),'twoCoreNodes':len(core),'multiCardContexts':sum(len(row)>1 for row in groups.values()),
            'maximumContextCards':max(map(len,groups.values())),'positionsProposed':False}
    (directory/'branch-map.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def self_test():
    ids=list('abcdefghi')
    groups,core=graph_branches(ids,[('a','b'),('b','c'),('c','a'),('a','d'),('d','e'),
                                  ('b','f'),('b','f'),('h','i')])
    assert core==set('abc') and groups['a']==set('ade') and groups['b']==set('bf')
    assert all(groups[node]=={node} for node in 'cdefghi')
    print(json.dumps({'graphBranchFixture':'pass','cyclesTreesParallelEdgesAndDisconnectedForest':True}))


if __name__=='__main__':self_test()
