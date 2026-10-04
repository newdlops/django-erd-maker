"""Retain a cent-quantized NN output already inside the immutable source planes."""
import numpy as np
from compact_ordered_source_port_policy import OrderedSourcePortPatchActor, decode_adjacent
from compact_source_port_policy import SourcePortPatchActor


def decode_feasible_adjacent(delta, buffers):
    motion = np.sum((delta[buffers['star_second']] - delta[buffers['star_first']]) * buffers['star_axis'], axis=1)
    if np.all(buffers['star_gap'] + motion >= 1e-6):
        return delta.copy(), 1.
    return decode_adjacent(delta, buffers)


class FeasibleSourcePortPatchActor(OrderedSourcePortPatchActor):
    def forward(self, features):
        output, _ = SourcePortPatchActor.forward(self, features)
        n = len(self.decoder.positions)
        output[:n], scale = decode_feasible_adjacent(output[:n], self.buffers)
        return output, dict(sourceAdjacentScale=scale, sourceAdjacentConstraints=len(self.buffers['star_gap']))
