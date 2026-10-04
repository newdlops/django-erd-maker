"""Neural slot ordering with structurally separated card positions.

The model predicts ordering logits and within-cell offsets. Stable sorting is
its deterministic discrete output layer; it never reads geometry scores or
searches candidate assignments. Training uses a documented soft-rank
straight-through gradient, while every evaluated batch uses the hard order.
Only shared MLP weights are trainable. Source geometry and grid bounds are
immutable decoder buffers.
"""
import json
import math
from pathlib import Path
import tempfile
import numpy as np
from joint_layout_proxy import JointPolicy, sigmoid


class GridOrderPolicy(JointPolicy):
    buffers=('mean','scale','negative','positive','origin','frame_low','cell_size',
             'grid_shape','jitter_room','rank_origin','rank_temperature')

    def __init__(self, features, positions, sizes, max_step, seed):
        super().__init__(features,positions,sizes,max_step,seed)
        self.origin=positions.copy()
        self.frame_low=(positions-sizes/2).min(0)
        frame_high=(positions+sizes/2).max(0)
        count=len(positions)
        columns=math.ceil(math.sqrt(count));rows=math.ceil(count/columns)
        self.grid_shape=np.array([columns,rows],dtype=np.int32)
        self.cell_size=(frame_high-self.frame_low)/self.grid_shape
        self.jitter_room=(self.cell_size-sizes.max(0)-np.array([58.,44.]))/2
        if (self.jitter_room<=0).any():raise ValueError('source frame cannot fit separated uniform cells')
        self.rank_origin=2*(positions-self.frame_low)/(frame_high-self.frame_low)-1
        self.rank_temperature=np.array(.05)
        self.p['wo']=np.zeros((32,4));self.p['bo']=np.zeros(4)
        # Break grid-line collinearity using neural jitter, not coordinate edits.
        self.p['wo'][:,2:]=np.random.default_rng(seed+27000).normal(0,.03,(32,2))

    def forward(self, features):
        x=(features-self.mean)/self.scale
        h1=np.tanh(x@self.p['w1']+self.p['b1'])
        h2=np.tanh(h1@self.p['w2']+self.p['b2'])
        y=np.tanh(h2@self.p['wo']+self.p['bo'])
        rank=self.rank_origin+2*y[:,:2]
        order=np.argsort(rank[:,0],kind='stable')
        columns=np.empty(len(rank),dtype=np.int32)
        columns[order]=np.arange(len(rank))//self.grid_shape[1]
        slots=np.empty_like(rank);slots[:,0]=columns
        for column in np.unique(columns):
            members=np.flatnonzero(columns==column)
            ordered=members[np.argsort(rank[members,1],kind='stable')]
            slots[ordered,1]=np.linspace(0,self.grid_shape[1]-1,len(members)) if len(members)>1 else (self.grid_shape[1]-1)/2
        position=self.frame_low+self.cell_size*(slots+.5)+self.jitter_room*y[:,2:]
        return position-self.origin,(x,h1,h2,y,rank,columns)

    def rank_backward(self, values, gradient):
        probability=sigmoid((values[:,None]-values[None,:])/self.rank_temperature)
        derivative=probability*(1-probability)/self.rank_temperature
        return derivative.sum(1)*gradient-derivative@gradient

    def backward(self, cache, gradient, hidden_gradient=None):
        if hidden_gradient is not None:raise ValueError('grid ordering has no edge head')
        x,h1,h2,y,rank,columns=cache
        rank_gradient=np.zeros_like(rank)
        rank_gradient[:,0]=self.rank_backward(rank[:,0],gradient[:,0]*self.cell_size[0]/self.grid_shape[1])
        for column in np.unique(columns):
            members=np.flatnonzero(columns==column)
            if len(members)>1:
                scale=self.cell_size[1]*(self.grid_shape[1]-1)/(len(members)-1)
                rank_gradient[members,1]=self.rank_backward(rank[members,1],gradient[members,1]*scale)
        dy=np.concatenate([2*rank_gradient,gradient*self.jitter_room],axis=1)*(1-y*y)
        return self.weights_backward(cache,dy)

    def backward_latent(self, cache, rank_gradient):
        y=cache[3]
        dy=np.concatenate([2*rank_gradient,np.zeros_like(rank_gradient)],axis=1)*(1-y*y)
        return self.weights_backward(cache,dy)

    def weights_backward(self, cache, dy):
        x,h1,h2=cache[:3]
        result={'wo':h2.T@dy,'bo':dy.sum(0)}
        dh2=(dy@self.p['wo'].T)*(1-h2*h2)
        result.update(w2=h1.T@dh2,b2=dh2.sum(0))
        dh1=(dh2@self.p['w2'].T)*(1-h1*h1)
        result.update(w1=x.T@dh1,b1=dh1.sum(0))
        return result

    def surrogate(self, features, columns):
        """Smooth training surrogate with the current hard column groups fixed."""
        _,cache=self.forward(features)
        y,rank=cache[3:5]
        soft=np.empty_like(rank)
        soft[:,0]=(sigmoid((rank[:,None,0]-rank[None,:,0])/self.rank_temperature).sum(1)-.5)/self.grid_shape[1]
        for column in np.unique(columns):
            members=np.flatnonzero(columns==column)
            if len(members)>1:
                values=rank[members,1]
                soft[members,1]=(sigmoid((values[:,None]-values[None,:])/self.rank_temperature).sum(1)-.5)*(self.grid_shape[1]-1)/(len(members)-1)
            else:soft[members,1]=(self.grid_shape[1]-1)/2
        return self.frame_low+self.cell_size*(soft+.5)+self.jitter_room*y[:,2:]-self.origin

    def save(self, path, metadata):
        np.savez_compressed(path,**self.p,**{k:getattr(self,k) for k in self.buffers},metadata=json.dumps(metadata))

    @classmethod
    def load(cls, path):
        model=object.__new__(cls)
        with np.load(path) as data:
            model.p={k:data[k].copy() for k in cls.keys}
            for key in cls.buffers:setattr(model,key,data[key].copy())
        return model


def self_test():
    rng=np.random.default_rng(773)
    count=19
    positions=np.array([[i%5*900.,i//5*800.] for i in range(count)])
    sizes=rng.uniform([90.,40.],[160.,120.],(count,2))
    features=rng.normal(size=(count,23))
    model=GridOrderPolicy(features,positions,sizes,5000.,773)
    gradient=rng.normal(size=positions.shape)
    _,cache=model.forward(features)
    analytic=model.backward(cache,gradient)
    columns=cache[-1]
    checked=0
    for name in model.keys:
        for flat in np.argsort(abs(analytic[name]).ravel())[-4:]:
            index=np.unravel_index(flat,model.p[name].shape)
            original=model.p[name][index];step=1e-6
            model.p[name][index]=original+step;hi=np.sum(model.surrogate(features,columns)*gradient)
            model.p[name][index]=original-step;lo=np.sum(model.surrogate(features,columns)*gradient)
            model.p[name][index]=original
            np.testing.assert_allclose(analytic[name][index],(hi-lo)/(2*step),atol=3e-5,rtol=3e-5)
            checked+=1
    low=(positions-sizes/2).min(0);high=(positions+sizes/2).max(0)
    for _ in range(64):
        model.p['wo']=rng.normal(0,.7,model.p['wo'].shape)
        model.p['bo']=rng.normal(0,.7,model.p['bo'].shape)
        placed=positions+model.forward(features)[0]
        assert (placed-sizes/2>=low).all() and (placed+sizes/2<=high).all()
        overlap=(sizes[:,None,:]+sizes[None,:,:])/2+np.array([56.,42.])-abs(placed[:,None,:]-placed[None,:,:])
        assert not np.triu((overlap>0).all(2),1).any()
    with tempfile.TemporaryDirectory() as temporary:
        path=Path(temporary)/'model.npz';model.save(path,{'validationOnly':True})
        np.testing.assert_array_equal(GridOrderPolicy.load(path).forward(features)[0],model.forward(features)[0])
    print(json.dumps({'softRankSurrogateDerivativeChecks':checked,'hardGradientIsStraightThrough':True,
        'randomizedSeparatedFrameChecks':64,'frozenRoundtripExact':True,'passed':True}))


if __name__=='__main__':self_test()
