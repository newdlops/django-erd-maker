"""Train compact cut-context policies using uncommitted synthetic rewards."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

assert os.environ.get('OMP_NUM_THREADS')=='1'
base=Path(__file__).parent;out=base/'cut-policy-training1';out.mkdir(exist_ok=False)
binary=base/'component-environment-v25';learner=Path('scripts/erd-poc/learn_card_policy.py')
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report={'branchMode':'cut','nativeBinarySha256':digest(binary),'learnerSha256':digest(learner),
        'actualCaptainExamplesUsed':0,'steps':[],'views':{}}
def run(name,command):
    started=time.monotonic()
    with (out/(name+'.stdout')).open('w') as stdout,(out/(name+'.stderr')).open('w') as stderr:
        subprocess.run(list(map(str,command)),check=True,stdout=stdout,stderr=stderr)
    report['steps'].append({'name':name,'command':list(map(str,command)),'seconds':time.monotonic()-started})
    print(json.dumps({'stage':name,'seconds':report['steps'][-1]['seconds']}),flush=True)
for view,seed in [('individual',98401),('overview',98481)]:
    dataset=out/(view+'.jsonl');checkpoint=out/(view+'.npz')
    run(view+'-data',[binary,'--dataset',dataset,'--decoder','moving-ray','--branches','1','--branch-mode','cut',
        '--graphs','64','--samples','64','--seconds','20','--seed',seed,
        *(['--overview-only','1'] if view=='overview' else [])])
    graphs=set();states=actions=positives=grouped_positive=grouped_states=0
    for line in dataset.open():
        row=json.loads(line);assert row['branchMoves'] and row['branchMode']=='cut'
        graphs.add(row['graph']);states+=1;actions+=len(row['actions']);positives+=sum(a[2]>0 for a in row['actions'])
        if row['contextCards']>1:
            grouped_states+=1;grouped_positive+=sum(a[2]>0 for a in row['actions'])
    assert len(graphs)>=8 and grouped_positive>0
    run(view+'-train',[sys.executable,learner,'train','--dataset',dataset,'--checkpoint',checkpoint,
        '--schema','relative-component-context-v2-64','--decoder','moving-ray','--branch-training','--branch-mode','cut',
        '--epochs','30','--seconds','8','--gain-cap','32','--min-log-sigma','-7','--seed',seed+7,
        *(['--overview-training'] if view=='overview' else [])])
    training=json.loads(checkpoint.with_suffix('.training.json').read_text())
    report['views'][view]={'states':states,'actions':actions,'positives':positives,'groupedPositive':grouped_positive,
        'groupedStates':grouped_states,'graphs':len(graphs),'datasetSha256':digest(dataset),
        'checkpointSha256':digest(checkpoint),'trainedUpdates':training['trainedUpdates'],'bestEpoch':training['bestEpoch'],
        'initialValidationNll':training['initialValidationNll'],'bestValidationNll':training['bestValidationNll']}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['views']),flush=True)
