"""Learn the source-conflict-gated radial NN with the fixed reward runner."""
import argparse
import json
from pathlib import Path
import run_scalar_source_reward as scalar
from pressure_radial_leaf_policy import PressureRadialLeafPolicy


def run(args):
    args.policy = 'radial'
    args.span = .005
    scalar.CODE = scalar.CODE + ('pressure_radial_leaf_policy.py', 'run_pressure_radial_reward.py')
    scalar.RadialLeafPolicy = lambda features,decoder,seed,cap: PressureRadialLeafPolicy(
        features, decoder, seed, cap, args.view)
    scalar.run(args)
    path = args.out / 'report.json'
    report = json.loads(path.read_text())
    report.update(kind='source-conflict-gated-radial-nn-native-reward-v1',
        decoderVariant='immutable-source-cross-and-hit-gate',
        sourcePressureFeatureIndices=[4,5] if args.view == 'overview' else [6,7],
        sourcePressureMaskFixedBeforeFirstReward=True)
    path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview','individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=119507)
    parser.add_argument('--max-step', type=float, default=4096.)
    parser.add_argument('--sigma', type=float, default=.05)
    parser.add_argument('--rate', type=float, default=.03)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=30.)
    run(parser.parse_args())
