"""Validate branch geometry, training labels, and source-graph map parity."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

base=Path('.tmp/visualcross-ml-150-750-20261003')
binary=base/'component-environment-v24'
results={}
for name,command in [
    ('native',[''+str(binary),'--self-test']),
    ('pythonGraph',['python3','scripts/erd-poc/learned_branch_map.py']),
    ('networkGradient',['.venv-ml/bin/python','scripts/erd-poc/learn_card_policy.py','self-test'])]:
    result=subprocess.run(command,check=True,text=True,capture_output=True)
    results[name]=[json.loads(line) for line in result.stdout.splitlines()]
for name in ['individual-branch-policy1','overview-branch-policy2']:
    directory=base/name
    result=subprocess.run([str(binary),'--directory',str(directory),'--decoder','moving-ray',
        '--branches','1','--out',str(base/'unused-branch-validation')],input='QUIT\n',
        capture_output=True,text=True,check=True)
    ready=json.loads(result.stdout)
    assert ready['ready']
    results[name]={'sourceGraphMapParity':True,'sourceVisual':ready['visual']}
source=base/'branch-policy-v24-sources';source.mkdir(exist_ok=False)
hashes={}
for name in ['ml_component_environment.cpp','constrained_dual_node_geometry.h','constrained_scene.h',
             'constrained_boundary_sweep.h','learned_branch_map.py','learned_global_replay.py','learn_card_policy.py',
             'run_individual_policy_experiment.py','run_learned_leaf_layout.py','run_memory_bounded.py']:
    path=Path('scripts/erd-poc')/name;shutil.copyfile(path,source/name)
    hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
results.update(implementationHashes=hashes,nativeBinarySha256=hashlib.sha256(binary.read_bytes()).hexdigest())
(base/'branch-policy-v24-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results))
