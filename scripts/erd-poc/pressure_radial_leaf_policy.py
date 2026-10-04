"""Use immutable observed source conflicts to gate the radial NN field."""
import numpy as np
from radial_leaf_policy import RadialLeafPolicy


class PressureRadialLeafPolicy(RadialLeafPolicy):
    def __init__(self, features, decoder, seed, max_step, view):
        assert view in ('overview', 'individual')
        super().__init__(features, decoder, seed, max_step)
        index = 4 if view == 'overview' else 6
        # These are the existing Native source cross/hit observation features.
        # No future pose is measured or used to construct this mask.
        pressure = np.any(np.asarray(features)[:,index:index+2] > 0, axis=1)
        self.buffers['source_pressure'] = pressure & self.buffers['eligible']
        self.buffers['pressure_feature_indices'] = np.array([index,index+1], dtype=np.int32)

    def forward(self, features):
        action, info = super().forward(features)
        count = len(self.decoder.positions)
        action[:count] *= self.buffers['source_pressure'][:,None]
        return action, info | dict(movedOwners=int(np.any(action[:count] != 0, axis=1).sum()),
            maximumDisplacementPixels=float(abs(action[:count]).max()),
            activePressureOwners=int(self.buffers['source_pressure'].sum()),
            sourcePressureMaskFixed=True)
