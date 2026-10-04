"""Train a whole-scene slot model with a population-independent score scale."""
import argparse
import json
from pathlib import Path
import run_common_slot_reward as base
from compact_ordinal_slot_policy import OrdinalSlotAnchorPolicy
from geometry_world_model import digest


def run(args):
    original = base.CommonSlotAnchorPolicy
    base.CommonSlotAnchorPolicy = OrdinalSlotAnchorPolicy
    try:
        base.run(args)
    finally:
        base.CommonSlotAnchorPolicy = original
    output = args.out / 'report.json'
    report = json.loads(output.read_text())
    report['kind'] = 'common-original-card-anchor-ordinal-slot-reward-learning-v1'
    report['rankUnits'] = 'source-slot ordinal; fixed neural span 32 slots'
    report['rankScaleIndependentOfGroupPopulation'] = True
    for name in ('compact_ordinal_slot_policy.py', Path(__file__).name):
        report['codeSha256'][name] = digest(Path(__file__).parent / name)
    output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview', 'individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=114107)
    parser.add_argument('--sigma', type=float, default=.005)
    parser.add_argument('--rate', type=float, default=.003)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=30.)
    run(parser.parse_args())
