"""Shared neural deformation with compact source-order spacing constraints.

Only output weights are learned. Bounds retain one separating source axis per
pair and the complete source frame. This decoder does not consult visual cost,
choose between coordinates, repair proposals or accept geometry.
"""
import numpy as np
from joint_separation_policy import decode_separation


def patch_buffers(positions,sizes,active,max_step):
    active=np.asarray(active,dtype=np.int32); assert len(set(active.tolist()))==len(active)
    assert 1<=len(active)<=16 and 0<max_step<=2048
    p,s=positions[active],sizes[active]; count=len(active)
    frame_low=(positions-sizes/2).min(0); frame_high=(positions+sizes/2).max(0)
    lower=np.ceil(np.minimum(0.,np.maximum(-max_step,frame_low-p+s/2)+1e-7)*100).astype(np.int64)
    upper=np.floor(np.maximum(0.,np.minimum(max_step,frame_high-p-s/2)-1e-7)*100).astype(np.int64)
    selected={int(n):i for i,n in enumerate(active)}; arcs=[[],[]]
    for local,n in enumerate(active):
        others=np.flatnonzero(np.arange(len(positions))!=n); delta=positions[others]-positions[n]
        room=abs(delta)-(sizes[others]+sizes[n])/2-[55.99,41.99]
        assert ((room[:,0]>=-1e-8)|(room[:,1]>=-1e-8)).all()
        use_x=(room[:,0]>=0)&((room[:,1]<0)|(np.floor(room[:,0]*1e6+.5)>=np.floor(room[:,1]*1e6+.5)))
        axes=np.where(use_x,0,1)
        for row,m in enumerate(others):
            axis=int(axes[row]); gap=max(0.,float(room[row,axis])-1e-7)
            lag=int(np.ceil(-gap*100)); ahead=bool(delta[row,axis]>0)
            if int(m) not in selected:
                if ahead: upper[local,axis]=min(upper[local,axis],-lag)
                else: lower[local,axis]=max(lower[local,axis],lag)
            elif n<m:
                other=selected[int(m)]; source,target=(local,other) if ahead else (other,local)
                arcs[axis].append((source,target,lag))
    result={'dag_lower':lower,'dag_upper':upper}
    for axis,name in enumerate(('x','y')):
        rows=sorted(arcs[axis],key=lambda r:(r[1],r[0]))
        parents=np.array([r[0] for r in rows],dtype=np.int32)
        children=np.array([r[1] for r in rows],dtype=np.int32)
        lags=np.array([r[2] for r in rows],dtype=np.int64)
        ptr=np.r_[0,np.cumsum(np.bincount(children,minlength=count))].astype(np.int32)
        order=np.argsort(p[:,axis],kind='stable').astype(np.int32)
        for node in order[::-1]:
            start,end=ptr[node:node+2]
            np.minimum.at(upper[:,axis],parents[start:end],upper[node,axis]-lags[start:end])
        for key,value in [('order',order),('ptr',ptr),('parents',parents),('lags',lags)]: result[f'dag_{name}_{key}']=value
    assert (lower<=0).all() and (upper>=0).all()
    return result


def decode_patch(fractions,buffers):
    return decode_separation(fractions,buffers,True)[0]


class PatchActor:
    def __init__(self,features,decoder,active,max_step,seed):
        self.decoder=decoder; self.active=np.asarray(active,dtype=np.int32)
        self.buffers=patch_buffers(decoder.positions,decoder.sizes,self.active,max_step)
        features=np.asarray(features,dtype=np.float32)
        self.mean=features.mean(0,dtype=np.float64); self.scale=np.maximum(.2,features.std(0,dtype=np.float64))
        rng=np.random.default_rng(seed); self.w1=rng.normal(0,1/np.sqrt(64),(64,12)); self.w2=rng.normal(0,1/np.sqrt(12),(12,8))
        self.embedding=np.tanh(np.tanh(np.clip((features[self.active]-self.mean)/self.scale,-8,8))@self.w1)@self.w2
        self.embedding=np.tanh(self.embedding)
        self.p={'wo':np.zeros((8,2)),'bo':np.zeros(2)}

    def forward(self,features):
        fraction=np.tanh(self.embedding@self.p['wo']+self.p['bo'])
        delta=np.zeros_like(self.decoder.positions); delta[self.active]=decode_patch(fraction,self.buffers)
        direction=self.decoder.ray_base_direction+delta[self.decoder.owner_edges[:,1]]-delta[self.decoder.owner_edges[:,0]]
        phase=self.decoder.ray_phase(direction)[0]
        offsets=np.mod(phase-self.decoder.ray_initial_phase+.5,1.)-.5
        return np.concatenate([delta,offsets]),None

    def save(self,path,metadata):
        import json
        np.savez_compressed(path,**self.p,active=self.active,embedding=self.embedding,w1=self.w1,w2=self.w2,
            mean=self.mean,scale=self.scale,**self.buffers,metadata=np.array(json.dumps(metadata)))
