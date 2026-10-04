"""A shared neural ranking head permutes cards among equal-size source slots.

All original rectangles remain occupied with the same dimensions. Stable
sorting of neural logits is the only assignment decoder: it has no access to
edge scores and performs no geometric optimization or repair. Only MLP weights
are trained; the source slot geometry, shape groups, and base order are fixed.
"""
import json
from pathlib import Path
import tempfile
import numpy as np
from joint_layout_proxy import JointPolicy


class SlotPermutationPolicy(JointPolicy):
    buffers=('mean','scale','negative','positive','slot_origin','slot_order','group_bounds','rank_origin')

    def __init__(self, features, positions, sizes, max_step, seed):
        super().__init__(features,positions,sizes,max_step,seed)
        self.slot_origin=positions.copy()
        self.p['wo']=np.zeros((32,1));self.p['bo']=np.zeros(1)
        normalized=(positions-positions.min(0))/np.maximum(1.,np.ptp(positions,axis=0))
        integer=np.floor(normalized*65535).astype(np.uint64)
        morton=np.zeros(len(positions),dtype=np.uint64)
        for bit in range(16):
            morton|=((integer[:,0]>>bit)&1)<<(2*bit)
            morton|=((integer[:,1]>>bit)&1)<<(2*bit+1)
        groups={}
        for index,size in enumerate(sizes):groups.setdefault(tuple(size),[]).append(index)
        order=[];bounds=[0];self.rank_origin=np.zeros(len(positions))
        for shape in sorted(groups):
            members=np.array(groups[shape])
            members=members[np.argsort(morton[members],kind='stable')]
            self.rank_origin[members]=np.linspace(-1.,1.,len(members)) if len(members)>1 else 0.
            order.extend(members.tolist());bounds.append(len(order))
        self.slot_order=np.array(order,dtype=np.int32)
        self.group_bounds=np.array(bounds,dtype=np.int32)

    def forward(self, features):
        x=(features-self.mean)/self.scale
        h1=np.tanh(x@self.p['w1']+self.p['b1'])
        h2=np.tanh(h1@self.p['w2']+self.p['b2'])
        score=self.rank_origin+(h2@self.p['wo']+self.p['bo'])[:,0]
        positions=np.empty_like(self.slot_origin)
        for begin,end in zip(self.group_bounds[:-1],self.group_bounds[1:]):
            slots=self.slot_order[begin:end]
            nodes=slots[np.argsort(score[slots],kind='stable')]
            positions[nodes]=self.slot_origin[slots]
        return positions-self.slot_origin,(x,h1,h2,score)

    def backward(self, *args, **kwargs):
        raise ValueError('hard slot ordering is trained from measured rewards, not a coordinate gradient')

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
    rng=np.random.default_rng(787)
    count=35
    positions=np.array([[i%7*700.,i//7*400.] for i in range(count)])
    sizes=np.array([[120.,80.],[140.,60.],[120.,80.],[170.,90.],[140.,60.]]*7)
    features=rng.normal(size=(count,19))
    model=SlotPermutationPolicy(features,positions,sizes,5000.,787)
    np.testing.assert_array_equal(model.forward(features)[0],0.)
    expected=sorted(map(tuple,np.concatenate([positions,sizes],axis=1)))
    changed=0
    for _ in range(64):
        model.p['wo']=rng.normal(0,.5,model.p['wo'].shape)
        delta,_=model.forward(features);changed+=np.count_nonzero(delta)
        actual=sorted(map(tuple,np.concatenate([positions+delta,sizes],axis=1)))
        assert actual==expected
    assert changed>0
    with tempfile.TemporaryDirectory() as temporary:
        path=Path(temporary)/'policy.npz';model.save(path,{'validationOnly':True})
        np.testing.assert_array_equal(SlotPermutationPolicy.load(path).forward(features)[0],model.forward(features)[0])
    print(json.dumps({'randomizedRectangleMultisetChecks':64,'zeroHeadPreservesAllPositions':True,
        'frozenRoundtripExact':True,'passed':True}))


if __name__=='__main__':self_test()
