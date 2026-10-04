"""Uniform global source words for neural one-owner translations of all sizes."""
import numpy as np

def vocabulary(decoder,nodes,seed,count):
    assert 1<=count<=1024
    active=np.any(nodes[:,4:6]>0,axis=1);a=np.flatnonzero(active);b=np.flatnonzero(~active)
    c2=lambda n:n*(n-1)//2;c3=lambda n:n*(n-1)*(n-2)//6
    one=len(a)*c2(len(b));two=c2(len(a))*len(b);three=c3(len(a));total=one+two+three
    requested=min(count,total);assert requested>0
    rng=np.random.default_rng(seed);seen=set();words=[];draws=0
    while len(words)<requested:
        assert draws<requested*1000;pick=int(rng.integers(total));draws+=1
        if pick<one:word=tuple(sorted((int(rng.choice(a)),*map(int,rng.choice(b,2,replace=False)))))
        elif pick<one+two:word=tuple(sorted((*map(int,rng.choice(a,2,replace=False)),int(rng.choice(b)))))
        else:word=tuple(sorted(map(int,rng.choice(a,3,replace=False))))
        if word in seen:continue
        seen.add(word);words.append(word)
    words=np.array(words,dtype=np.int32);cycles=np.empty((2*requested,3),dtype=np.int32)
    cycles[::2]=words;cycles[1::2]=words[:,[0,2,1]]
    assert active[cycles].any(axis=1).all()
    return cycles,{'kind':'uniform global all-size source triples with at least one directly conflicting owner',
        'eligibleUnorderedTriples':total,'sampledUnorderedTriples':requested,'draws':draws,
        'bothDirections':True,'sameSizeTriplesAllowed':True,'conflictingIsolatedCardsEligible':True,
        'symmetricSwapIsolateExclusionNotAppliedToTranslations':True,
        'sourceGeometryAndInitialObservationsOnly':True,'nativeMeasurementsUsedToChooseDirections':False,'coordinateProposalsBySupport':False}
