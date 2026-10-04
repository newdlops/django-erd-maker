#!/usr/bin/env python3
"""Matched transfer with collinear source anchors on non-tangent source rays.

The source defines anchor buffers once. Non-tangent rays retain their exact
source line; tangent source rays retain the original strict interior buffers.
Submitted neural actions receive no geometry search, repair or fallback.
"""
import argparse
from pathlib import Path
import numpy as np
import run_cycle_endpoint_transfer as transfer
from run_anchor_pair_policy import digest


class CollinearSourceDecoder(transfer.IndependentDecoder):
    def __init__(self, directory):
        super().__init__(directory)
        provider = self.provider; full = self.positions[provider.owner]+provider.offsets
        direction = full[provider.full_edges[:, 1]]+provider.base_boundary[:, 1]-full[provider.full_edges[:, 0]]-provider.base_boundary[:, 0]
        unit = direction/np.linalg.norm(direction, axis=1)[:, None]
        inward = unit[:, None, :]*np.array([-1., 1.])[None, :, None]
        half = self.ray_sizes/2; boundary = provider.base_boundary
        distances = np.divide(np.where(inward >= 0, half-boundary, -half-boundary), inward,
            out=np.full_like(inward, np.inf), where=abs(inward) > 1e-12)
        reach = np.maximum(0., distances.min(2)); collinear = boundary+.5*reach[:, :, None]*inward
        eligible = (reach > 1e-6).all(1) & (abs(collinear) < half-1e-9).all(axis=(1, 2))
        self.collinear_source_edges = eligible.copy()
        self.anchor_offsets = np.where(eligible[:, None, None], collinear, self.anchor_offsets)
        assert np.isfinite(self.anchor_offsets).all() and (abs(self.anchor_offsets) < half).all()
        self.ray_base_direction = full[provider.full_edges[:, 1]]+self.anchor_offsets[:, 1]-full[provider.full_edges[:, 0]]-self.anchor_offsets[:, 0]
        self.ray_initial_phase = self.ray_phase(self.ray_base_direction)[0]


def execute(args):
    original_decoder = transfer.IndependentDecoder; original_hashes = transfer.code_hashes
    # Inject this fixed decoding strategy only for this call, then restore the
    # original module so other decoder controls replay unchanged in one process.
    transfer.IndependentDecoder = CollinearSourceDecoder
    transfer.code_hashes = lambda: original_hashes() | {'run_collinear_cycle_transfer.py': digest(__file__)}
    args.mode = 'collinear-source'
    try:
        return (transfer.run if args.command == 'run' else transfer.replay)(args)
    finally:
        transfer.IndependentDecoder = original_decoder; transfer.code_hashes = original_hashes


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('command', choices=['run', 'replay'])
    p.add_argument('--source', type=Path); p.add_argument('--out', type=Path, required=True)
    p.add_argument('--budget', type=int, default=128); p.add_argument('--seconds', type=float, default=20)
    execute(p.parse_args())
