"""Train a shared neural head from noncommitting native geometry rewards.

Antithetic Gaussian parameter probes estimate the gradient of the smoothed
native objective. The encoder is fixed; no card coordinate is a parameter.
Every probe and Adam head update is replayable from its recorded seed and the
initial network. Only updated neural checkpoints are submitted for acceptance.
"""
import copy
from collections import OrderedDict
import hashlib
import io
import json
import numpy as np


def head_hash(head):
    return hashlib.sha256(np.ascontiguousarray(head,dtype=np.float64).tobytes()).hexdigest()


def head_vector(model, keys=('wo',), scales=(1.,)):
    assert len(keys)==len(scales) and len(set(keys))==len(keys)
    assert set(keys)<=set(model.p) and all(0<scale<=16 for scale in scales)
    return np.concatenate([model.p[key].ravel()/scale for key,scale in zip(keys,scales)])


def set_head_vector(model, vector, keys=('wo',), scales=(1.,)):
    offset=0
    for key,scale in zip(keys,scales):
        shape=model.p[key].shape;size=model.p[key].size
        model.p[key]=(vector[offset:offset+size]*scale).reshape(shape).copy()
        offset+=size
    assert offset==len(vector)


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
    def __init__(self, model, features, request, formatter, stream, seed, sigma, directions, rate,
                 keys=('wo',), scales=(1.,), cache_size=0):
        # Opt in only when MEASURE is a pure function of the command and the
        # immutable source. Each trainer owns its cache; TRY is never cached.
        assert isinstance(cache_size,int) and 0<=cache_size<=128
        self.model,self.features,self.request,self.formatter=model,features,request,formatter
        self.stream,self.seed,self.sigma,self.directions,self.rate=stream,seed,sigma,directions,rate
        self.keys,self.scales=tuple(keys),tuple(scales)
        self.first=np.zeros_like(head_vector(model,keys,scales));self.second=np.zeros_like(self.first)
        self.iterations=0;self.measurements=0;self.ever_updated=False
        self.cache_size=cache_size;self.measurement_cache=OrderedDict()
        self.native_measurements=0;self.cache_hits=0

    def measure(self, command):
        assert command.startswith('MEASURE ')
        if command in self.measurement_cache:
            result=self.measurement_cache.pop(command)
            self.measurement_cache[command]=result
            self.cache_hits+=1
            return copy.deepcopy(result),True
        result=self.request(command)
        assert result['measureOnly'] and not result['accepted']
        self.native_measurements+=1
        if self.cache_size:
            # Full wire text is the key, so reuse does not depend on a hash
            # collision assumption or a geometry-equivalence approximation.
            self.measurement_cache[command]=copy.deepcopy(result)
            if len(self.measurement_cache)>self.cache_size:self.measurement_cache.popitem(last=False)
        return result,False

    def step(self):
        self.iterations+=1
        base=head_vector(self.model,self.keys,self.scales);gradient=np.zeros_like(base);probes=[];scores=[]
        original={key:self.model.p[key].copy() for key in self.keys}
        try:
            for index in range(self.directions):
                seed=self.seed+self.iterations*100+index
                noise=np.random.default_rng(seed).normal(size=base.shape)
                samples=[]
                for sign in [1.,-1.]:
                    set_head_vector(self.model,base+sign*self.sigma*noise,self.keys,self.scales)
                    command=measurement_text(self.model.forward(self.features)[0],self.formatter)
                    result,cache_hit=self.measure(command);score=objective(result)
                    sample={'actionSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result}
                    if self.cache_size:sample['cacheHit']=cache_hit
                    samples.append(sample)
                    scores.append(score);self.measurements+=1
                gradient+=(scores[-2]-scores[-1])/(2*self.sigma*self.directions)*noise
                probes.append({'noiseSeed':seed,'samples':samples})
        finally:self.model.p.update(original)
        head,self.first,self.second=adam_head(base,gradient,self.first,self.second,self.iterations,self.rate)
        set_head_vector(self.model,head,self.keys,self.scales)
        head=head_vector(self.model,self.keys,self.scales)
        self.ever_updated|=not np.array_equal(base,head)
        record={'iteration':self.iterations,'baseHeadSha256':head_hash(base),
            'parameterKeys':self.keys,'parameterScales':self.scales,
            'sigma':self.sigma,'rate':self.rate,'directions':probes,'trainedHeadSha256':head_hash(head)}
        if self.cache_size:record['measurementCacheSize']=self.cache_size
        self.stream.write(json.dumps(record)+'\n')
        self.stream.flush()
        if not self.ever_updated:self.sigma=min(.05,self.sigma*2)
        return {'total':float(np.mean(scores)),'rewardMeasurements':self.measurements,
                'minimumMeasuredObjective':min(scores),'gradientNorm':float(np.linalg.norm(gradient)),
                'headUpdated':not np.array_equal(base,head)}


def verify_trace(model, features, records, formatter, expected_heads=None):
    first=second=None;checked=0;configuration=None;measurement_cache=OrderedDict()
    for step,record in enumerate(records,1):
        assert record['iteration']==step
        keys=tuple(record.get('parameterKeys',['wo']));scales=tuple(record.get('parameterScales',[1.]))
        cache_size=record.get('measurementCacheSize',0)
        assert isinstance(cache_size,int) and 0<=cache_size<=128
        if configuration is None:configuration=(keys,scales,cache_size)
        assert configuration==(keys,scales,cache_size)
        base=head_vector(model,keys,scales);assert head_hash(base)==record['baseHeadSha256']
        original={key:model.p[key].copy() for key in keys}
        if first is None:first=np.zeros_like(base);second=np.zeros_like(base)
        gradient=np.zeros_like(base)
        for probe in record['directions']:
            noise=np.random.default_rng(probe['noiseSeed']).normal(size=base.shape);scores=[]
            for sign,sample in zip([1.,-1.],probe['samples']):
                set_head_vector(model,base+sign*record['sigma']*noise,keys,scales)
                command=measurement_text(model.forward(features)[0],formatter)
                assert hashlib.sha256(command.encode()).hexdigest()==sample['actionSha256']
                if cache_size:
                    assert sample['cacheHit'] is (command in measurement_cache)
                    if command in measurement_cache:
                        assert measurement_cache[command]==sample['result']
                        measurement_cache.move_to_end(command)
                    else:
                        measurement_cache[command]=copy.deepcopy(sample['result'])
                        if len(measurement_cache)>cache_size:measurement_cache.popitem(last=False)
                else:assert 'cacheHit' not in sample
                scores.append(objective(sample['result']));checked+=1
            gradient+=(scores[0]-scores[1])/(2*record['sigma']*len(record['directions']))*noise
        head,first,second=adam_head(base,gradient,first,second,step,record['rate'])
        model.p.update(original)
        set_head_vector(model,head,keys,scales)
        head=head_vector(model,keys,scales)
        assert head_hash(head)==record['trainedHeadSha256']
        if expected_heads and step in expected_heads:assert head_hash(head)==expected_heads[step]
    return checked


def self_test(coupled=False):
    class LinearFixture:
        def __init__(self):self.p={'wo':np.zeros((2,1)),'ewo':np.zeros((2,1)),'fixed':np.array([7.])}
        def forward(self,features):
            nodes=features@self.p['wo']
            return (np.concatenate([nodes,features@self.p['ewo']]) if coupled else nodes),None
    model=LinearFixture();initial=copy.deepcopy(model)
    features=np.array([[1.,0.],[0.,1.],[1.,-1.]])
    target=features@np.array([[1.],[-.5]])
    if coupled:target=np.concatenate([target,features@np.array([[-.3],[.8]])])
    def formatter(action):return 'TRY '+' '.join(f'{v:.12g}' for v in action.ravel())
    def request(command):
        assert command.startswith('MEASURE ')
        prediction=np.fromstring(command[8:],sep=' ').reshape(-1,1)
        return {'measureOnly':True,'accepted':False,'legal':True,'visual':float(np.sum((prediction-target)**2))}
    stream=io.StringIO()
    keys,scales=(('wo','ewo'),(1.,4.)) if coupled else (('wo',),(1.,))
    trainer=RewardHeadTrainer(model,features,request,formatter,stream,811,.02,4,.035,keys,scales)
    steps=160 if coupled else 96
    for _ in range(steps):trainer.step()
    loss=float(np.sum((model.forward(features)[0]-target)**2))
    assert loss<float(np.sum(target**2))*.01
    checked=verify_trace(initial,features,map(json.loads,stream.getvalue().splitlines()),formatter)
    for key in model.p:np.testing.assert_array_equal(initial.p[key],model.p[key])
    np.testing.assert_array_equal(model.p['fixed'],[7.])
    if coupled:assert np.any(model.p['ewo']) and np.any(model.p['wo'])
    print(json.dumps({'rewardOptimizationFixturePassed':True,'coupledHeads':coupled,'probeActionsReplayed':checked,
        'adamUpdatesReplayed':steps,'finalSquaredError':loss,'passed':True}))


if __name__=='__main__':
    self_test()
    self_test(coupled=True)
