"""Check cut contexts against complete geometry and prior frozen actions."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0,'scripts/erd-poc')
from learned_branch_map import prepare_branch_map
from learned_global_replay import INPUT_FILES

base=Path(__file__).parent
out=base/'cut-branch-validation';out.mkdir(exist_ok=False)
binary=base/'component-environment-v25'
digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
results={}
def run(name,command,input=None):
    result=subprocess.run(list(map(str,command)),input=input,capture_output=True,text=True,check=True)
    (out/(name+'.stdout')).write_text(result.stdout)
    (out/(name+'.stderr')).write_text(result.stderr)
    results[name]=[json.loads(line) for line in result.stdout.splitlines()]
    return results[name]

run('native',[binary,'--self-test'])
run('python-map',['python3','scripts/erd-poc/learned_branch_map.py'])
run('legacy-branch-replay',['.venv-ml/bin/python','scripts/erd-poc/learn_card_policy.py','replay',
    '--checkpoint',base/'branch-policy-training1/individual-synthetic.npz',
    '--out',base/'individual-branch-trained2/learned.tsv'])
for view in ['individual','overview']:
    for mode in ['core','cut']:
        directory=out/(view+'-'+mode);directory.mkdir()
        if view=='individual':
            previous=base/'individual-branch-trained2'
            for name,old in [('nodes.tsv','individual.nodes.tsv'),('edges.tsv','individual.edges.tsv'),
                             ('positions.tsv','learned.tsv.individual'),('routes.tsv','learned.tsv.individual.routes.tsv')]:
                for prefix in ['','individual.']:shutil.copyfile(previous/old,directory/(prefix+name))
            for name,target in [('nodes.tsv','components.tsv'),('edges.tsv','groups.tsv')]:
                ids=[line.split('\t')[0] for line in (directory/name).read_text().splitlines()]
                (directory/target).write_text(''.join(f'{node}\t{node}\n' for node in ids))
            source=previous/'candidate.individual.layout.json';expected=1975
        else:
            previous=base/'overview-branch-trained1'
            for name in INPUT_FILES:shutil.copyfile(previous/name,directory/name)
            source=base/'overview-branch-policy2/candidate.layout.json';expected=294
            assert json.loads((previous/'workflow.json').read_text())['sourceSha256']==digest(source)
        report=prepare_branch_map(directory,digest(source),mode)
        command=[binary,'--directory',directory,'--decoder','moving-ray','--branches','1',
                 '--branch-mode',mode,'--out',out/'unused-proposal']
        ready=run(view+'-'+mode,command,'QUIT\n')[0]
        assert ready['ready'] and ready['visual']==expected
        results[view+'-'+mode]=dict(sourceGraphMapParity=True,sourceVisual=expected,branchMap=report)
        if mode=='cut':
            wrong=list(command);wrong[wrong.index('--branch-mode')+1]='core'
            rejected=subprocess.run(list(map(str,wrong)),input='QUIT\n',capture_output=True,text=True)
            assert rejected.returncode and 'branch map differs from source graph' in rejected.stderr
            results[view+'-'+mode]['incorrectModeRejected']=True

label_check='''import json,sys,tempfile
from pathlib import Path
sys.path.insert(0,'scripts/erd-poc')
from learn_card_policy import load_data,self_test
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'labels.jsonl'
 p.write_text(json.dumps(dict(graph=1,features=[0],actions=[[0,0,1]],branchMoves=True,branchMode='cut'))+'\\n')
 load_data(p,1,expected_branches=True,expected_branch_mode='cut')
 for kwargs in [dict(expected_branches=False),dict(expected_branch_mode='core')]:
  try:load_data(p,1,**kwargs)
  except AssertionError:pass
  else:raise AssertionError('mismatched branch labels accepted')
 p.write_text(json.dumps(dict(graph=1,features=[0],actions=[[0,0,1]],branchMoves=True))+'\\n')
 load_data(p,1,expected_branches=True,expected_branch_mode='core')
print(json.dumps(dict(trainingLabelContracts='pass',legacyCoreLabelsAccepted=True)))
self_test()
'''
run('training-labels',['.venv-ml/bin/python','-c',label_check])
sources=out/'sources';sources.mkdir()
names=['ml_component_environment.cpp','constrained_dual_node_geometry.h','constrained_scene.h',
       'constrained_boundary_sweep.h','learned_branch_map.py','learned_global_replay.py','learn_card_policy.py',
       'run_individual_policy_experiment.py','run_learned_leaf_layout.py','run_memory_bounded.py']
hashes={}
for name in names:
    path=Path('scripts/erd-poc')/name;shutil.copyfile(path,sources/name);hashes[name]=digest(path)
results.update(implementationHashes=hashes,nativeBinarySha256=digest(binary))
(out/'report.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps({'native':results['native'],'legacyReplay':results['legacy-branch-replay'],
    'actualSourceGraphMapsVerified':4,'incorrectModeRejections':2,'trainingLabels':results['training-labels']}))
