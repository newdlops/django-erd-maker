"""Combine model-selected distinct-owner moves; Native never proposes geometry."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native,wire,digest
from run_anchor_pair_walk import WalkDecoder,array_hash,verify_saved
from run_fraction_conditioned_policy import sample_order
from run_single_owner_cycle_policy_v2 import hashes as parent_hashes
from single_owner_cycle_model import AMPLITUDES,load,rank,vocabulary,allowed_indices,motion_key

ARITIES=(2,4,8,16)

def hashes():
    return parent_hashes()|{'run_neural_owner_batches.py':digest(Path(__file__))}

def propose(decoder,cycles,scores,allowed,seed,arity,temperature):
    ordered,noise,policy=sample_order(scores.ravel()[allowed,None],seed,temperature)
    selected=[];owners=set();zeros=duplicates=0
    for position in ordered:
        flat=int(allowed[position]);ci,ai=map(int,np.unravel_index(flat,scores.shape));key=motion_key(decoder,cycles[ci],AMPLITUDES[ai])
        if key[1:]==(0,0):zeros+=1;continue
        if key[0] in owners:duplicates+=1;continue
        owners.add(key[0]);selected.append({'cycleIndex':ci,'amplitudeIndex':ai,'ownerDeltaCents':list(key),
                                          'score':float(scores[ci,ai]),'gumbel':float(noise[position,0])})
        if len(selected)==arity:break
    assert len(selected)==arity
    return selected,policy,{'zeroDeltasSkipped':zeros,'duplicateOwnersSkipped':duplicates}

def decode(decoder,components):
    cents=np.zeros(decoder.positions.shape,dtype=np.int64)
    for component in components:
        owner,x,y=component['ownerDeltaCents'];assert not np.any(cents[owner]);cents[owner]=(x,y)
    assert np.count_nonzero(np.any(cents!=0,axis=1))==len(components)
    delta=cents.astype(np.float64)/100;phases,_=decoder.endpoint_offsets(delta)
    return np.concatenate([delta,phases])

def run(args):
    assert 1<=args.budget<=256 and 1<=args.triples<=1024 and 0<args.seconds<=30 and 0<args.temperature<=4
    args.out.mkdir(exist_ok=False);started=time.monotonic();decoder=WalkDecoder(args.directory);model,metadata=load(args.checkpoint)
    inputs={n:digest(args.directory/n) for n in INPUT_FILES};binding=json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert (str(args.directory),inputs)==(binding['directory'],binding['inputs'])
    native=Native(args.environment,args.directory,args.out/'learned.tsv',args.view=='overview',True)
    best=None;attempts=accepted=improving=ties=0;reasons=Counter();wires=set()
    try:
        assert native.initial['visual']==binding['expectedVisual']
        zero=decode(decoder,[]);baseline=native.request(wire(zero,'MEASURE'))
        assert baseline['legal'] and baseline['hard']==baseline['individualHard']==baseline['spacing']==0
        nodes=np.array([n['features'] for n in native.initial['nodes']]);cycles,support=vocabulary(decoder,nodes,args.seed,args.triples)
        scores,feature_hashes,error=rank(model,decoder,nodes,cycles,args.view=='overview');allowed=allowed_indices(nodes,cycles)
        np.savez_compressed(args.out/'observations.npz',node_features=nodes,cycles=cycles,amplitudes=AMPLITUDES,scores=scores,allowed_indices=allowed)
        schedule=[];seen=set();draws=duplicates=0
        while len(schedule)<args.budget:
            assert draws<4*args.budget;seed=args.seed+1000+draws;arity=ARITIES[len(schedule)%len(ARITIES)]
            components,policy,dedup=propose(decoder,cycles,scores,allowed,seed,arity,args.temperature);draws+=1
            # This uniqueness check consults only model outputs, never metrics.
            geometry=sorted(c['ownerDeltaCents'] for c in components);key=hashlib.sha256(json.dumps(geometry).encode()).hexdigest()
            if key in seen:duplicates+=1;continue
            seen.add(key);schedule.append({'rank':len(schedule),'seed':seed,'arity':arity,'components':components,
                                          'policy':policy,'deduplication':dedup,'ownerDeltaSetSha256':key})
        (args.out/'proposal-schedule.json').write_text(json.dumps(schedule,indent=2)+'\n');preparation=time.monotonic()-started
        with (args.out/'actions.jsonl').open('x') as stream:
            for entry in schedule:
                if time.monotonic()-started>=args.seconds:break
                proposed=decode(decoder,entry['components']);command=wire(proposed);key=hashlib.sha256(command.encode()).hexdigest()
                assert key not in wires;wires.add(key);result=native.request(command)
                stream.write(json.dumps({'rank':attempts,'arity':entry['arity'],'wireSha256':key,'result':result})+'\n')
                attempts+=1;reasons[result['reason']]+=1;improving+=int(result['legal'] and result['visual']<native.initial['visual'])
                ties+=int(result['legal'] and result['visual']==native.initial['visual'])
                if result['accepted']:accepted+=1;best=proposed.copy()
        assert native.request('SAVE')['saved']
    finally:native.close()
    verify_saved(args.directory,args.out,decoder,best);stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==accepted
    assert stats['hardConditions']==stats['individualHardConditions']==stats['spacing']==stats['overlap']==0
    if best is not None:np.save(args.out/'best-action.npy',best)
    report={'kind':'independent-neural-owner-batch-policy-v1','view':args.view,'sourceDirectory':str(args.directory),'sourceInputs':inputs,
     'sourceBinding':str(args.source_binding),'sourceBindingSha256':digest(args.source_binding),'checkpoint':str(args.checkpoint),
     'checkpointSha256':digest(args.checkpoint),'checkpointTraining':metadata,'criticTrainedOnJointBatches':False,
     'environment':str(args.environment),'environmentSha256':digest(args.environment),'seed':args.seed,'triples':args.triples,
     'temperature':args.temperature,'arities':list(ARITIES),'sourceSupport':support,'featureChunkHashes':feature_hashes,'scaleFeatureMaxError':error,
     'budget':args.budget,'secondsLimit':args.seconds,'scheduledBeforeAnyNativeAction':len(schedule),'modelSamplingDraws':draws,
     'duplicateBatchGeometriesSkipped':duplicates,'attempts':attempts,'accepted':accepted,'improvingLegalActions':improving,'legalTies':ties,
     'reasons':dict(reasons),'initialVisual':native.initial['visual'],'initialIndividualVisual':native.initial['individualVisual'],
     'baselineNativeMeasurement':baseline,'final':stats,'modelProposesEachOwnerMovement':True,'distinctOwnersWithinEachBatch':True,
     'componentScoresCombinedForBatchScore':False,'batchHasLearnedJointInteractionModel':False,
     'newTrainingUpdates':0,'nativeCoordinateSearchOrRepairAdded':False,'neutralGeometriesCannotReplaceSavedBest':True,
     'policyOrderFixedBeforeNativeMeasurement':True,'allHistoryWireExclusionClaimed':False,'preparationSeconds':preparation,
     'wallSeconds':time.monotonic()-started,'codeSha256':hashes(),'observationsSha256':digest(args.out/'observations.npz'),
     'scheduleSha256':digest(args.out/'proposal-schedule.json'),'actionsSha256':digest(args.out/'actions.jsonl')}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('view','attempts','accepted','improvingLegalActions','legalTies','reasons','preparationSeconds','wallSeconds')}|{'finalVisual':stats['visual']}),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);p.add_argument('--source-binding',type=Path,required=True)
    p.add_argument('--checkpoint',type=Path,required=True);p.add_argument('--environment',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--view',choices=['overview','individual'],required=True);p.add_argument('--seed',type=int,default=99103)
    p.add_argument('--triples',type=int,default=1024);p.add_argument('--budget',type=int,default=256)
    p.add_argument('--seconds',type=float,default=30);p.add_argument('--temperature',type=float,default=4.);run(p.parse_args())
