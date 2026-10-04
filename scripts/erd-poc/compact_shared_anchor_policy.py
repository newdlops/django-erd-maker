"""Shared NN weights predict one interior anchor for each original card.

Endpoint rays use the NN anchors at both ends. Source rounding residues are
handled inside the fixed boundary decoder, before any Native measurement.
"""
import numpy as np
from compact_source_port_policy import SourcePortPatchActor
from joint_neural_ports import perimeter_phase


class SharedAnchorPatchActor(SourcePortPatchActor):
    def __init__(self, features, decoder, active, max_step, seed):
        assert np.array_equal(decoder.provider.owner, np.arange(len(decoder.positions)))
        super().__init__(features, decoder, active, max_step, seed)
        features = np.asarray(features, dtype=np.float32)
        hidden = np.tanh(np.tanh(np.clip((features - self.mean) / self.scale, -8, 8)) @ self.w1) @ self.w2
        self.anchor_embedding = np.tanh(hidden)
        assert np.max(abs(self.anchor_embedding[self.active] - self.embedding)) < 1e-12
        self.anchor_embedding[self.active] = self.embedding

    def forward(self, features):
        output, _ = super().forward(features)
        n = len(self.decoder.positions)
        provider = self.decoder.provider
        fraction = np.tanh(self.anchor_embedding @ self.p['wo'] + self.p['bo'])
        offsets = .2 * self.decoder.sizes * fraction
        anchors = self.decoder.positions + output[:n] + offsets
        ends = anchors[provider.full_edges]
        direction = ends[:, ::-1] - ends
        residue = provider.port_offsets - provider.base_boundary
        local = offsets[provider.full_edges] - residue
        half = provider.endpoint_sizes / 2
        nonzero = abs(direction) > 1e-12
        den = np.where(nonzero, direction, 1.)
        sides = np.where(direction >= 0, half, -half)
        parameter = np.min(np.where(nonzero, (sides - local) / den, np.inf), axis=2)
        assert np.isfinite(parameter).all() and (parameter > 0).all()
        boundary = local + parameter[:, :, None] * direction
        phase = perimeter_phase(boundary, provider.endpoint_sizes)
        output[n:] = np.mod(phase - provider.phase + .5, 1.) - .5
        return output, dict(anchorInteriorFraction=.2, commonAnchorPerOriginalCard=True)
