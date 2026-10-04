"""Neural scores use source-slot ordinal units, independent of group population."""
import numpy as np
from compact_common_slot_policy import CommonSlotAnchorPolicy


class OrdinalSlotAnchorPolicy(CommonSlotAnchorPolicy):
    def __init__(self, features, decoder, seed):
        super().__init__(features, decoder, seed)
        rank = np.zeros(len(decoder.positions))
        for start, end in zip(self.buffers['group_bounds'][:-1], self.buffers['group_bounds'][1:]):
            rank[self.buffers['slot_order'][start:end]] = np.arange(end - start)
        self.buffers['rank_origin'] = rank
        self.buffers['rank_span'] = np.array(32.)
