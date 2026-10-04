"""Shared trained head predicts every card and its common interior anchor."""
import numpy as np
from compact_shared_anchor_policy import SharedAnchorPatchActor
from joint_separation_policy import separation_graph


class GlobalAnchorPolicy(SharedAnchorPatchActor):
    def __init__(self, features, decoder, active, max_step, seed):
        super().__init__(features, decoder, active, max_step, seed)
        self.training_active = self.active.copy()
        self.active = np.arange(len(decoder.positions), dtype=np.int32)
        self.embedding = self.anchor_embedding
        self.buffers = separation_graph(decoder.positions, decoder.sizes, 2048.)
