"""Small shared NN predicts reward from source observations and NN actions."""
import hashlib
import numpy as np
from learn_pair_policy import Ranker,KEYS


class GainRanker(Ranker):
    def __init__(self,seed):
        super().__init__(seed,features=70,hidden=24)

    def loss(self,features,targets,gradients=False):
        output,(x,h1,h2) = self.forward(features)
        residual = output-targets
        loss = float(np.mean(residual*residual))
        if not gradients:
            return loss
        dz = 2*residual/len(residual)
        g2 = dz[:,None]*self.p['wo'][None,:]*(1-h2*h2)
        g1 = (g2@self.p['w2'].T)*(1-h1*h1)
        return loss,dict(wo=h2.T@dz,bo=np.array([dz.sum()]),w2=h1.T@g2,b2=g2.sum(0),
            w1=x.T@g1,b1=g1.sum(0))


def parameter_hash(parameters):
    h = hashlib.sha256()
    for key in KEYS:
        h.update(np.ascontiguousarray(parameters[key],dtype=np.float64).tobytes())
    return h.hexdigest()


def train_gain_ranker(features,gains,seed,updates=300):
    assert features.shape[1]==70 and 1<=len(features)<=128 and updates==300
    targets = np.clip(np.asarray(gains,dtype=np.float64),-32,32)/32
    model = GainRanker(seed)
    model.mean = features.mean(0)
    model.scale = np.maximum(.2,features.std(0))
    initial = {key:value.copy() for key,value in model.p.items()}
    first = {key:np.zeros_like(value) for key,value in model.p.items()}
    second = {key:value.copy() for key,value in first.items()}
    history = []
    for step in range(1,updates+1):
        before = parameter_hash(model.p)
        loss,gradients = model.loss(features,targets,True)
        norm = float(np.sqrt(sum(np.sum(g*g) for g in gradients.values())))
        for key in KEYS:
            gradient = gradients[key]*min(1.,5/max(norm,1e-12))
            first[key] = .9*first[key]+.1*gradient
            second[key] = .999*second[key]+.001*gradient*gradient
            model.p[key] -= .01*(first[key]/(1-.9**step))/(np.sqrt(second[key]/(1-.999**step))+1e-8)
        assert all(np.isfinite(value).all() for value in model.p.values())
        history.append(dict(step=step,beforeSha256=before,loss=loss,gradientNorm=norm,
            headSha256=parameter_hash(model.p)))
    return model,initial,history
