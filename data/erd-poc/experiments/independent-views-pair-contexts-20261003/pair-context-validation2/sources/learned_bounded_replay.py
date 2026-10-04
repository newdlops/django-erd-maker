"""Independent replay of the neural tanh translation output layer.

Bounds retain one currently separating axis for each moved/unmoved pair.
They are not searched using crossing scores and rejection has no fallback.
"""
import math
import numpy as np
from learned_global_replay import MovingRayReplay, bounds, rounded


def translation_box(positions, sizes, members, frame):
    physical=positions[0][members[0]];dimensions=sizes[0][members[0]]
    box=np.array([frame[0]-np.min(physical[:,0]-dimensions[:,0]/2),
                  frame[1]-np.max(physical[:,0]+dimensions[:,0]/2),
                  frame[2]-np.min(physical[:,1]-dimensions[:,1]/2),
                  frame[3]-np.max(physical[:,1]+dimensions[:,1]/2)])
    for points,dimensions,moved in zip(positions,sizes,members):
        mask=np.ones(len(points),dtype=bool);mask[moved]=False
        if not mask.any():continue
        offsets=points[mask][None,:,:]-points[moved][:,None,:]
        room=np.abs(offsets)-(dimensions[mask][None,:,:]+dimensions[moved][:,None,:])/2-[55.99,41.99]
        assert np.all((room[:,:,0]>=0)|(room[:,:,1]>=0)), 'bounded action source has a spacing violation'
        choose_x=(room[:,:,0]>=0)&((room[:,:,1]<0)|(np.floor(room[:,:,0]*1e6+.5)>=np.floor(room[:,:,1]*1e6+.5)))
        for axis,selected in ((0,choose_x),(1,~choose_x)):
            positive=selected&(offsets[:,:,axis]>0)
            negative=selected&~(offsets[:,:,axis]>0)
            if positive.any():box[2*axis+1]=min(box[2*axis+1],room[:,:,axis][positive].min())
            if negative.any():box[2*axis]=max(box[2*axis],-room[:,:,axis][negative].min())
    assert np.all(box[::2]<=0) and np.all(box[1::2]>=0), 'bounded action must contain zero'
    box[::2]=np.ceil(np.minimum(0,box[::2]+1e-7)*100)/100
    box[1::2]=np.floor(np.maximum(0,box[1::2]-1e-7)*100)/100
    return box


def decode_latent(action, box):
    fractions=[math.tanh(float(f'{value:.12g}')) for value in action]
    return [rounded(fraction*(box[2*k+1] if fraction>=0 else -box[2*k]))
            for k,fraction in enumerate(fractions)]


class BoundedMovingRayReplay(MovingRayReplay):
    def __init__(self,directory,area_limit,branches=False):
        super().__init__(directory,area_limit,branches)
        self.frame=bounds(self.positions[0],self.sizes[0])
        self.ids=[list(rows) for rows in self.positions]
        self.index=[{key:i for i,key in enumerate(keys)} for keys in self.ids]
        self.arrays=[np.array([rows[key] for key in keys]) for rows,keys in zip(self.positions,self.ids)]
        self.dimensions=[np.array([rows[key] for key in keys]) for rows,keys in zip(self.sizes,self.ids)]
        self.members=[]
        for key in self.context_keys:
            group=self.branches[key]
            self.members.append([[self.index[0][node] for node in group],
                                 sorted(self.index[1][node] for physical in group for node in self.components[physical])])

    def box(self,node):
        return translation_box(self.arrays,self.dimensions,self.members[node],self.frame)

    def apply_record(self,record):
        box=self.box(record['node']);result=record['result']
        np.testing.assert_allclose(box,result['actionBounds'],rtol=0,atol=1e-8)
        delta=decode_latent(record['action'],box)
        np.testing.assert_allclose(delta,result['decodedDelta'],rtol=0,atol=1e-8)
        assert box[0]<=delta[0]<=box[1] and box[2]<=delta[1]<=box[3]
        if not result['accepted']:return
        super().apply_record({**record,'action':delta})
        for points,members in zip(self.arrays,self.members[record['node']]):points[members]+=delta


def self_test():
    # Unequal cards: x clearance is exactly zero, so only y can move.
    points=np.array([[0.,0.],[355.99,0.],[0.,500.]])
    sizes=np.array([[200.,100.],[400.,100.],[200.,100.]])
    box=translation_box([points,points],[sizes,sizes],[[0],[0]],[-1000,1000,-1000,1000])
    assert box[1]==0 and box[0]<0 and box[2]<0 and box[3]>0
    assert decode_latent([0.,0.],box)==[0.,0.]
    for x in [-1e9,-1.,0.,1.,1e9]:
        for y in [-1e9,-1.,0.,1.,1e9]:
            delta=decode_latent([x,y],box)
            assert box[0]<=delta[0]<=box[1] and box[2]<=delta[1]<=box[3]
    # Both cards belong to a rigid group: no artificial constraint inside it.
    all_members=[[0,1,2],[0,1,2]]
    whole=translation_box([points,points],[sizes,sizes],all_members,[-1000,1000,-1000,1000])
    assert whole[1]>0
    shifted=points+[8123.,-998.]
    np.testing.assert_allclose(whole,translation_box([shifted,shifted],[sizes,sizes],all_members,
        [-1000+8123,1000+8123,-1000-998,1000-998]),rtol=0,atol=1e-8)
    print('{"boundedDecoderFixture":"pass","zeroIdentityAndSaturation":true,"groupAndTranslationInvariance":true}')


if __name__=='__main__':self_test()
