"""Learned temperature mixtures for event deltas; no future exact predicates."""
import json
from pathlib import Path
import numpy as np
from geometry_world_model import cross_eligible, cross_features, hit_eligible, hit_features, load as load_events, digest
from learn_card_policy import sigmoid

TEMPERATURES=np.array([.01,.03,.1,.3,1.,2.,4.])
SCHEMA='learned-event-temperature-mixture-v1'


def softmax(x):
    e=np.exp(x-x.max(axis=-1,keepdims=True)); return e/e.sum(axis=-1,keepdims=True)


class CurveProxy:
    def __init__(self,models): self.models=models

    def crosses(self,lines,left,right):
        total=np.zeros(len(TEMPERATURES))
        for first in range(0,len(left),2048):
            a,b=lines[left[first:first+2048]],lines[right[first:first+2048]]
            keep=cross_eligible(a,b)
            if keep.any():
                z=self.models['cross'].forward(cross_features(a[keep],b[keep]))[0]
                total+=sigmoid(z[:,None]/TEMPERATURES).sum(0)
        return total

    def hits(self,lines,positions,sizes,edges,line_ids,node_ids):
        total=np.zeros(len(TEMPERATURES))
        for first in range(0,len(line_ids),2048):
            a,b=line_ids[first:first+2048],node_ids[first:first+2048]
            keep=np.all(edges[a]!=b[:,None],axis=1)&hit_eligible(lines[a],positions[b],sizes[b])
            if keep.any():
                z=self.models['hit'].forward(hit_features(lines[a[keep]],positions[b[keep]],sizes[b[keep]]))[0]
                total+=sigmoid(z[:,None]/TEMPERATURES).sum(0)
        return total

    def spectrum(self,before,after,sizes,edges):
        p,a=before; q,b=after
        changed_edges=np.flatnonzero(np.any(a!=b,axis=(1,2))).astype(np.int32)
        changed_nodes=np.flatnonzero(np.any(p!=q,axis=1)).astype(np.int32)
        assert len(changed_nodes)<=16
        changed=np.zeros(len(a),dtype=bool); changed[changed_edges]=True
        left=np.repeat(changed_edges,len(a)); right=np.tile(np.arange(len(a),dtype=np.int32),len(changed_edges))
        keep=(left!=right)&((~changed[right])|(left<right)); left,right=left[keep],right[keep]
        cross=self.crosses(b,left,right)-self.crosses(a,left,right)
        line_ids=np.r_[np.repeat(changed_edges,len(sizes)),np.repeat(np.flatnonzero(~changed).astype(np.int32),len(changed_nodes))]
        node_ids=np.r_[np.tile(np.arange(len(sizes),dtype=np.int32),len(changed_edges)),np.tile(changed_nodes,np.count_nonzero(~changed))]
        hit=self.hits(b,q,sizes,edges,line_ids,node_ids)-self.hits(a,p,sizes,edges,line_ids,node_ids)
        return np.r_[cross,hit].astype(np.float32)


class Mixture:
    def __init__(self): self.theta=np.zeros((2,len(TEMPERATURES)))

    def forward(self,x):
        x=np.asarray(x,dtype=np.float32).reshape(-1,2,len(TEMPERATURES))
        return np.sum(x*softmax(self.theta)[None],axis=(1,2))

    def loss(self,x,y,gradients=False):
        x=np.asarray(x,dtype=np.float32).reshape(-1,2,len(TEMPERATURES)); weights=softmax(self.theta)
        by_head=np.sum(x*weights[None],axis=2); residual=by_head.sum(1)-y
        loss=float(np.mean(residual**2))
        if not gradients: return loss
        gradient=np.mean(2*residual[:,None,None]*weights[None]*(x-by_head[:,:,None]),axis=0)
        return loss,gradient


def load(path):
    with np.load(path,allow_pickle=False) as saved:
        metadata=json.loads(str(saved['metadata'])); assert metadata['schema']==SCHEMA and metadata['updatesExecuted']>0
        model=Mixture(); model.theta=saved['theta'].copy()
    assert np.isfinite(model.theta).all()
    events,event_metadata=load_events(Path(metadata['eventModel']))
    assert digest(metadata['eventModel'])==metadata['eventModelSha256']
    return CurveProxy(events),model,metadata
