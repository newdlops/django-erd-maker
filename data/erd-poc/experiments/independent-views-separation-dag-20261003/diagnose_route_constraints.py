"""Classify frozen learned proposals; never create a new geometry candidate."""
import collections,hashlib,json,shutil,subprocess
from pathlib import Path
base=Path(__file__).parent;out=base/'route-constraint-diagnosis1';out.mkdir(exist_ok=False)
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
binary=base/'route-constraint-diagnostic-v1'
report={'proposalSource':'previously frozen bounded neural actions only','candidateCreated':False,
        'nativeBinarySha256':digest(binary),'views':{},
        'hardCategories':['outwardAtMovedCard','outwardAtFixedCard','ownCardReentry','adjacentCrossingDoubled','boundaryContactDoubled']}
for view in ['individual','overview']:
    source=base/(view+'-bounded-trained2');policy=json.loads((source/'learned.tsv.policy.json').read_text())
    trace=source/'learned.tsv.actions.jsonl';assert digest(trace)==policy['actionsSha256']
    assert digest(Path(policy['checkpoint']))==policy['checkpointSha256']
    for name,sha in {**policy['movingRayInputHashes'],**policy['branchInputHashes']}.items():assert digest(source/name)==sha
    actions=[json.loads(line) for line in trace.open()];best={};hard=positive=0
    for i,row in enumerate(actions):
        result=row['result']
        if result['reason'] not in ('overview-hard','individual-hard'):continue
        hard+=1
        if result['gain']<=0:continue
        positive+=1;node=row['node']
        if node not in best or result['gain']>actions[best[node]]['result']['gain']:best[node]=i
    # Ranking selects diagnostic records, never changes a coordinate proposal.
    selected=set(sorted(best.values(),key=lambda i:(-actions[i]['result']['gain'],i))[:64])
    input_file=out/(view+'.actions.tsv')
    with input_file.open('x') as stream:
        for i,row in enumerate(actions):
            r=row['result'];x,y=row['action']
            stream.write(f"{row['node']}\t{x:.12g}\t{y:.12g}\t{int(r['accepted'])}\t{r['gain']}\t{r['individualGain']}\t{r['reason']}\t{int(i in selected)}\n")
    stdout=out/(view+'.stdout');stderr=out/(view+'.stderr')
    with input_file.open() as stdin,stdout.open('x') as output,stderr.open('x') as errors:
        subprocess.run(list(map(str,[binary,'--directory',source,'--expected-visual',policy['final']['visual'],
            '--overview-only',int(view=='overview')])),stdin=stdin,stdout=output,stderr=errors,check=True)
    rows=[json.loads(line) for line in stdout.read_text().splitlines()]
    summary=rows.pop();assert summary['completeReplay'] and summary['actions']==len(actions) and summary['inspected']==len(selected)
    ids=[line.split('\t')[0] for line in (source/'nodes.tsv').read_text().splitlines()]
    for row in rows:row['root']=ids[row['node']]
    counts={scope:[sum(row[scope][k]>0 for row in rows) for k in range(5)] for scope in ['overviewHard','individualHard']}
    entry={'sourceDirectory':str(source),'traceSha256':digest(trace),'checkpointSha256':policy['checkpointSha256'],
           'actionsReplayed':len(actions),'hardRejectedActions':hard,'positiveGainHardRejectedActions':positive,
           'distinctPositiveRoots':len(best),'sampledRoots':len(rows),'candidatesWithCategory':counts,'samples':rows}
    report['views'][view]=entry
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({view:{k:v for k,v in entry.items() if k!='samples'}}),flush=True)
sources=out/'sources';sources.mkdir()
report['implementationHashes']={}
for name in ['diagnose_learned_route_constraints.cpp','ml_component_environment.cpp','constrained_scene.h',
             'constrained_dual_node_geometry.h','constrained_boundary_sweep.h']:
    path=Path('scripts/erd-poc')/name;shutil.copyfile(path,sources/name);report['implementationHashes'][name]=digest(path)
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
