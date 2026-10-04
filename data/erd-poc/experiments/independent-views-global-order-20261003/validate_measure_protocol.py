"""Prove that a better-than-ceiling measurement cannot mutate native state."""
from pathlib import Path
import json
import subprocess
import sys
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from learned_global_replay import pairs,routes

root=Path('.tmp/visualcross-ml-150-750-20261003')
source=root/'individual-outward-joint1'
directory=root/'native-measure-validation';directory.mkdir(exist_ok=False)
output=directory/'saved.tsv'
process=subprocess.Popen([str(root/'joint-batch-environment-v7'),'--directory',str(source),
    '--out',str(output),'--overview-only','0','--regression-limit','200'],
    stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
def request(command):
    process.stdin.write(command+'\n');process.stdin.flush()
    return json.loads(process.stdout.readline())
try:
    initial=json.loads(process.stdout.readline())
    zero=' '.join(['0']*(len(initial['nodes'])*2))
    measure=request('MEASURE '+zero)
    assert measure['legal'] and measure['measureOnly'] and not measure['accepted']
    assert measure['visual']<initial['visual']+200
    assert request('SAVE')['saved']
    stats=json.loads(Path(str(output)+'.stats.json').read_text())
    assert stats['visual']==initial['visual'] and stats['policyActionsEvaluated']==0
    assert pairs(output)==pairs(source/'positions.tsv')
    assert routes(Path(str(output)+'.individual.routes.tsv'))==routes(source/'individual.routes.tsv')
    positive=request('TRY '+zero)
    assert positive['accepted'] and not positive['measureOnly']
    process.stdin.write('QUIT\n');process.stdin.flush();assert process.wait(timeout=5)==0
finally:
    if process.poll() is None:process.kill();process.wait()
report={'sourceVisual':initial['visual'],'measuredVisual':measure['visual'],
        'measurementPreservedSourceState':True,'zeroInferenceActionsAfterMeasure':True,
        'sameOutputAcceptedByTryPositiveControl':True,'positiveControlSaved':False,
        'promoted':False,'allChecksPassed':True}
(directory/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
