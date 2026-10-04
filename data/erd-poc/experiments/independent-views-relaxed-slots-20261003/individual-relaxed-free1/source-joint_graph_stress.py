"""A graph-distance objective for the neural ordering model's continuous code.

Graph distances are fixed training inputs, never coordinate targets. Exact
native conflicts still decide whether the model's hard decoded layout is kept.
"""
from collections import deque
import json
import numpy as np


class GraphStress:
    def __init__(self, count, edges, seed, sample_count=32768):
        adjacency=[set() for _ in range(count)]
        for a,b in edges:adjacency[a].add(int(b));adjacency[b].add(int(a))
        distances=np.full((count,count),-1,dtype=np.int16)
        for source in range(count):
            distances[source,source]=0;queue=deque([source])
            while queue:
                node=queue.popleft()
                for other in adjacency[node]:
                    if distances[source,other]<0:
                        distances[source,other]=distances[source,node]+1;queue.append(other)
        rng=np.random.default_rng(seed)
        pairs=rng.integers(0,count,(sample_count,2))
        pairs=np.concatenate([pairs,np.asarray(edges)],axis=0)
        pairs=np.sort(pairs[pairs[:,0]!=pairs[:,1]],axis=1)
        self.pairs=np.unique(pairs,axis=0)
        steps=distances[self.pairs[:,0],self.pairs[:,1]].astype(float)
        connected=steps>0
        steps[~connected]=float(distances.max()+2)
        self.target=.15*steps
        self.weight=1/np.maximum(.15,self.target)**2
        self.report={'samplePairs':len(self.pairs),'edgePairsIncluded':len(np.unique(np.sort(edges,axis=1),axis=0)),
                     'graphDiameter':int(distances.max()),'disconnectedSamplePairs':int((~connected).sum()),
                     'graphDistanceScale':.15,'coordinateTargetsUsed':False}

    def loss(self, code):
        first,second=self.pairs.T
        delta=code[first]-code[second]
        length=np.sqrt(np.sum(delta*delta,axis=1)+1e-8)
        residual=length-self.target
        value=float(np.mean(self.weight*residual**2))
        force=(2*self.weight*residual/length/len(first))[:,None]*delta
        gradient=np.zeros_like(code)
        np.add.at(gradient,first,force);np.add.at(gradient,second,-force)
        return {'total':value,'graphDistanceStress':value},gradient


def self_test():
    from joint_grid_policy import GridOrderPolicy
    rng=np.random.default_rng(823)
    positions=np.array([[i%4*900.,i//4*800.] for i in range(15)])
    sizes=np.tile([120.,80.],(len(positions),1))
    edges=np.array([[i,i+1] for i in range(11)]+[[0,4],[2,6],[3,9]])
    features=rng.normal(size=(len(positions),17))
    model=GridOrderPolicy(features,positions,sizes,5000.,823)
    model.p['wo']=rng.normal(0,.02,model.p['wo'].shape)
    stress=GraphStress(len(positions),edges,823,128)
    def value():return stress.loss(model.forward(features)[1][4])[0]['total']
    _,cache=model.forward(features)
    _,gradient=stress.loss(cache[4])
    analytic=model.backward_latent(cache,gradient)
    checked=0
    for name in model.keys:
        for flat in np.argsort(abs(analytic[name]).ravel())[-4:]:
            index=np.unravel_index(flat,model.p[name].shape)
            original=model.p[name][index];step=1e-6
            model.p[name][index]=original+step;hi=value()
            model.p[name][index]=original-step;lo=value()
            model.p[name][index]=original
            np.testing.assert_allclose(analytic[name][index],(hi-lo)/(2*step),atol=1e-6,rtol=1e-5);checked+=1
    print(json.dumps({'graphStressNetworkDerivativeChecks':checked,'objectiveUsesContinuousCode':True,
        'hardNativeLayoutStillRequired':True,'passed':True}))


if __name__=='__main__':self_test()
