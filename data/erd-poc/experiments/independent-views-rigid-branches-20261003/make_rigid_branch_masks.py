"""Select graph branches around conflicted core cards; no coordinates used."""
import collections
import json
from pathlib import Path

root=Path(__file__).parent
output=root/'rigid-branch-masks';output.mkdir(exist_ok=False)
reports=[]
for source,stage in zip(json.loads((root/'localized-conflict-analysis/analysis.json').read_text()),
                        ['individual-outward-joint1','overview-ray-conditioned1']):
    directory=root/stage
    ids=[row.split('\t')[0] for row in (directory/'nodes.tsv').read_text().splitlines()]
    graph={key:set() for key in ids}
    for _,a,b in (row.split('\t') for row in (directory/'edges.tsv').read_text().splitlines()):
        if a!=b:graph[a].add(b);graph[b].add(a)
    core=set(ids);degree={key:len(adj) for key,adj in graph.items()}
    queue=collections.deque(key for key in ids if degree[key]<2)
    while queue:
        node=queue.popleft()
        if node not in core:continue
        core.remove(node)
        for other in graph[node]&core:
            degree[other]-=1
            if degree[other]<2:queue.append(other)
    branches=[]
    for node,pressure in source['topNodes']:
        if node not in core:continue
        members={node};pending=[node]
        while pending:
            current=pending.pop()
            for other in graph[current]-members-core:
                members.add(other);pending.append(other)
        if len(members)<2:continue
        patch={'view':source['view'],'sourceSha256':source['sourceSha256'],'sourceVisual':source['sourceVisual'],
               'nodeIds':[node]+sorted(members-{node}),'coreRoot':node,'rootConflictIncidences':pressure,
               'selection':'conflicted two-core root with all attached nodes outside the simple-graph two-core',
               'positionsProposed':False,'coordinatesAsTargets':False}
        filename=f"{source['view']}-branch{len(branches)}.json"
        (output/filename).write_text(json.dumps(patch,indent=2)+'\n')
        branches.append({'file':filename,'root':node,'nodes':len(members),'rootConflicts':pressure})
    reports.append({'view':source['view'],'simpleGraphTwoCoreNodes':len(core),'branches':branches})
(output/'analysis.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps(reports))
