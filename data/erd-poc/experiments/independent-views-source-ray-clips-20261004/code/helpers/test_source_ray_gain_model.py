"""Check real neural gradients and regression fitting before reward training."""
import importlib.util
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
assert importlib.util.find_spec('source_ray_gain_model') is not None,'neural source-ray gain regressor is missing'
from source_ray_gain_model import GainRanker,train_gain_ranker,parameter_hash
from learn_pair_policy import KEYS

rng = np.random.default_rng(127107)
features = rng.normal(size=(11,70))
targets = rng.normal(0,.15,size=11)
model = GainRanker(127307)
_,gradients = model.loss(features,targets,True)
checked = 0
largest = 0.
for key in KEYS:
    for flat in rng.choice(model.p[key].size,min(4,model.p[key].size),replace=False):
        index = np.unravel_index(flat,model.p[key].shape)
        value = model.p[key][index]
        model.p[key][index] = value+1e-5
        hi = model.loss(features,targets)
        model.p[key][index] = value-1e-5
        lo = model.loss(features,targets)
        model.p[key][index] = value
        error = abs((hi-lo)/2e-5-gradients[key][index])
        assert error<2e-6,(key,index,error)
        checked += 1
        largest = max(largest,error)
trained,initial,history = train_gain_ranker(features,targets*32,127307)
prediction = trained.forward(features)[0]
assert np.mean((prediction-targets)**2)<1e-5
assert int(np.argmax(prediction))==int(np.argmax(targets))
assert len(history)==300 and history[-1]['headSha256']==parameter_hash(trained.p)
assert any(not np.array_equal(initial[key],trained.p[key]) for key in KEYS)
print({'status':'pass','finiteDifferenceChecks':checked,'maximumGradientError':largest,
    'trainedUpdates':len(history),'gainOrderingVerified':True})
