"""Quantize neural input features consistently; retain float64 geometry and weights."""
import numpy as np
from fractional_cycle_model import FractionCritic
from owner_amplitude_cycle_model import load as load_base


class Float32InputCritic(FractionCritic):
    def forward(self, features):
        return super().forward(np.asarray(features, dtype=np.float32))


def with_float32_inputs(model):
    wrapped = object.__new__(Float32InputCritic)
    wrapped.p = model.p; wrapped.mean = model.mean; wrapped.scale = model.scale
    return wrapped


def load(path):
    model, metadata = load_base(path)
    if metadata.get('inferenceFeatureInputDtype') == 'float32': model = with_float32_inputs(model)
    return model, metadata
