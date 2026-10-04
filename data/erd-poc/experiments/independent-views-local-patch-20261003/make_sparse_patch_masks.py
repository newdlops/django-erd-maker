"""Choose a smaller immutable training context, without proposing positions."""
import collections
import json
from pathlib import Path

root=Path(__file__).parent
analysis=root/'localized-conflict-analysis'
records=[]
for source,stage in zip(json.loads((analysis/'analysis.json').read_text()),
                        ['individual-outward-joint1','overview-ray-conditioned1']):
    edges={row[0]:row[1:] for row in
           (line.split('\t') for line in (root/stage/'edges.tsv').read_text().splitlines())}
    degree=collections.Counter(node for endpoints in edges.values() for node in endpoints)
    choices=[]
    for edge,conflicts in source['topEdges']:
        node=min(edges[edge],key=lambda node:(degree[node],node))
        choices.append((conflicts/degree[node],edge,node,conflicts,degree[node]))
    _,edge,node,conflicts,count=max(choices)
    patch={'view':source['view'],'sourceSha256':source['sourceSha256'],
           'sourceVisual':source['sourceVisual'],'nodeIds':[node],'seedEdge':edge,
           'selection':'among the six most conflicted source edges, activate the lower-degree endpoint with greatest conflicts per incident edge',
           'seedEdgeSourceConflicts':conflicts,'activeNodeDegree':count,
           'positionsProposed':False,'coordinatesAsTargets':False}
    path=analysis/(source['view']+'-sparse-n1.json')
    with path.open('x') as output:output.write(json.dumps(patch,indent=2)+'\n')
    records.append({'file':str(path),**patch})
print(json.dumps(records))
