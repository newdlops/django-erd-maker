"""Small NumPy policy and differentiable losses for coordinated neural moves.

The losses train network weights. They never directly update card coordinates;
only a frozen network forward pass supplies a candidate to the exact validator.
"""
import numpy as np


def cross(a, b):
    return a[..., 0]*b[..., 1]-a[..., 1]*b[..., 0]


def absolute_distance(point, a, b):
    direction, relative = b-a, point-a
    length = np.sqrt((direction*direction).sum(1)+1e-12)
    signed = cross(direction, relative)
    value = np.abs(signed)/length
    point_gradient = np.sign(signed)[:, None]*np.stack([-direction[:, 1], direction[:, 0]], 1)/length[:, None]
    b_gradient = np.sign(signed)[:, None]*np.stack([relative[:, 1], -relative[:, 0]], 1)/length[:, None]
    b_gradient -= value[:, None]*direction/(length*length)[:, None]
    return value, point_gradient, -point_gradient-b_gradient, b_gradient


def signed_distance(point, a, b):
    direction,relative=b-a,point-a
    length2=(direction*direction).sum(1)+1e-12;length=np.sqrt(length2)
    value=cross(direction,relative)/length
    gp=np.stack([-direction[:,1],direction[:,0]],1)/length[:,None]
    gb=np.stack([relative[:,1],-relative[:,0]],1)/length[:,None]-value[:,None]*direction/length2[:,None]
    return value,gp,-gp-gb,gb


def sigmoid(value):
    return 1/(1+np.exp(-np.clip(value,-60,60)))


def clipped_ports(positions,sizes,edges):
    source,target=positions[edges[:,0]],positions[edges[:,1]]
    direction=target-source
    safe=np.maximum(np.abs(direction),1e-12)
    parameters=[];derivatives=[]
    for endpoint in range(2):
        ratios=sizes[edges[:,endpoint]]/2/safe
        axis=np.argmin(ratios,1);row=np.arange(len(edges));value=ratios[row,axis]
        derivative=np.zeros_like(direction)
        derivative[row,axis]=-value/direction[row,axis]
        parameters.append(value);derivatives.append(derivative)
    points=np.stack([source+parameters[0][:,None]*direction,target-parameters[1][:,None]*direction],1)
    return points,(direction,parameters,derivatives)


def propagate_port_gradients(gradient,route_gradient,edges,cache):
    direction,parameters,derivatives=cache
    source_g,target_g=route_gradient[:,0],route_gradient[:,1]
    to_target=parameters[0][:,None]*source_g+derivatives[0]*(source_g*direction).sum(1)[:,None]
    to_source=parameters[1][:,None]*target_g+derivatives[1]*(target_g*direction).sum(1)[:,None]
    np.add.at(gradient,edges[:,0],source_g-to_target+to_source)
    np.add.at(gradient,edges[:,1],target_g-to_source+to_target)


class LayoutProxy:
    def __init__(self, positions, sizes, edges, max_step, spacing_weight=20.):
        self.origin, self.sizes, self.edges = positions, sizes, edges
        self.spacing_weight = spacing_weight
        count = len(positions)
        pair_rows, hit_rows, near_rows = [], [], []
        starts, ends = positions[edges[:, 0]], positions[edges[:, 1]]
        low, high = np.minimum(starts, ends)-max_step, np.maximum(starts, ends)+max_step
        card_low, card_high = positions-sizes/2-10-max_step, positions+sizes/2+10+max_step
        edge_ids, node_ids = np.arange(len(edges)), np.arange(count)
        for i, (source, target) in enumerate(edges):
            mask = ((low[i] <= high).all(1) & (low <= high[i]).all(1) & (edge_ids > i)
                    & (edges[:, 0] != source) & (edges[:, 0] != target)
                    & (edges[:, 1] != source) & (edges[:, 1] != target))
            pair_rows.extend((i, int(j)) for j in np.flatnonzero(mask))
            mask = ((low[i] <= card_high).all(1) & (card_low <= high[i]).all(1)
                    & (node_ids != source) & (node_ids != target))
            hit_rows.extend((i, int(j)) for j in np.flatnonzero(mask))
        for i in range(count):
            reach=(sizes[i]+sizes)/2+np.array([58., 44.])+2*max_step
            mask=(np.abs(positions-positions[i]) <= reach).all(1) & (node_ids > i)
            near_rows.extend((i, int(j)) for j in np.flatnonzero(mask))
        self.cross_pairs=np.array(pair_rows, dtype=np.int32).reshape(-1, 2)
        self.hit_pairs=np.array(hit_rows, dtype=np.int32).reshape(-1, 2)
        self.near_pairs=np.array(near_rows, dtype=np.int32).reshape(-1, 2)

    def loss(self, positions):
        gradient=np.zeros_like(positions); values={}
        ports,port_cache=clipped_ports(positions,self.sizes,self.edges)
        route_gradient=np.zeros_like(ports);points=ports.reshape(-1,2);point_gradient=route_gradient.reshape(-1,2)
        cp=self.cross_pairs
        crossing_loss=0.
        if len(cp):
            endpoints=np.stack([2*cp[:,0],2*cp[:,0]+1,2*cp[:,1],2*cp[:,1]+1],1)
            groups=[(0,1,2,3),(2,3,0,1)];margins=[];derivatives=[]
            for p,q,a,b in groups:
                u=signed_distance(points[endpoints[:,p]],points[endpoints[:,a]],points[endpoints[:,b]])[0]
                v=signed_distance(points[endpoints[:,q]],points[endpoints[:,a]],points[endpoints[:,b]])[0]
                denominator=np.abs(u)+np.abs(v)+1e-6
                margins.append(-u*v/denominator)
                derivatives.append((-v/denominator+u*v*np.sign(u)/denominator**2,
                                    -u/denominator+u*v*np.sign(v)/denominator**2))
            selected=np.argmin(np.stack(margins,1),1)
            margin=np.minimum(*margins);probability=sigmoid(margin/4.)
            crossing_loss=probability.sum();weight=probability*(1-probability)/4.
            for k,(p,q,a,b) in enumerate(groups):
                mask=selected==k;rows=endpoints[mask]
                if not len(rows):continue
                for node,multiplier in [(p,derivatives[k][0]),(q,derivatives[k][1])]:
                    _,gp,ga,gb=signed_distance(points[rows[:,node]],points[rows[:,a]],points[rows[:,b]])
                    factor=(weight[mask]*multiplier[mask])[:,None]
                    for index,g in [(node,gp),(a,ga),(b,gb)]:np.add.at(point_gradient,rows[:,index],g*factor)
        values['softCrossingCount']=float(crossing_loss)
        hp=self.hit_pairs
        node_loss=0.
        if len(hp):
            point=positions[hp[:,1]];a=ports[hp[:,0],0];b=ports[hp[:,0],1]
            direction,relative=b-a,point-a
            length2=(direction*direction).sum(1)+1e-12; length=np.sqrt(length2)
            along=(direction*relative).sum(1)/length2
            signed=cross(direction,relative)
            half=self.sizes[hp[:,1]]/2+10
            support=half[:,0]*np.abs(direction[:,1])+half[:,1]*np.abs(direction[:,0])
            penetration=(support-np.abs(signed))/length
            active=(along>0)&(along<1)
            if active.any():
                rows=hp[active]
                direction,relative,length,length2,signed,half,penetration=(v[active] for v in [direction,relative,length,length2,signed,half,penetration])
                gp=-np.sign(signed)[:,None]*np.stack([-direction[:,1],direction[:,0]],1)/length[:,None]
                gb=(np.stack([half[:,1]*np.sign(direction[:,0]),half[:,0]*np.sign(direction[:,1])],1)
                    -np.sign(signed)[:,None]*np.stack([relative[:,1],-relative[:,0]],1))/length[:,None]
                gb-=penetration[:,None]*direction/length2[:,None]
                ga=-gp-gb
                probability=sigmoid(penetration/2.);weight=(probability*(1-probability)/2.)[:,None]
                node_loss=probability.sum()
                np.add.at(gradient,rows[:,1],gp*weight)
                np.add.at(route_gradient[:,0],rows[:,0],ga*weight)
                np.add.at(route_gradient[:,1],rows[:,0],gb*weight)
        values['softCardHitCount']=float(node_loss)
        propagate_port_gradients(gradient,route_gradient,self.edges,port_cache)
        pairs=self.near_pairs
        spacing_loss=0.
        if len(pairs):
            diff=positions[pairs[:,0]]-positions[pairs[:,1]]
            overlap=(self.sizes[pairs[:,0]]+self.sizes[pairs[:,1]])/2+np.array([58.,44.])-np.abs(diff)
            active=(overlap>0).all(1)
            pairs,diff,overlap=pairs[active],diff[active],overlap[active]
            if len(pairs):
                axis=np.argmin(overlap,1);row=np.arange(len(pairs));depth=overlap[row,axis]
                spacing_loss=self.spacing_weight*np.sum((depth/8.)**2)
                g=np.zeros_like(diff);g[row,axis]=-self.spacing_weight*depth/32.*np.sign(diff[row,axis])
                np.add.at(gradient,pairs[:,0],g);np.add.at(gradient,pairs[:,1],-g)
        values['spacingPenalty']=float(spacing_loss)
        displacement=positions-self.origin
        regularization=1e-5*np.sum((displacement/128.)**2)
        gradient+=2e-5*displacement/128.**2
        values['movementPenalty']=float(regularization)
        values['total']=sum(values.values())
        return values,gradient


class JointPolicy:
    keys=('w1','b1','w2','b2','wo','bo')

    def __init__(self, features, positions, sizes, max_step, seed):
        rng=np.random.default_rng(seed)
        self.mean=features.mean(0);self.scale=np.maximum(.2,features.std(0))
        low=(positions-sizes/2).min(0);high=(positions+sizes/2).max(0)
        # Fixed frame limits are a decoder constraint, never candidate search.
        self.negative=np.floor(np.maximum(0,np.minimum(max_step,positions-sizes/2-low))*100+1e-7)/100
        self.positive=np.floor(np.maximum(0,np.minimum(max_step,high-positions-sizes/2))*100+1e-7)/100
        self.p={'w1':rng.normal(0,1/np.sqrt(features.shape[1]),(features.shape[1],64)),
                'b1':np.zeros(64),'w2':rng.normal(0,1/8,(64,32)),'b2':np.zeros(32),
                'wo':np.zeros((32,2)),'bo':np.zeros(2)}

    def forward(self,features):
        x=(features-self.mean)/self.scale
        h1=np.tanh(x@self.p['w1']+self.p['b1'])
        h2=np.tanh(h1@self.p['w2']+self.p['b2'])
        y=np.tanh(h2@self.p['wo']+self.p['bo'])
        room=np.where(y<0,self.negative,self.positive)
        return room*y,(x,h1,h2,y,room)

    def backward(self,cache,gradient):
        x,h1,h2,y,room=cache
        dy=gradient*room*(1-y*y)
        result={'wo':h2.T@dy,'bo':dy.sum(0)}
        dh2=(dy@self.p['wo'].T)*(1-h2*h2)
        result.update(w2=h1.T@dh2,b2=dh2.sum(0))
        dh1=(dh2@self.p['w2'].T)*(1-h1*h1)
        result.update(w1=x.T@dh1,b1=dh1.sum(0))
        return result

    def save(self,path,metadata):
        import json
        np.savez_compressed(path,**self.p,mean=self.mean,scale=self.scale,
                            negative=self.negative,positive=self.positive,metadata=json.dumps(metadata))

    @classmethod
    def load(cls,path):
        model=object.__new__(cls)
        with np.load(path) as data:
            model.p={key:data[key].copy() for key in cls.keys}
            for key in ['mean','scale','negative','positive']: setattr(model,key,data[key].copy())
        return model


def self_test():
    rng=np.random.default_rng(43)
    positions=rng.uniform(-300,300,(10,2));sizes=rng.uniform(20,100,(10,2))
    edges=np.array([[0,1],[2,3],[4,5],[6,7],[0,8],[2,9],[1,5]])
    proxy=LayoutProxy(positions,sizes,edges,100.)
    _,gradient=proxy.loss(positions)
    checked=0
    for n in range(len(positions)):
        for k in range(2):
            step=1e-4;high=positions.copy();low=positions.copy();high[n,k]+=step;low[n,k]-=step
            numerical=(proxy.loss(high)[0]['total']-proxy.loss(low)[0]['total'])/(2*step)
            np.testing.assert_allclose(gradient[n,k],numerical,rtol=2e-5,atol=2e-5);checked+=1
    features=rng.normal(size=(10,64));model=JointPolicy(features,positions,sizes,100.,44)
    model.p['wo']=rng.normal(0,.01,(32,2));model.p['bo']=rng.normal(0,.01,2)
    delta,cache=model.forward(features);_,position_gradient=proxy.loss(positions+delta)
    gradients=model.backward(cache,position_gradient)
    for key in model.keys:
        for _ in range(4):
            index=tuple(rng.integers(size) for size in model.p[key].shape)
            value=model.p[key][index];step=1e-6
            model.p[key][index]=value+step;high=proxy.loss(positions+model.forward(features)[0])[0]['total']
            model.p[key][index]=value-step;low=proxy.loss(positions+model.forward(features)[0])[0]['total']
            model.p[key][index]=value
            np.testing.assert_allclose(gradients[key][index],(high-low)/(2*step),rtol=5e-5,atol=5e-5);checked+=1
    print({'jointPolicyGradientChecks':'pass','finiteDifferenceComparisons':checked})


if __name__=='__main__': self_test()
