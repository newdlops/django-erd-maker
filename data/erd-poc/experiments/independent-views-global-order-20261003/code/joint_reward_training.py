"""Train a shared neural head from noncommitting native geometry rewards.

Antithetic Gaussian parameter probes estimate the gradient of the smoothed
native objective. The encoder is fixed; no card coordinate is a parameter.
Every probe and Adam head update is replayable from its recorded seed and the
initial network. Only updated neural checkpoints are submitted for acceptance.
"""
import copy
import hashlib
import io
import json
import numpy as np


def head_hash(head):
    return hashlib.sha256(np.ascontiguousarray(head,dtype=np.float64).tobytes()).hexdigest()


def measurement_text(action, formatter):
    command=formatter(action)
    assert command.startswith('TRY ')
    return 'MEASURE '+command[4:]


def objective(result):
    assert result['measureOnly'] and not result['accepted']
    if result['legal']:return float(result['visual'])
    return 1e6+result['visual']+1000*sum(result[k] for k in ['spacing','hard','individualHard'])


def adam_head(base, gradient, first, second, step, rate):
    norm=float(np.linalg.norm(gradient))
    gradient=gradient*min(1.,10/max(norm,1e-12))
    first=.9*first+.1*gradient
    second=.999*second+.001*gradient**2
    head=base-rate*(first/(1-.9**step))/(np.sqrt(second/(1-.999**step))+1e-8)
    assert np.isfinite(head).all()
    return head,first,second


class RewardHeadTrainer:
    def __init__(self, model, features, request, formatter, stream, seed, sigma, directions, rate):
        self.model,self.features,self.request,self.formatter=model,features,request,formatter
        self.stream,self.seed,self.sigma,self.directions,self.rate=stream,seed,sigma,directions,rate
        self.first=np.zeros_like(model.p['wo']);self.second=np.zeros_like(model.p['wo'])
        self.iterations=0;self.measurements=0;self.ever_updated=False

    def step(self):
        self.iterations+=1
        base=self.model.p['wo'].copy();gradient=np.zeros_like(base);probes=[];scores=[]
        try:
            for index in range(self.directions):
                seed=self.seed+self.iterations*100+index
                noise=np.random.default_rng(seed).normal(size=base.shape)
                samples=[]
                for sign in [1.,-1.]:
                    self.model.p['wo']=base+sign*self.sigma*noise
                    command=measurement_text(self.model.forward(self.features)[0],self.formatter)
                    result=self.request(command);score=objective(result)
                    samples.append({'actionSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result})
                    scores.append(score);self.measurements+=1
                gradient+=(scores[-2]-scores[-1])/(2*self.sigma*self.directions)*noise
                probes.append({'noiseSeed':seed,'samples':samples})
        finally:self.model.p['wo']=base
        head,self.first,self.second=adam_head(base,gradient,self.first,self.second,self.iterations,self.rate)
        self.model.p['wo']=head
        self.ever_updated|=not np.array_equal(base,head)
        self.stream.write(json.dumps({'iteration':self.iterations,'baseHeadSha256':head_hash(base),
            'sigma':self.sigma,'rate':self.rate,'directions':probes,'trainedHeadSha256':head_hash(head)})+'\n')
        self.stream.flush()
        if not self.ever_updated:self.sigma=min(.05,self.sigma*2)
        return {'total':float(np.mean(scores)),'rewardMeasurements':self.measurements,
                'minimumMeasuredObjective':min(scores),'gradientNorm':float(np.linalg.norm(gradient)),
                'headUpdated':not np.array_equal(base,head)}


def verify_trace(model, features, records, formatter, expected_heads=None):
    first=np.zeros_like(model.p['wo']);second=np.zeros_like(model.p['wo'])
    checked=0
    for step,record in enumerate(records,1):
        assert record['iteration']==step
        base=model.p['wo'].copy();assert head_hash(base)==record['baseHeadSha256']
        gradient=np.zeros_like(base)
        for probe in record['directions']:
            noise=np.random.default_rng(probe['noiseSeed']).normal(size=base.shape);scores=[]
            for sign,sample in zip([1.,-1.],probe['samples']):
                model.p['wo']=base+sign*record['sigma']*noise
                command=measurement_text(model.forward(features)[0],formatter)
                assert hashlib.sha256(command.encode()).hexdigest()==sample['actionSha256']
                scores.append(objective(sample['result']));checked+=1
            gradient+=(scores[0]-scores[1])/(2*record['sigma']*len(record['directions']))*noise
        head,first,second=adam_head(base,gradient,first,second,step,record['rate'])
        assert head_hash(head)==record['trainedHeadSha256']
        if expected_heads and step in expected_heads:assert head_hash(head)==expected_heads[step]
        model.p['wo']=head
    return checked


def self_test():
    class LinearFixture:
        def __init__(self):self.p={'wo':np.zeros((2,1))}
        def forward(self,features):return features@self.p['wo'],None
    model=LinearFixture();initial=copy.deepcopy(model)
    features=np.array([[1.,0.],[0.,1.],[1.,-1.]])
    target=features@np.array([[1.],[-.5]])
    def formatter(action):return 'TRY '+' '.join(f'{v:.12g}' for v in action.ravel())
    def request(command):
        assert command.startswith('MEASURE ')
        prediction=np.fromstring(command[8:],sep=' ').reshape(-1,1)
        return {'measureOnly':True,'accepted':False,'legal':True,'visual':float(np.sum((prediction-target)**2))}
    stream=io.StringIO()
    trainer=RewardHeadTrainer(model,features,request,formatter,stream,811,.02,4,.035)
    for _ in range(96):trainer.step()
    loss=float(np.sum((model.forward(features)[0]-target)**2))
    assert loss<float(np.sum(target**2))*.01
    checked=verify_trace(initial,features,map(json.loads,stream.getvalue().splitlines()),formatter)
    np.testing.assert_array_equal(initial.p['wo'],model.p['wo'])
    print(json.dumps({'rewardOptimizationFixturePassed':True,'probeActionsReplayed':checked,
        'adamUpdatesReplayed':96,'finalSquaredError':loss,'passed':True}))


if __name__=='__main__':self_test()
