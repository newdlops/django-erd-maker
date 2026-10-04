"""All current near-pair losses, evaluated in bounded chunks.

The logistic tail is explicitly truncated at 24 smoothing units. Bounding boxes
exclude only pairs with zero truncated loss. Every actual conflict remains in
the loss, and exact native validation remains independent and unchanged.
"""
import numpy as np
from joint_layout_proxy import LayoutProxy,clipped_ports,sigmoid


class ActiveLayoutProxy(LayoutProxy):
    def __init__(self, positions, sizes, edges, spacing_weight=20., route_provider=None, chunk_size=16384):
        self.origin,self.sizes,self.edges=positions,sizes,edges
        self.spacing_weight,self.route_provider=spacing_weight,route_provider
        self.hard_only,self.hard_weight=False,0.
        self.cross_weight=self.hit_weight=1.
        self.visual_cutoff_sigmas=24.
        self.skip_regularizers=False
        if not 1<=chunk_size<=16384:raise ValueError('bounded loss chunk must be in 1..16384')
        self.chunk_size=chunk_size
        population=len(edges)*(len(edges)-1)/2+len(edges)*len(positions)
        self.loss_tail_bound=float(population*sigmoid(-self.visual_cutoff_sigmas))
        self.refresh(positions,1.)

    def refresh(self, positions, temperature):
        if self.route_provider is None:
            ports=clipped_ports(positions,self.sizes,self.edges)[0];edges=self.edges
        else:ports,_,edges=self.route_provider.forward(positions,self.sizes)
        low,high=ports.min(1),ports.max(1)
        pad=2*self.visual_cutoff_sigmas*temperature
        near=(low[:,None,0]<=high[None,:,0]+pad)&(low[None,:,0]<=high[:,None,0]+pad)
        near&=(low[:,None,1]<=high[None,:,1]+pad)&(low[None,:,1]<=high[:,None,1]+pad)
        for first in range(2):
            for second in range(2):near&=edges[:,None,first]!=edges[None,:,second]
        self.cross_pairs=np.stack(np.nonzero(np.triu(near,1)),1).astype(np.int32)
        card_low=positions-self.sizes/2-10;card_high=positions+self.sizes/2+10
        pad=self.visual_cutoff_sigmas*temperature
        near=(low[:,None,0]<=card_high[None,:,0]+pad)&(card_low[None,:,0]<=high[:,None,0]+pad)
        near&=(low[:,None,1]<=card_high[None,:,1]+pad)&(card_low[None,:,1]<=high[:,None,1]+pad)
        row=np.arange(len(edges));near[row,edges[:,0]]=False;near[row,edges[:,1]]=False
        self.hit_pairs=np.stack(np.nonzero(near),1).astype(np.int32)
        near=abs(positions[:,None,0]-positions[None,:,0])<(self.sizes[:,None,0]+self.sizes[None,:,0])/2+58
        near&=abs(positions[:,None,1]-positions[None,:,1])<(self.sizes[:,None,1]+self.sizes[None,:,1])/2+44
        self.near_pairs=np.stack(np.nonzero(np.triu(near,1)),1).astype(np.int32)

    def loss(self, positions, temperature=1.):
        self.refresh(positions,temperature)
        cp,hp=self.cross_pairs,self.hit_pairs
        if max(len(cp),len(hp))<=self.chunk_size:
            values,gradient=super().loss(positions,temperature)
        else:
            empty=np.empty((0,2),dtype=np.int32)
            try:
                self.cross_pairs=self.hit_pairs=empty
                values,gradient=super().loss(positions,temperature)
                self.skip_regularizers=True
                for start in range(0,max(len(cp),len(hp)),self.chunk_size):
                    self.cross_pairs=cp[start:start+self.chunk_size]
                    self.hit_pairs=hp[start:start+self.chunk_size]
                    part,g=super().loss(positions,temperature)
                    gradient+=g
                    for key,value in part.items():values[key]=values.get(key,0)+value
            finally:
                self.cross_pairs,self.hit_pairs=cp,hp
                self.skip_regularizers=False
        values['activeCrossPairs']=len(cp);values['activeHitPairs']=len(hp)
        return values,gradient


if __name__=='__main__':
    rng=np.random.default_rng(741)
    original=rng.uniform(-500,500,(16,2));sizes=rng.uniform(25,110,(16,2))
    edges=np.array([[0,1],[2,3],[4,5],[6,7],[8,9],[10,11],[12,13],[14,15],[1,5],[3,9]])
    full=LayoutProxy(original,sizes,edges,5000.);full.visual_cutoff_sigmas=24.
    # Tiny chunks deliberately exercise gradient and regularizer accumulation.
    active=ActiveLayoutProxy(original,sizes,edges,chunk_size=3)
    checked=0
    for temperature in [1.,8.,32.]:
        positions=original+rng.uniform(-150,150,original.shape)
        expected,expected_gradient=full.loss(positions,temperature)
        actual,gradient=active.loss(positions,temperature)
        for key in expected:
            np.testing.assert_allclose(actual[key],expected[key],atol=1e-10,rtol=1e-10)
        np.testing.assert_allclose(gradient,expected_gradient,atol=1e-10,rtol=1e-10)
        for node in range(len(positions)):
            for axis in range(2):
                hi,lo=positions.copy(),positions.copy();hi[node,axis]+=1e-4;lo[node,axis]-=1e-4
                numeric=(active.loss(hi,temperature)[0]['total']-active.loss(lo,temperature)[0]['total'])/2e-4
                np.testing.assert_allclose(gradient[node,axis],numeric,atol=3e-5,rtol=3e-5);checked+=1
    print({'activeLossGradientComparisons':checked,'allPopulationTruncatedLossAndGradientMatch':True,
           'chunkedRegularizersCountedOnce':True,'passed':True})
