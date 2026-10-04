"""Full-measured reward learning of NN patch positions and local common anchors."""
import argparse
import json
from pathlib import Path
import run_compact_source_port_reward as base
from compact_neighborhood_patch_policy import NeighborhoodPatchAnchorPolicy
from geometry_world_model import digest


def run(args):
    original, native_class = base.SourcePortPatchActor, base.Native
    class Actor(NeighborhoodPatchAnchorPolicy):
        def __init__(self, features, decoder, active, max_step, seed):
            super().__init__(features, decoder, active, max_step, seed, args.hops)
    class FullNative(native_class):
        def __init__(self, environment, directory, out, overview, sparse):
            super().__init__(environment, directory, out, overview, False)
    base.SourcePortPatchActor, base.Native = Actor, FullNative
    try:
        base.run(args)
    finally:
        base.SourcePortPatchActor, base.Native = original, native_class
    file = args.out / 'report.json'
    report = json.loads(file.read_text())
    report.update(kind='neural-neighborhood-patch-common-anchor-native-reward-v1',
        endpointOffsetsAlwaysZero=False, noMovedOwnerRetainsSourceGeometryExactly=True,
        graphNeighborhoodHops=args.hops, oneInteriorAnchorPerAffectedOriginalCard=True,
        unchangedPeerOriginalEndpointUsedAsRayTarget=True, fullNativeRewardScoring=True,
        geometryAfterNativeRejectionNeverRepaired=True)
    for name in ('compact_neighborhood_patch_policy.py', 'joint_neural_ports.py', Path(__file__).name):
        report['codeSha256'][name] = digest(Path(__file__).parent / name)
    file.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview', 'individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=116107)
    parser.add_argument('--patch-size', type=int, default=8)
    parser.add_argument('--root-rank', type=int, default=0)
    parser.add_argument('--max-step', type=float, default=2048.)
    parser.add_argument('--sigma', type=float, default=.025)
    parser.add_argument('--rate', type=float, default=.005)
    parser.add_argument('--hops', type=int, default=2)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=30.)
    run(parser.parse_args())
