"""Learn in the original-port source region without shrinking feasible outputs."""
import argparse
import json
from pathlib import Path
import run_compact_source_port_reward as base
from compact_feasible_source_port_policy import FeasibleSourcePortPatchActor
from geometry_world_model import digest


def run(args):
    assert args.view == 'individual'
    original = base.SourcePortPatchActor
    base.SourcePortPatchActor = FeasibleSourcePortPatchActor
    try:
        base.run(args)
    finally:
        base.SourcePortPatchActor = original
    path = args.out / 'report.json'
    report = json.loads(path.read_text())
    report['kind'] = 'compact-source-spacing-feasible-original-port-native-reward-learning-v2'
    report['immutableSourceAdjacentPlanesAppliedInEveryForward'] = True
    report['alreadyFeasibleQuantizedOutputsPreserved'] = True
    report['planesContainNoFutureVisualCosts'] = True
    report['coordinateRepairsAfterNativeRejection'] = 0
    for name in ('compact_ordered_source_port_policy.py', 'compact_feasible_source_port_policy.py', Path(__file__).name):
        report['codeSha256'][name] = digest(Path(__file__).parent / name)
    path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--view', choices=['individual'], required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seed', type=int, default=110103)
    p.add_argument('--patch-size', type=int, default=8)
    p.add_argument('--root-rank', type=int, default=16)
    p.add_argument('--max-step', type=float, default=256.)
    p.add_argument('--sigma', type=float, default=.025)
    p.add_argument('--rate', type=float, default=.005)
    p.add_argument('--iterations', type=int, default=64)
    p.add_argument('--seconds', type=float, default=30.)
    run(p.parse_args())
