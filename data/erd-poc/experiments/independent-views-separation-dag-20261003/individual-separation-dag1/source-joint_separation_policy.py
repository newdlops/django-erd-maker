"""Neural output layer with source-order separation constraints.

One already separating axis is retained for every physical-card pair. Its
source order forms a DAG. Neural fractions jointly decode all translations
inside that DAG and the source frame. No objective is consulted by decoding.
Cent quantization uses floor(x + .5); its training gradient is straight-through.
"""
import numpy as np
from joint_neural_ports import JointPortPolicy, RayConditionedPolicy

DAG_BUFFERS=('dag_lower','dag_upper',*(f'dag_{axis}_{name}' for axis in ('x','y')
              for name in ('order','ptr','parents','lags')))


def separation_graph(positions,sizes,max_step):
    count=len(positions);pairs=count*(count-1)//2
    source=np.empty(pairs,dtype=np.int32);target=np.empty(pairs,dtype=np.int32)
    axes=np.empty(pairs,dtype=np.int8);lags=np.empty(pairs,dtype=np.int64)
    at=0
    for node in range(count-1):
        other=np.arange(node+1,count,dtype=np.int32)
        delta=positions[other]-positions[node]
        room=abs(delta)-(sizes[other]+sizes[node])/2-[55.99,41.99]
        assert ((room[:,0]>=0)|(room[:,1]>=0)).all(), 'source card spacing is invalid'
        use_x=(room[:,0]>=0)&((room[:,1]<0)|(np.floor(room[:,0]*1e6+.5)>=np.floor(room[:,1]*1e6+.5)))
        axis=np.where(use_x,0,1);row=np.arange(len(other));forward=delta[row,axis]>0
        section=slice(at,at+len(other));source[section]=np.where(forward,node,other)
        target[section]=np.where(forward,other,node);axes[section]=axis
        lags[section]=np.ceil(-np.maximum(0,room[row,axis]-1e-7)*100).astype(np.int64)
        at+=len(other)
    assert at==pairs and (lags<=0).all()
    low=(positions-sizes/2).min(0);high=(positions+sizes/2).max(0)
    lower=np.maximum(-max_step,low-positions+sizes/2)
    upper=np.minimum(max_step,high-positions-sizes/2)
    lower=np.ceil(np.minimum(0,lower+1e-7)*100).astype(np.int64)
    upper=np.floor(np.maximum(0,upper-1e-7)*100).astype(np.int64)
    # Distant pairs already separated for every point in their frame bounds
    # need no stored edge. This removes only redundant inequalities.
    necessary=lags>lower[target,axes]-upper[source,axes]
    result={'dag_lower':lower,'dag_upper':upper}
    for axis,name in enumerate(('x','y')):
        mask=(axes==axis)&necessary;parents=source[mask];children=target[mask];weights=lags[mask]
        permutation=np.lexsort((parents,children));parents=parents[permutation];weights=weights[permutation]
        ptr=np.r_[0,np.cumsum(np.bincount(children,minlength=count))].astype(np.int32)
        order=np.argsort(positions[:,axis],kind='stable').astype(np.int32)
        # A backwards pass reserves enough room for every downstream output.
        for node in order[::-1]:
            begin,end=ptr[node:node+2]
            np.minimum.at(upper[:,axis],parents[begin:end],upper[node,axis]-weights[begin:end])
        assert (upper[:,axis]>=0).all()
        for key,value in [('order',order),('ptr',ptr),('parents',parents),('lags',weights)]:
            result[f'dag_{name}_{key}']=value
    return result


def decode_separation(fractions,buffers,quantized=True):
    assert np.isfinite(fractions).all() and (abs(fractions)<=1).all()
    count=len(fractions);decoded=np.zeros_like(fractions);ticks=np.zeros(fractions.shape,dtype=np.int64)
    derivative=np.zeros_like(fractions);parent_weight=np.zeros_like(fractions)
    active_parent=np.full(fractions.shape,-1,dtype=np.int32)
    for axis,name in enumerate(('x','y')):
        order,ptr,parents,lags=(buffers[f'dag_{name}_{key}'] for key in ('order','ptr','parents','lags'))
        for node in order:
            lower=float(buffers['dag_lower'][node,axis]);upper=float(buffers['dag_upper'][node,axis])
            if not quantized:lower/=100;upper/=100
            begin,end=ptr[node:node+2]
            if begin<end:
                values=(ticks[parents[begin:end],axis]+lags[begin:end] if quantized
                        else decoded[parents[begin:end],axis]+lags[begin:end]/100)
                selected=int(np.argmax(values));maximum=values[selected]
                if maximum>lower:lower=maximum;active_parent[node,axis]=parents[begin+selected]
            assert lower<=upper+1e-8, 'infeasible DAG prefix'
            anchor=max(0.,lower);value=fractions[node,axis]
            room=upper-anchor if value>=0 else anchor-lower
            if quantized:
                ticks[node,axis]=int(np.floor(anchor+value*room+.5))
                decoded[node,axis]=ticks[node,axis]/100;derivative[node,axis]=room/100
            else:
                decoded[node,axis]=anchor+value*room;derivative[node,axis]=room
            parent_weight[node,axis]=(1-value if value>=0 else 1.) if lower>0 else (0. if value>=0 else -value)
    cache=(derivative,parent_weight,active_parent)
    # Each node is decoded directly to integer cents before downstream nodes.
    # This avoids floating half-tie drift across a tight pair inequality.
    return decoded,cache


def backward_separation(gradient,cache,buffers):
    derivative,parent_weight,active_parent=cache;upstream=gradient.copy();result=np.zeros_like(gradient)
    for axis,name in enumerate(('x','y')):
        for node in buffers[f'dag_{name}_order'][::-1]:
            result[node,axis]=upstream[node,axis]*derivative[node,axis]
            parent=active_parent[node,axis]
            if parent>=0:upstream[parent,axis]+=upstream[node,axis]*parent_weight[node,axis]
    return result


class SeparationDagPortPolicy(RayConditionedPolicy):
    buffers=RayConditionedPolicy.buffers+('separation_dag',)+DAG_BUFFERS

    def __init__(self,features,positions,sizes,max_step,seed,provider,port_span):
        super().__init__(features,positions,sizes,max_step,seed,provider,port_span)
        # Physical separation implies full-card separation only if members
        # remain inside their owner's rectangle. Verify that source contract.
        assert (abs(provider.offsets)+provider.sizes/2<=sizes[provider.owner]/2+1e-7).all(), 'full cards escape physical owners'
        self.separation_dag=np.array(1,dtype=np.int32)
        for key,value in separation_graph(positions,sizes,max_step).items():setattr(self,key,value)
        self.negative=np.ones_like(self.negative);self.positive=np.ones_like(self.positive)

    def graph_buffers(self):
        return {key:getattr(self,key) for key in DAG_BUFFERS}

    def forward(self,features,quantized=True):
        actions,base=JointPortPolicy.forward(self,features)
        nodes,dag=decode_separation(actions[:len(features)],self.graph_buffers(),quantized)
        actions[:len(features)]=nodes
        direction=self.ray_base_direction+nodes[self.owner_edges[:,1]]-nodes[self.owner_edges[:,0]]
        phase,derivative=self.ray_phase(direction)
        actions[len(features):]=np.mod(actions[len(features):]+phase-self.ray_initial_phase+.5,1.)-.5
        return actions,(base,dag,derivative)

    def backward(self,cache,gradient,port_gradient):
        base,dag,derivative=cache
        direction_gradient=np.sum(port_gradient[:,:,None]*derivative,axis=1)
        coupled=gradient.copy()
        np.add.at(coupled,self.owner_edges[:,0],-direction_gradient)
        np.add.at(coupled,self.owner_edges[:,1],direction_gradient)
        return JointPortPolicy.backward(self,base,backward_separation(coupled,dag,self.graph_buffers()),port_gradient)
