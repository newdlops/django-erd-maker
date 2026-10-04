"""Shared field actor retaining initially coincident endpoints by construction.

The fixed source grouping is applied to every NN forward pass, before any
Native measurement. It contains no cost feedback or per-candidate repair.
"""
from compact_patch_neural_policy import PatchActor


class TiedPatchActor(PatchActor):
    def forward(self,features):
        output,cache=super().forward(features)
        n=len(self.decoder.positions)
        output[n:]=self.decoder.pool(output[n:])
        return output,cache
