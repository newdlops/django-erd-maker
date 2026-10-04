"""Train source-slot permutations through a finite Sinkhorn relaxation.

Only shared MLP weights are trained. Forward inference retains exact hard slot
sorting and anchor endpoints. The separate training forward subtracts a fixed
source relaxation, preserving zero outputs, and differentiates every finite
normalization. This is not a straight-through derivative of hard sorting.
Finite iteration and hard/soft disagreement remain explicit limitations.

Continuous permutation relaxation reference (not a reproduction of its noisy
model): Mena et al., https://arxiv.org/abs/1802.08665.
"""
import numpy as np
from joint_slot_policy import SlotPermutationPolicy
from joint_slot_anchor_policy import SlotAnchorRayPolicy

SINKHORN_PASSES=8
MAX_PAIR_ENTRIES=262144


def relaxed_assignment(score, reference, temperature, keep_cache=True):
    """Row-normalized finite log-space Sinkhorn; reference ranks are fixed."""
    assert len(score)>1 and .05<=temperature<=4 and np.isfinite(score).all()
    centered=score-score.mean();target=reference-reference.mean()
    deviation=np.sqrt(np.mean(centered*centered)+1e-12)
    scale=np.sqrt(np.mean(target*target)+1e-12)/deviation
    normalized=centered*scale
    step=2./(len(score)-1)
    difference=(normalized[:,None]-target[None,:])/step
    logits=-.5*(difference/temperature)**2
    normalizations=[]
    for axis in [1,0]*SINKHORN_PASSES+[1]:
        maximum=logits.max(axis=axis,keepdims=True)
        exponent=np.exp(logits-maximum)
        total=exponent.sum(axis=axis,keepdims=True)
        logits=logits-maximum-np.log(total)
        if keep_cache:normalizations.append((axis,exponent/total))
    probability=np.exp(logits)
    cache=(centered,deviation,scale,difference,step,temperature,normalizations,probability) if keep_cache else None
    return probability,cache


def assignment_backward(gradient, cache):
    centered,deviation,scale,difference,step,temperature,normalizations,probability=cache
    derivative=gradient*probability
    for axis,weights in normalizations[::-1]:
        derivative-=weights*derivative.sum(axis=axis,keepdims=True)
    score_gradient=np.sum(derivative*(-difference/(step*temperature**2)),axis=1)
    return scale*(score_gradient-score_gradient.mean()
        -centered*np.mean(score_gradient*centered)/deviation**2)


class RelaxedSlotAnchorPolicy(SlotAnchorRayPolicy):
    buffers=SlotAnchorRayPolicy.buffers+('relaxed_slots',)

    def __init__(self,features,positions,sizes,max_step,seed,provider,port_span):
        super().__init__(features,positions,sizes,max_step,seed,provider,port_span)
        self.relaxed_slots=np.array(1,dtype=np.int32)
        assert np.sum(np.diff(self.group_bounds).astype(np.int64)**2)<=MAX_PAIR_ENTRIES

    def forward_relaxed(self,features,temperature):
        # The hard forward is also the single source of the network's logits.
        _,network=SlotPermutationPolicy.forward(self,features)
        score=network[3];nodes=np.zeros_like(self.slot_origin);groups=[]
        mass_error=correction_size=0.
        assert np.sum(np.diff(self.group_bounds).astype(np.int64)**2)<=MAX_PAIR_ENTRIES
        for begin,end in zip(self.group_bounds[:-1],self.group_bounds[1:]):
            members=self.slot_order[begin:end]
            if len(members)<2:continue
            reference=self.rank_origin[members]
            # Center fixed slot positions for translation-invariant numerics.
            slots=self.slot_origin[members]-self.slot_origin[members].mean(0)
            probability,cache=relaxed_assignment(score[members],reference,temperature)
            baseline,_=relaxed_assignment(reference,reference,temperature,keep_cache=False)
            nodes[members]=(probability-baseline)@slots
            groups.append((members,slots,cache))
            mass_error=max(mass_error,float(abs(probability.sum(0)-1).max()))
            correction_size=max(correction_size,float(abs(baseline@slots-slots).max()))
        offsets,derivative=self.endpoint_offsets(nodes)
        self.relaxation_report={'slotTemperature':float(temperature),'sinkhornPasses':SINKHORN_PASSES,
            'maximumColumnMassError':mass_error,'sourceCorrectionMaximumAbs':correction_size,
            'trainingCoordinatesAreRelaxed':True,'hardSortingGradientUsed':False}
        return np.concatenate([nodes,offsets]),('relaxed-slots',network,groups,derivative)

    def backward(self,cache,gradient,port_gradient):
        assert cache[0]=='relaxed-slots', 'backward requires the relaxed training forward'
        _,network,groups,derivative=cache
        direction_gradient=np.sum(self.pool(port_gradient)[:,:,None]*derivative,axis=1)
        coupled=gradient.copy()
        np.add.at(coupled,self.owner_edges[:,0],-direction_gradient)
        np.add.at(coupled,self.owner_edges[:,1],direction_gradient)
        score_gradient=np.zeros(len(gradient))
        for members,slots,assignment in groups:
            score_gradient[members]=assignment_backward(coupled[members]@slots.T,assignment)
        x,h1,h2,_=network;dy=score_gradient[:,None]
        result={'wo':h2.T@dy,'bo':dy.sum(0)}
        dh2=(dy@self.p['wo'].T)*(1-h2*h2)
        result.update(w2=h1.T@dh2,b2=dh2.sum(0))
        dh1=(dh2@self.p['w2'].T)*(1-h1*h1)
        result.update(w1=x.T@dh1,b1=dh1.sum(0))
        return result
