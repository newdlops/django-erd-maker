"""Source retention, shared-endpoint ties, side bounds and full Native legality."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from shared_card_phase_policy import SharedCardPhasePolicy
from geometry_world_model import digest
from joint_neural_ports import perimeter_points
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder

parser = argparse.ArgumentParser()
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
source = args.out / 'source'
source.mkdir()
values = dict(nodes='n0\t120\t100\nn1\t100\t100\nn2\t100\t100\nn3\t100\t100\n',
    positions='n0\t0\t0\nn1\t500\t-200\nn2\t1100\t200\nn3\t1700\t0\n',
    edges='e0\tn0\tn1\ne1\tn0\tn2\n', routes='e0\t60,-10 450,-200\ne1\t60,-10 1050,200\n')
for kind, content in values.items():
    for prefix in ('', 'individual.'):
        (source / (prefix + kind + '.tsv')).write_text(content)
(source / 'components.tsv').write_text('n0\tn0\nn1\tn1\nn2\tn2\nn3\tn3\n')
(source / 'groups.tsv').write_text('e0\te0\ne1\te1\n')
environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
decoder = WalkDecoder(source)
native = Native(environment, source, args.out / 'unused.tsv', False, False)
try:
    nodes = np.array([row['features'] for row in native.initial['nodes']])
    model = SharedCardPhasePolicy(nodes, decoder, 118107, .005)
    initial, _ = model.forward(nodes)
    assert not np.any(initial)
    baseline = native.request(wire(initial, 'MEASURE'))
    assert baseline['legal'] and baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
    model.p['bo'][:] = .7
    action, info = model.forward(nodes)
    assert not np.any(action[:4])
    phases = action[4:]
    assert phases[0, 0] == phases[1, 0]
    points = decoder.provider.original_ports + (
        perimeter_points(decoder.provider.phase + phases, decoder.provider.endpoint_sizes)[0] - decoder.provider.base_boundary)
    np.testing.assert_array_equal(points[0, 0], points[1, 0])
    moved = float(abs(points - decoder.provider.original_ports).max())
    assert moved > 1., moved
    result = native.request(wire(action, 'MEASURE'))
    assert result['legal'] and result['hard'] == result['individualHard'] == result['spacing'] == 0, result
    # Existing corners are immovable in either direction under the source-side rule.
    from shared_card_phase_policy import source_phase_limits
    corner_phases = np.array([[0., .25]])
    sides = np.array([[[100., 100.], [100., 100.]]])
    limits = source_phase_limits(corner_phases, sides, np.array([[0, 1]]), 2, .05)
    assert not np.any(limits[0]) and not np.any(limits[1])
    audit = dict(status='pass', zeroHeadRetainsEverySourceCoordinateAndPort=True,
        trainedCardCoordinates=False, coincidentEndpointsRemainExactlyTied=True,
        sourceCornerGuardPassed=True, maximumEndpointDisplacementPixels=moved, fullNativeMeasurements=2,
        result=result, codeSha256=digest(__file__), modelCodeSha256=digest('scripts/erd-poc/shared_card_phase_policy.py'),
        wireSha256=hashlib.sha256(wire(action, 'MEASURE').encode()).hexdigest(), info=info)
    (args.out / 'audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps(audit), flush=True)
finally:
    native.close()
