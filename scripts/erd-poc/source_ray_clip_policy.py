"""Shared NN moves one owner; original rays exit its translated rectangles.

The zero head preserves every source position and port. Original endpoint
anchors remain inside the translated effective rectangles, so the analytic
new endpoints shorten original segments. Native only observes and validates;
no measured future event, coordinate search or rejection repair is used.
"""
import json
import numpy as np
from compact_patch_neural_policy import patch_buffers
from joint_neural_ports import perimeter_phase


def containment_bounds(decoder, active, max_step):
    p = decoder.provider
    lower,upper = [],[]
    for node in active:
        bounds = patch_buffers(decoder.positions,decoder.sizes,[int(node)],max_step)
        low,high = bounds['dag_lower'][0]/100,bounds['dag_upper'][0]/100
        incident = p.owner_edges==node
        assert incident.any()
        boundary,half = p.base_boundary[incident],p.endpoint_sizes[incident]/2
        contain_low = np.minimum(0.,np.max(boundary-half,axis=0))
        contain_high = np.maximum(0.,np.min(boundary+half,axis=0))
        low = np.maximum(low,np.ceil(contain_low*100-1e-7)/100)
        high = np.minimum(high,np.floor(contain_high*100+1e-7)/100)
        assert (low<=0).all() and (high>=0).all()
        lower.append(low)
        upper.append(high)
    return np.array(lower),np.array(upper)


class SourceRayClipPolicy:
    def __init__(self, features, decoder, active, max_step, seed):
        self.decoder = decoder
        self.active = np.asarray(active,dtype=np.int32)
        assert 1<=len(self.active)<=1244 and len(set(self.active.tolist()))==len(self.active)
        assert 0<max_step<=2048
        lower,upper = containment_bounds(decoder,self.active,max_step)
        p = decoder.provider
        direction = p.original_ports[:,::-1]-p.original_ports
        assert (np.linalg.norm(direction,axis=2)>1e-9).all()
        self.buffers = dict(lower=lower,upper=upper,max_step=np.array(max_step),
            source_direction=direction,source_boundary=p.base_boundary.copy(),
            source_phase=p.phase.copy(),endpoint_sizes=p.endpoint_sizes.copy(),
            owner_edges=p.owner_edges.copy())
        features = np.asarray(features,dtype=np.float32)
        assert features.shape==(len(decoder.positions),64)
        self.mean = features.mean(0,dtype=np.float64)
        self.scale = np.maximum(.2,features.std(0,dtype=np.float64))
        inputs = np.concatenate([np.clip((features[self.active]-self.mean)/self.scale,-8,8),
            lower/max_step,upper/max_step,np.ones((len(self.active),1))],axis=1)
        rng = np.random.default_rng(seed)
        self.w1 = rng.normal(0,1/np.sqrt(69),(69,12))
        self.w2 = rng.normal(0,1/np.sqrt(12),(12,8))
        raw = np.tanh(np.tanh(inputs@self.w1)@self.w2)
        norm = np.linalg.norm(raw,axis=1)
        assert (norm>1e-12).all()
        # A shared query can select any distinct source embedding prototype.
        self.embedding = raw/norm[:,None]
        self.p = dict(wo=np.zeros((8,3)),bo=np.zeros(3))

    def forward(self, features):
        logits = self.embedding@self.p['wo']+self.p['bo']
        selected = int(np.argmax(logits[:,0]))
        node = int(self.active[selected])
        fraction = np.tanh(logits[selected,1:3])
        low,high = self.buffers['lower'][selected],self.buffers['upper'][selected]
        raw = fraction*np.where(fraction>=0,high,-low)
        delta = np.zeros_like(self.decoder.positions)
        delta[node] = np.copysign(np.floor(abs(raw)*100+.5),raw)/100
        offsets = np.zeros_like(self.buffers['source_phase'])
        moved = np.any(delta[self.buffers['owner_edges']]!=0,axis=2)
        if moved.any():
            local = self.buffers['source_boundary']-delta[self.buffers['owner_edges']]
            direction = self.buffers['source_direction']
            nonzero = abs(direction)>1e-12
            denominator = np.where(nonzero,direction,1.)
            faces = np.sign(direction)*self.buffers['endpoint_sizes']/2
            parameter = np.min(np.where(nonzero,(faces-local)/denominator,np.inf),axis=2)
            assert np.isfinite(parameter[moved]).all() and (parameter[moved]>=-1e-8).all()
            assert (parameter[moved]<1).all(),'translated rectangle covers a peer endpoint'
            boundary = local+parameter[:,:,None]*direction
            phase = perimeter_phase(boundary,self.buffers['endpoint_sizes'])
            changed = np.mod(phase-self.buffers['source_phase']+.5,1.)-.5
            offsets[moved] = changed[moved]
        return np.concatenate([delta,offsets]),dict(selectedOwner=node,selectedVocabularyIndex=selected,
            movedOwners=int(np.any(delta!=0,axis=1).sum()),maximumBodyDisplacementPixels=float(abs(delta).max()),
            changedEndpointPhases=int(np.count_nonzero(offsets)),
            maximumPhaseOffset=float(abs(offsets).max()),sourceAnchorsInsideTranslatedRectangles=True,
            originalRaysAnalyticallyShortened=True,noWholeComponentPortRemapping=True,
            sourceContainmentCapacityPixels=float(np.maximum(abs(low),abs(high)).max()))

    def save(self, path, metadata):
        np.savez_compressed(path,**self.p,active=self.active,embedding=self.embedding,
            w1=self.w1,w2=self.w2,mean=self.mean,scale=self.scale,**self.buffers,
            metadata=np.array(json.dumps(metadata)))
