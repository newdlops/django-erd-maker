import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from radial_leaf_policy import RadialLeafPolicy
from run_anchor_pair_walk import WalkDecoder
from joint_reward_training import verify_trace
from run_anchor_pair_policy import wire
from geometry_world_model import digest

root = Path('.tmp/visualcross-ml-150-750-20261004/radial-leaf1')
rows = []
for view in ('individual', 'overview'):
    stage = root / f'{view}-learning1'
    report = json.loads((stage / 'report.json').read_text())
    decoder = WalkDecoder(Path(report['sourceDirectory']))
    with np.load(stage / 'observations.npz', allow_pickle=False) as saved:
        nodes = saved['nodes'].copy()
    model = RadialLeafPolicy(nodes, decoder, report['seed'], report['maxStep'])
    assert digest(stage / 'training.jsonl') == report['trainingSha256']
    records = [json.loads(line) for line in (stage / 'training.jsonl').read_text().splitlines()]
    controls = [sample['result'] for row in records for probe in row['directions'] for sample in probe['samples']]
    replayed = verify_trace(model, nodes, records, wire)
    assert replayed == report['rewardProbes']
    index = 4 if view == 'overview' else 6
    pressure = np.any(nodes[:,index:index+2] > 0, axis=1)
    eligible = model.buffers['eligible']
    tries = [json.loads(line) for line in (stage / 'actions.jsonl').read_text().splitlines()]
    summaries = []
    for row in tries:
        with np.load(row['checkpoint'], allow_pickle=False) as saved:
            model.p = {key:saved[key].copy() for key in ('wo','bo')}
        action, info = model.forward(nodes)
        moved = np.any(action[:len(nodes)] != 0, axis=1)
        summaries.append(dict(step=row['step'], movedPressureOwners=int(np.count_nonzero(moved & pressure)),
            movedNoPressureOwners=int(np.count_nonzero(moved & ~pressure)), visual=row['result']['visual'], info=info))
    rows.append(dict(view=view, eligibleOwners=int(eligible.sum()),
        eligibleWithSourcePressure=int(np.count_nonzero(eligible & pressure)),
        eligibleWithoutSourcePressure=int(np.count_nonzero(eligible & ~pressure)),
        replayedUpdates=len(records), replayedProbeWires=replayed,
        minimumProbeVisual=min(row['visual'] for row in controls),
        sourceVisual=report['initialVisual'], legalProbes=sum(row['legal'] for row in controls),
        betterLegalProbes=sum(row['legal'] and row['visual'] < report['initialVisual'] for row in controls),
        trainedOutputs=summaries))
result = dict(status='pass', runs=rows, nativeMeasurements=0, diagnosticOnly=True,
    noCandidateSelected=True, sourcePressureFeatures='overview indices 4/5; individual 6/7')
target = root / 'source-pressure-inspection.json'
assert not target.exists()
target.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
