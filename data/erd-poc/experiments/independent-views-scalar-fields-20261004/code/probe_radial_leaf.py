"""Three fixed NN bias controls; no selection, training, TRY or adoption."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from radial_leaf_policy import RadialLeafPolicy
from geometry_world_model import digest
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder

parser = argparse.ArgumentParser()
parser.add_argument('--view', choices=('individual', 'overview'), required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--seed', type=int, default=119107)
args = parser.parse_args()
binding = Path('.tmp/visualcross-ml-150-750-20261004/geometry-world1/source-binding.json')
spec = json.loads(binding.read_text())['viewSources'][args.view]
directory = Path(spec['directory'])
inputs = {name:digest(directory / name) for name in INPUT_FILES}
assert inputs == spec['inputs']
args.out.mkdir(parents=True, exist_ok=False)
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
decoder = WalkDecoder(directory)
native = Native(environment, directory, args.out / 'unused.tsv', args.view == 'overview', False)
records = []
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    np.savez_compressed(args.out / 'observations.npz', nodes=nodes)
    model = RadialLeafPolicy(nodes, decoder, args.seed, 4096.)
    assert not np.any(model.forward(nodes)[0])
    for bias in (0., .1, .5):
        model.p['bo'][:] = bias
        action, info = model.forward(nodes)
        assert not np.any(action[len(decoder.positions):])
        command = wire(action, 'MEASURE')
        result = native.request(command)
        if bias == 0:
            assert result['legal'] and result['visual'] == spec['expectedVisual']
        checkpoint = args.out / f'bias-{bias}.npz'
        model.save(checkpoint, dict(trainedUpdates=0, diagnosticControl=True, bias=bias))
        row = dict(bias=bias, result=result, info=info, checkpoint=str(checkpoint),
            checkpointSha256=digest(checkpoint), wireSha256=hashlib.sha256(command.encode()).hexdigest())
        records.append(row)
        print(json.dumps(row), flush=True)
finally:
    native.close()
report = dict(status='pass', kind='radial-leaf-fixed-bias-controls-v1', view=args.view, seed=args.seed,
    maxStep=4096., sourceDirectory=str(directory), sourceInputs=inputs, sourceBindingSha256=digest(binding),
    environment=str(environment), environmentSha256=digest(environment), observationsSha256=digest(args.out / 'observations.npz'),
    codeSha256={name:digest(path) for name,path in [('probe',Path(__file__)),
        ('radial_leaf_policy.py',Path('scripts/erd-poc/radial_leaf_policy.py'))]},
    controls=records, fullNativeMeasurements=3, trainedUpdates=0, nativeTryCalls=0,
    sourceOnlySeparationBoundsFixedBeforeMeasurement=True, futureMetricInput=False, candidatePromoted=False)
(args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
