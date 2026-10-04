#!/usr/bin/env python3
"""Learned event world selects coordinated same-size card permutations."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from calibrated_geometry_world import load,digest
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native,wire
from run_anchor_pair_walk import WalkDecoder,verify_saved
from run_geometry_world_policy import source_geometry
from single_owner_cached_observer_v2 import CachedObserver,rounded


class UnpooledDecoder(WalkDecoder):
    def action_word(self,word,shift,reverse):
        word=np.asarray(word,dtype=np.int32)
        target=word[::-1] if reverse else np.roll(word,shift)
        assert np.array_equal(self.sizes[word],self.sizes[target])
        delta=np.zeros_like(self.positions); delta[word]=self.positions[target]-self.positions[word]; delta=rounded(delta)
        direction=self.ray_base_direction+delta[self.owner_edges[:,1]]-delta[self.owner_edges[:,0]]
        phase=self.ray_phase(direction)[0]
        offsets=np.mod(phase-self.ray_initial_phase+.5,1.)-.5
        # Each original interior anchor independently decodes an endpoint.
        # There is no objective feedback, endpoint averaging or repair.
        return np.concatenate([delta,offsets])


def vocabulary(decoder,nodes,seed,count):
    active=np.any(nodes[:,4:6]>0,axis=1); words=set()
    for group in decoder.groups:
        for n in group:
            other=group[group!=n]; distance=np.rint(np.sum((decoder.positions[other]-decoder.positions[n])**2,axis=1)*1e6)
            order=other[np.lexsort((other,distance))]
            for width in (4,8):
                if len(group)<width: continue
                word=tuple(sorted([int(n),*map(int,order[:width-1])]))
                if active[list(word)].any(): words.add(word)
    words=sorted(words); assert words
    selected=np.random.default_rng(seed).choice(len(words),min(count,len(words)),replace=False); result=[]
    for i in selected:
        word=np.array(words[int(i)],dtype=np.int32); positions=decoder.positions[word]; center=positions.mean(0)
        angle=np.arctan2(positions[:,1]-center[1],positions[:,0]-center[0])
        result.append(word[np.lexsort((word,angle))].tolist())
    return result,{'kind':'nearest same-size source sets of 4 or 8, source angular order',
        'eligibleUnorderedWords':len(words),'sampledWords':len(result),'futureCostsUsedForSupport':False}


def transforms(width):
    shifts=[1,2,-1] if width==4 else [1,2,4,-1,-2,-4]
    return [(shift,False) for shift in shifts]+[(0,True)]


def run(args):
    assert 1<=args.words<=512 and 1<=args.budget<=128 and 0<args.seconds<=30
    args.out.mkdir(parents=True,exist_ok=False); started=time.monotonic()
    binding=json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert str(args.directory)==binding['directory']
    inputs={n:digest(args.directory/n) for n in INPUT_FILES}; assert inputs==binding['inputs']
    decoder=UnpooledDecoder(args.directory); observer=CachedObserver(args.directory,decoder)
    zero=decoder.decode(np.arange(len(decoder.positions),dtype=np.int32))
    before,sizes,edges=source_geometry(observer,zero,args.view); proxy,model,metadata=load(args.checkpoint)
    native=Native(args.environment,args.directory,args.out/'learned.tsv',args.view=='overview',True)
    rows=[]; spectra=[]; best=None; attempts=accepted=improving=ties=0; reasons=Counter(); wires=set()
    best_visual=native.initial['visual']
    try:
        assert best_visual==binding['expectedVisual']
        baseline=native.request(wire(zero,'MEASURE')); assert baseline['legal']
        assert baseline['hard']==baseline['individualHard']==baseline['spacing']==0
        nodes=np.array([n['features'] for n in native.initial['nodes']]); words,support=vocabulary(decoder,nodes,args.seed,args.words)
        rank_started=time.monotonic()
        for wi,word in enumerate(words):
            for shift,reverse in transforms(len(word)):
                proposed=decoder.action_word(word,shift,reverse); after=source_geometry(observer,proposed,args.view)[0]
                spectrum=proxy.spectrum(before,after,sizes,edges); score=-float(model.forward(spectrum[None])[0])
                rows.append({'candidateIndex':len(rows),'wordIndex':wi,'word':word,'shift':shift,'reverse':reverse,
                    'score':score,'wireSha256':hashlib.sha256(wire(proposed).encode()).hexdigest()}); spectra.append(spectrum)
            if time.monotonic()-rank_started>=args.seconds: break
        scores=np.array([r['score'] for r in rows]); assert len(scores) and np.isfinite(scores).all()
        order=np.argsort(-scores,kind='stable'); rank_seconds=time.monotonic()-rank_started
        np.savez_compressed(args.out/'observations.npz',nodes=nodes,spectra=np.array(spectra),scores=scores,order=order)
        with (args.out/'ranking.jsonl').open('x') as stream:
            for row in rows: stream.write(json.dumps(row)+'\n')
        ranking_hash=digest(args.out/'ranking.jsonl'); evaluation_started=time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for rank,index in enumerate(order[:args.budget]):
                if time.monotonic()-evaluation_started>=args.seconds: break
                row=rows[int(index)]; proposed=decoder.action_word(row['word'],row['shift'],row['reverse']); command=wire(proposed)
                h=hashlib.sha256(command.encode()).hexdigest(); assert h==row['wireSha256'] and h not in wires; wires.add(h)
                result=native.request(command); attempts+=1; reasons[result['reason']]+=1
                improving+=int(result['legal'] and result['visual']<baseline['visual'])
                ties+=int(result['legal'] and result['visual']==baseline['visual'])
                if result['accepted']: best=proposed.copy(); best_visual=result['visual']; accepted+=1
                stream.write(json.dumps({'rank':rank,**row,'result':result})+'\n')
        assert digest(args.out/'ranking.jsonl')==ranking_hash and native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory,args.out,decoder,best)
    if best is not None: np.save(args.out/'best-action.npy',best)
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'],stats['policyActionsEvaluated'],stats['acceptedActions'])==(best_visual,attempts,accepted)
    assert stats['hardConditions']==stats['individualHardConditions']==stats['overlap']==stats['spacing']==0
    report={'kind':'calibrated-event-world-unpooled-local-4-or-8-card-policy-v1','view':args.view,'seed':args.seed,
        'sourceDirectory':str(args.directory),'sourceInputs':inputs,'sourceBinding':str(args.source_binding),
        'sourceBindingSha256':digest(args.source_binding),'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),
        'checkpointMetadata':metadata,'environment':str(args.environment),'environmentSha256':digest(args.environment),
        'wordCountRequested':args.words,'support':support,'candidatesScored':len(rows),'budget':args.budget,
        'secondsLimitPerRankingOrEvaluation':args.seconds,'attempts':attempts,'accepted':accepted,
        'improvingLegalActions':improving,'legalTies':ties,'reasons':dict(reasons),'initialVisual':baseline['visual'],
        'initialIndividualVisual':baseline['individualVisual'],'final':stats,'rankingSeconds':rank_seconds,
        'wallSeconds':time.monotonic()-started,'sameSizeRectangleMultisetPreserved':True,
        'neuralPredictedDeltaSelectsAllCoordinates':True,'sourcePreservingIndependentInteriorAnchorDecoder':True,
        'futureNativeMeasurementsBeforeRankingFrozen':0,'exactFuturePredicatesUsedInRanking':False,
        'nativeSearchCalls':0,'coordinateRepairs':0,'newTrainingUpdatesDuringInference':0,
        'savedGeometryMatchesNeuralSelectedProposal':True,'inferenceFeatureInputDtype':'float32',
        'rankingSha256':ranking_hash,'actionsSha256':digest(args.out/'actions.jsonl'),'observationsSha256':digest(args.out/'observations.npz'),
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in ['run_calibrated_macro_policy.py',
            'calibrated_geometry_world.py','geometry_world_model.py','single_owner_cached_observer_v2.py',
            'joint_anchor_ray_policy.py','run_anchor_pair_walk.py','run_anchor_pair_policy.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','candidatesScored','attempts','accepted','improvingLegalActions',
        'reasons','rankingSeconds','wallSeconds']}|{'initialVisual':baseline['visual'],'finalVisual':best_visual}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--source-binding',type=Path,required=True)
    p.add_argument('--directory',type=Path,required=True); p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--environment',type=Path,required=True); p.add_argument('--view',choices=['overview','individual'],required=True)
    p.add_argument('--out',type=Path,required=True); p.add_argument('--seed',type=int,default=105709)
    p.add_argument('--words',type=int,default=256); p.add_argument('--budget',type=int,default=128)
    p.add_argument('--seconds',type=float,default=30.); run(p.parse_args())
