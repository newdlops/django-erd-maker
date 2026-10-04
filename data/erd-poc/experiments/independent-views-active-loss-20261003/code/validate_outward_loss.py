"""Regression for a near-tangent peer and a tiny inward corner cut."""
import numpy as np
from joint_layout_proxy import LayoutProxy
from joint_hard_geometry import FixedPorts


def main():
    positions=np.array([[0.,0.],[50.,250.]])
    sizes=np.full((2,2),100.)
    edges=np.array([[0,1]])
    ports=np.array([[[50.,0.],[50.,200.]]])
    provider=FixedPorts(positions,edges,ports)
    proxy=LayoutProxy(positions,sizes,edges,1.,route_provider=provider,hard_only=True,hard_weight=1000.)
    records=[];checks=0
    for gap in [-.031,-.021,-.011001,-.011,0.,.021]:
        moved=positions.copy();moved[1,0]+=gap
        values,gradient=proxy.loss(moved)
        expected=1000*max(0.,-gap-.011)**2
        np.testing.assert_allclose(values['total'],expected,atol=1e-10,rtol=1e-8)
        if gap<-.0111:
            for node in range(2):
                hi,lo=moved.copy(),moved.copy();hi[node,0]+=1e-6;lo[node,0]-=1e-6
                numeric=(proxy.loss(hi)[0]['total']-proxy.loss(lo)[0]['total'])/2e-6
                np.testing.assert_allclose(gradient[node,0],numeric,rtol=1e-5,atol=1e-5)
                checks+=1
        records.append({'peerHorizontalGap':gap,'loss':values['total'],'peerGradient':gradient[1,0]})
    assert records[2]['loss']<1e-8 and records[3]['loss']<1e-20
    positions=np.array([[0.,0.],[-200.,250.]])
    ports=np.array([[[50.,49.97],[-200.,200.]]])
    proxy=LayoutProxy(positions,sizes,edges,1.,route_provider=FixedPorts(positions,edges,ports),hard_only=True,hard_weight=1000.)
    values,_=proxy.loss(positions)
    np.testing.assert_allclose(values['outwardBoundaryPenalty'],1000*(.03-.011)**2,atol=1e-10)
    assert values['binaryOutwardEndpoints']==1 and values['binaryOwnCardHits']==0
    print({'passed':True,'peerBoundaryCases':records,'peerPositionGradientChecks':checks,
           'cornerCutDetectedWithoutShrunkenOwnHit':True,'cornerPenalty':values['outwardBoundaryPenalty']})


if __name__=='__main__':main()
