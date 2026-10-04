"""Source-preserving internal anchors coupled to learned card translations.

Fixed anchors come from the original boundary rays, halfway through each card,
with a small source-independent inset. Only shared neural weights are trained.
The anchors and initial phases are immutable decoder buffers. No endpoint head,
objective search, or repair is used to turn a node output into boundary phases.
"""
import numpy as np
from joint_neural_ports import perimeter_phase,perimeter_points
from joint_separation_policy import SeparationDagPolicy


def interior_anchors(boundary,sizes,direction,inset=1e-4):
    """Halfway along the inward ray, then a strict convex inset from its face."""
    unit=direction/np.linalg.norm(direction,axis=1)[:,None]
    inward=unit[:,None,:]*np.array([-1.,1.])[None,:,None]
    half=sizes/2
    distance=np.divide(np.where(inward>=0,half-boundary,-half-boundary),inward,
        out=np.full_like(inward,np.inf),where=abs(inward)>1e-12)
    reach=np.maximum(0.,distance.min(2))
    anchors=(boundary+.5*reach[:,:,None]*inward)*(1-inset)
    assert np.isfinite(anchors).all() and (abs(anchors)<half).all()
    return anchors,reach


class InteriorAnchorEndpoints:
    anchor_buffers=('anchor_rays','owner_edges','anchor_offsets',
        'ray_base_direction','ray_sizes','ray_initial_phase','anchor_inset','degenerate_source_rays',
        'endpoint_groups','endpoint_counts')

    def initialize_anchors(self,positions,sizes,provider):
        assert (abs(provider.offsets)+provider.sizes/2<=sizes[provider.owner]/2+1e-7).all(), 'full cards escape physical owners'
        self.anchor_rays=np.array(1,dtype=np.int32)
        self.anchor_inset=np.array(.01)
        self.owner_edges=provider.owner_edges.copy()
        groups={};indices=[]
        for owner,point in zip(provider.full_edges.ravel(),provider.original_ports.reshape(-1,2)):
            indices.append(groups.setdefault((int(owner),*point),len(groups)))
        self.endpoint_groups=np.array(indices,dtype=np.int32)
        self.endpoint_counts=np.bincount(self.endpoint_groups)
        full=positions[provider.owner]+provider.offsets
        direction=full[provider.full_edges[:,1]]+provider.base_boundary[:,1]-full[provider.full_edges[:,0]]-provider.base_boundary[:,0]
        assert (np.linalg.norm(direction,axis=1)>1e-9).all()
        self.ray_sizes=provider.endpoint_sizes.copy()
        self.anchor_offsets,reach=interior_anchors(provider.base_boundary,self.ray_sizes,direction,float(self.anchor_inset))
        self.degenerate_source_rays=np.array(np.count_nonzero(reach<=1e-6),dtype=np.int32)
        self.ray_base_direction=full[provider.full_edges[:,1]]+self.anchor_offsets[:,1]-full[provider.full_edges[:,0]]-self.anchor_offsets[:,0]
        self.ray_initial_phase=self.ray_phase(self.ray_base_direction)[0]

    def pool(self,values):
        means=np.bincount(self.endpoint_groups,weights=values.ravel())/self.endpoint_counts
        return means[self.endpoint_groups].reshape(values.shape)

    def ray_phase(self,direction):
        signs=np.array([1.,-1.])[None,:]
        safe=np.maximum(abs(direction),1e-12)
        ratios=(self.ray_sizes/2-signs[:,:,None]*np.sign(direction)[:,None,:]*self.anchor_offsets)/safe[:,None,:]
        axes=np.argmin(ratios,axis=2);row=np.arange(len(direction))[:,None];endpoint=np.arange(2)[None,:]
        parameter=ratios[row,endpoint,axes]
        offset=self.anchor_offsets+signs[:,:,None]*parameter[:,:,None]*direction[:,None,:]
        phase=perimeter_phase(offset,self.ray_sizes)
        perimeter=2*self.ray_sizes.sum(2)
        tangent=perimeter_points(phase,self.ray_sizes)[1]/perimeter[:,:,None]**2
        derivative_parameter=np.zeros_like(offset);denominator=direction[row,axes]
        derivative_parameter[row,endpoint,axes]=np.divide(-parameter,denominator,
            out=np.zeros_like(parameter),where=abs(denominator)>1e-12)
        derivative=signs[:,:,None]*(parameter[:,:,None]*tangent+
            derivative_parameter*np.sum(tangent*direction[:,None,:],axis=2)[:,:,None])
        assert np.isfinite(phase).all() and np.isfinite(derivative).all()
        return phase,derivative

    def endpoint_offsets(self,nodes):
        direction=self.ray_base_direction+nodes[self.owner_edges[:,1]]-nodes[self.owner_edges[:,0]]
        phase,derivative=self.ray_phase(direction)
        return self.pool(np.mod(phase-self.ray_initial_phase+.5,1.)-.5),derivative


class AnchorRayPolicy(InteriorAnchorEndpoints,SeparationDagPolicy):
    buffers=SeparationDagPolicy.buffers+InteriorAnchorEndpoints.anchor_buffers

    def __init__(self,features,positions,sizes,max_step,seed,provider,port_span):
        super().__init__(features,positions,sizes,max_step,seed)
        self.initialize_anchors(positions,sizes,provider)

    def forward(self,features,quantized=True):
        nodes,base=super().forward(features,quantized)
        offsets,derivative=self.endpoint_offsets(nodes)
        return np.concatenate([nodes,offsets]),(base,derivative)

    def backward(self,cache,gradient,port_gradient):
        base,derivative=cache
        direction_gradient=np.sum(self.pool(port_gradient)[:,:,None]*derivative,axis=1)
        coupled=gradient.copy()
        np.add.at(coupled,self.owner_edges[:,0],-direction_gradient)
        np.add.at(coupled,self.owner_edges[:,1],direction_gradient)
        return super().backward(base,coupled)
