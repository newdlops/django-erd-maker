"""Learn card order while retaining original endpoint geometry at zero output.

Neural logits permute equal-size source slots. Fixed interior-anchor rays derive
the endpoint offsets, including shared-source-endpoint pooling. Both decoders
are deterministic; no geometry objective, search, or repair selects outputs.
Hard sorting is trained only from exact noncommitting native rewards.
"""
import numpy as np
from joint_slot_policy import SlotPermutationPolicy
from joint_anchor_ray_policy import InteriorAnchorEndpoints


class SlotAnchorRayPolicy(InteriorAnchorEndpoints,SlotPermutationPolicy):
    buffers=SlotPermutationPolicy.buffers+InteriorAnchorEndpoints.anchor_buffers+('slot_anchor_rays',)

    def __init__(self,features,positions,sizes,max_step,seed,provider,port_span):
        super().__init__(features,positions,sizes,max_step,seed)
        self.initialize_anchors(positions,sizes,provider)
        self.slot_anchor_rays=np.array(1,dtype=np.int32)

    def forward(self,features):
        nodes,cache=super().forward(features)
        offsets,_=self.endpoint_offsets(nodes)
        return np.concatenate([nodes,offsets]),cache
