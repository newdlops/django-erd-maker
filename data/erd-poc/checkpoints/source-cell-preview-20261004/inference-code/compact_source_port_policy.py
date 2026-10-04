"""Learned card translations with original card-relative endpoints retained.

This immutable decoder rule applies before every measurement. Native costs do
not select endpoint locations, and rejected geometry is never repaired.
"""
import numpy as np
from compact_patch_neural_policy import PatchActor, decode_patch


class SourcePortPatchActor(PatchActor):
    def forward(self, features):
        fraction = np.tanh(self.embedding @ self.p['wo'] + self.p['bo'])
        delta = np.zeros_like(self.decoder.positions)
        delta[self.active] = decode_patch(fraction, self.buffers)
        offsets = np.zeros_like(self.decoder.ray_initial_phase)
        return np.concatenate([delta, offsets]), None
