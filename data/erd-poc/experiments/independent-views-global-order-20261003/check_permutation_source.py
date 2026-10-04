"""Read-only source measurement before training a permutation policy."""
from pathlib import Path
import collections
import json
import subprocess

root=Path('.tmp/visualcross-ml-150-750-20261003')
directory=root/'individual-outward-joint1'
nodes=[line.split('\t') for line in (directory/'nodes.tsv').read_text().splitlines()]
groups=collections.defaultdict(list)
for row in nodes:groups[tuple(map(float,row[1:]))].append(row[0])
process=subprocess.Popen([str(root/'joint-batch-environment-v6'),'--directory',str(directory),
    '--out',str(root/'unused-permutation-diagnostic'),'--overview-only','0'],
    stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    observation=json.loads(process.stdout.readline())
    command='TRY '+' '.join(['0']*(2*len(nodes)))
    process.stdin.write(command+'\n');process.stdin.flush()
    measured=json.loads(process.stdout.readline())
    process.stdin.write('QUIT\n');process.stdin.flush()
    assert process.wait(timeout=5)==0
finally:
    if process.poll() is None:process.kill();process.wait()
report={'sourceVisual':observation['visual'],'canonicalSourceMeasurement':measured,
        'shapeGroups':len(groups),'movableNodes':sum(len(g) for g in groups.values() if len(g)>1),
        'groupSizes':sorted([len(g) for g in groups.values()],reverse=True),
        'geometrySaved':False,'modelCandidatePromoted':False}
(root/'permutation-source-diagnosis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
