"""Bounded serial training; reward collection never commits a placement."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

assert os.environ.get('OMP_NUM_THREADS')=='1'
base=Path('.tmp/visualcross-ml-150-750-20261003')
out=base/'branch-policy-training1';out.mkdir(exist_ok=False)
binary=base/'component-environment-v24'
learner=Path('scripts/erd-poc/learn_card_policy.py')
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report={'proposalAuthority':'trained shared neural weights','rewardCollectionCommitsPositions':False,
        'nativeBinarySha256':digest(binary),'learnerSha256':digest(learner),'steps':[],'views':{}}

def run(name,command):
    started=time.monotonic()
    with (out/(name+'.stdout')).open('w') as stdout,(out/(name+'.stderr')).open('w') as stderr:
        subprocess.run(list(map(str,command)),check=True,stdout=stdout,stderr=stderr)
    report['steps'].append({'name':name,'command':list(map(str,command)),
                            'seconds':time.monotonic()-started})
    print(json.dumps(report['steps'][-1]),flush=True)

for view,stage,seed in [('individual','individual-branch-policy1',98021),
                        ('overview','overview-branch-policy2',98101)]:
    objective=['--overview-only','1'] if view=='overview' else []
    synthetic=out/(view+'-synthetic.jsonl');actual=out/(view+'-actual.jsonl')
    run(view+'-synthetic',[binary,'--dataset',synthetic,'--decoder','moving-ray','--branches','1',
        '--graphs','64','--samples','64','--seconds','20','--seed',seed,*objective])
    run(view+'-actual',[binary,'--adapt-dataset',actual,'--directory',base/stage,
        '--decoder','moving-ray','--branches','1','--samples','64','--seconds','15','--seed',seed+3,*objective])
    source=json.loads((base/stage/'branch-map.json').read_text())
    origin=(base/stage/('experiment.json' if view=='individual' else 'workflow.json'))
    source_file=Path(json.loads(origin.read_text())['source'])
    assert digest(source_file)==source['sourceSha256']
    summaries={}
    for kind,path in [('synthetic',synthetic),('actual',actual)]:
        graphs=set();rows=actions=positive=grouped_positive=grouped_rows=0
        for line in path.open():
            row=json.loads(line);assert row['branchMoves'];graphs.add(row['graph']);rows+=1
            actions+=len(row['actions']);positive+=sum(a[2]>0 for a in row['actions'])
            if row['contextCards']>1:
                grouped_rows+=1;grouped_positive+=sum(a[2]>0 for a in row['actions'])
        summaries[kind]={'states':rows,'actions':actions,'positive':positive,'groupedPositive':grouped_positive,
                         'groupedStates':grouped_rows,'graphs':len(graphs),'sha256':digest(path)}
    assert summaries['synthetic']['graphs']>=8 and summaries['synthetic']['groupedPositive']>0
    combined=out/(view+'-combined.jsonl')
    with combined.open('xb') as dest:
        for path in [synthetic,actual]:
            with path.open('rb') as src:shutil.copyfileobj(src,dest)
    models={}
    for variant,dataset in [('synthetic',synthetic),('adapted',combined)]:
        if variant=='adapted' and not summaries['actual']['positive']:continue
        checkpoint=out/(view+'-'+variant+'.npz')
        run(view+'-'+variant+'-training',[sys.executable,learner,'train','--dataset',dataset,
            '--checkpoint',checkpoint,'--schema','relative-component-context-v2-64','--decoder','moving-ray',
            '--branch-training','--epochs','30','--seconds','8','--gain-cap','32','--min-log-sigma','-7',
            '--seed',seed+7,*(['--overview-training'] if view=='overview' else []),
            *(['--adapt-graph','100000','--adapt-source',source_file,'--adapt-weight','5'] if variant=='adapted' else [])])
        models[variant]={'path':str(checkpoint),'sha256':digest(checkpoint)}
    report['views'][view]={'sourceStage':stage,'sourceSha256':digest(source_file),'branchMap':source,
                           'datasets':summaries,'models':models}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['views']),flush=True)
