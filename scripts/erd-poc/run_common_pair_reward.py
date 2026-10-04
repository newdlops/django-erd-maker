"""Learn NN-selected local swaps with complete common-anchor endpoint stars."""
import argparse
import json
from pathlib import Path
import run_common_slot_reward as base
from compact_common_pair_policy import CommonPairAnchorPolicy
from geometry_world_model import digest


def run(args):
    class Actor(CommonPairAnchorPolicy):
        def __init__(self, features, decoder, seed):
            super().__init__(features, decoder, seed, overview=args.view == 'overview', hops=args.hops)
    original, keys = base.CommonSlotAnchorPolicy, base.KEYS
    base.CommonSlotAnchorPolicy, base.KEYS = Actor, ('po', 'pb', 'wo', 'bo')
    try:
        base.run(args)
    finally:
        base.CommonSlotAnchorPolicy, base.KEYS = original, keys
    output = args.out / 'report.json'
    report = json.loads(output.read_text())
    report.update(kind='common-original-card-anchor-neural-neighborhood-pair-reward-v1',
        noOpRetainsSourcePositionsAndEndpoints=True, graphNeighborhoodHops=args.hops,
        oneInteriorAnchorPerOriginalCard=False, oneInteriorAnchorPerAffectedOriginalCard=True,
        nodeFeatureInputDtype='float32', pairFeatureInputDtype='float64',
        featureInputDtype='fixed node and pair contexts',
        candidateVocabulary='source-only nearest 8 same-size peers within 2048; max 512 rows',
        unchangedPeerOriginalEndpointUsedAsRayTarget=True, geometryAfterNativeRejectionNeverRepaired=True)
    for name in ('compact_common_pair_policy.py', Path(__file__).name):
        report['codeSha256'][name] = digest(Path(__file__).parent / name)
    output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-binding', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--view', choices=('overview', 'individual'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=115107)
    parser.add_argument('--sigma', type=float, default=.025)
    parser.add_argument('--rate', type=float, default=.005)
    parser.add_argument('--hops', type=int, default=2)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--seconds', type=float, default=30.)
    run(parser.parse_args())
