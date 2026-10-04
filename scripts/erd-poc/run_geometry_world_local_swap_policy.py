#!/usr/bin/env python3
"""Local same-size vocabulary; learned event probabilities choose all swaps."""
import argparse
import json
from pathlib import Path
import numpy as np
import run_geometry_world_swap_policy as base
from geometry_world_model import digest
from run_anchor_pair_walk import WalkDecoder


class LocalSwapDecoder(WalkDecoder):
    def eligible_pairs(self, seed, count):
        words = set()
        for group in self.groups:
            for n in group:
                other = group[group != n]
                distance = np.rint(np.sum((self.positions[other]-self.positions[n])**2, axis=1)*1e6)
                for m in other[np.lexsort((other, distance))[:8]]:
                    if not (self.irrelevant_isolate[n] and self.irrelevant_isolate[m]):
                        words.add(tuple(sorted((int(n), int(m)))))
        words = np.array(sorted(words), dtype=np.int32)
        assert len(words)
        chosen = np.random.default_rng(seed).choice(len(words), min(count, len(words)), replace=False)
        self.support_record = {'kind': 'eight nearest same-size source owners per owner',
            'eligibleUnorderedPairs': len(words), 'sampledUnorderedPairs': len(chosen),
            'bothIsolatesExcluded': True, 'futureGeometryCostsUsedInVocabulary': False,
            'coordinateSelectionByVocabulary': False}
        return words[chosen]


def run(args):
    # The delegated implementation is hash-bound separately. Only the source
    # vocabulary changes; no Native outcome changes the fixed ranking order.
    original = base.WalkDecoder
    base.WalkDecoder = LocalSwapDecoder
    try:
        base.run(args)
    finally:
        base.WalkDecoder = original
    report_path = args.out/'report.json'; report = json.loads(report_path.read_text())
    decoder = LocalSwapDecoder(args.directory); decoder.eligible_pairs(args.seed, args.vocabulary)
    report['kind'] = 'learned-geometric-event-world-local-equal-size-swap-v1'
    report['support'] = decoder.support_record
    report['entrypoint'] = str(Path(__file__))
    report['codeSha256'][Path(__file__).name] = digest(__file__)
    report_path.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--directory', type=Path, required=True); p.add_argument('--world', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True); p.add_argument('--view', choices=['overview','individual'], required=True)
    p.add_argument('--out', type=Path, required=True); p.add_argument('--untrained', action='store_true')
    p.add_argument('--seed', type=int, default=104309); p.add_argument('--vocabulary', type=int, default=1024)
    p.add_argument('--budget', type=int, default=128); p.add_argument('--seconds', type=float, default=30.)
    run(p.parse_args())
