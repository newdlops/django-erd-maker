"""Measure one fixed continuous decoder of a trained graph-stress network."""
from pathlib import Path
import json
import subprocess
import sys
import numpy as np
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from joint_grid_policy import GridOrderPolicy
from joint_grouped_routes import read_pairs
from run_joint_neural_layout import action_text

root=Path('.tmp/visualcross-ml-150-750-20261003')
directory=root/'individual-grid-stress1'
report=json.loads((directory/'joint-worker-result.json').read_text())
checkpoint=directory/f"joint-policy-{report['iterations']:04d}.npz"
model=GridOrderPolicy.load(checkpoint)
features=np.load(directory/'joint-input-features.npy')
code=model.forward(features)[1][4]
sizes=np.array(list(read_pairs(directory/'nodes.tsv').values()))
span=model.cell_size*model.grid_shape
center=model.frame_low+span/2
positions=center+(span-sizes)/2*np.tanh(code)
command='MEASURE '+action_text(positions-model.origin)[4:]
process=subprocess.Popen([str(root/'joint-batch-environment-v7'),'--directory',str(directory),
    '--out',str(root/'unused-stress-coordinate-measurement'),'--overview-only','0'],
    stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
try:
    initial=json.loads(process.stdout.readline())
    process.stdin.write(command+'\n');process.stdin.flush()
    measured=json.loads(process.stdout.readline())
    assert measured['measureOnly'] and not measured['accepted']
    process.stdin.write('QUIT\n');process.stdin.flush();assert process.wait(timeout=5)==0
finally:
    if process.poll() is None:process.kill();process.wait()
result={'trainedCheckpoint':str(checkpoint),'sourceVisual':initial['visual'],
        'decoder':'fixed frame-center plus per-card half-room times tanh(learned code)',
        'nativeMeasurement':measured,'geometrySaved':False,'promoted':False}
(root/'stress-coordinate-diagnosis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
