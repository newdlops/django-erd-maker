#!/usr/bin/env python3
"""Batched neural ranking over all equal-size pairs touching current conflicts.

Only the trained network ranks proposals. Native geometry scores the submitted
model outputs, with no coordinate search, repair or fallback. Source remains
immutable throughout each stage.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from full_context_pair_model import expand,pressure,load,SCHEMA
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native,pair_features,wire,digest
from run_anchor_pair_walk import WalkDecoder,verify_saved,array_hash


def vocabulary(decoder,features):
    active=np.any(features[:,4:6]>0,axis=1);chunks=[];all_count=expected=0
    choose=lambda n:n*(n-1)//2
    for group in decoder.groups:
        n=len(group);all_count+=choose(n);a,b=np.triu_indices(n,1)
        candidates=np.stack([group[a],group[b]],axis=1).astype(np.int32)
        keep=active[candidates].any(1)&~decoder.irrelevant_isolate[candidates].all(1)
        chunks.append(candidates[keep])
        p=int(active[group].sum());isolated=decoder.irrelevant_isolate[group]
        i=int(isolated.sum());ip=int(np.sum(isolated&active[group]))
        expected+=choose(n)-choose(n-p)-(choose(i)-choose(i-ip))
    pairs=np.concatenate(chunks)
    assert 0<len(pairs)==expected and all_count<=250000
    assert pressure(features,pairs).all()
    return pairs,{'allEqualSizePairs':all_count,'eligiblePairs':len(pairs),'pressuredNodes':int(active.sum()),
        'support':'at least one current physical-view crossing/hit; exclude two isolated single cards',
        'combinatorialCountVerified':True,'coversIndirectPoolingOnlyConflicts':False}


def rank(model,features,decoder,pairs,overview,chunk=2048):
    scores=np.empty(len(pairs));hashes=[];max_error=0
    for offset in range(0,len(pairs),chunk):
        selected=pairs[offset:offset+chunk]
        base,error=pair_features(features,decoder,selected,overview);full=expand(base,features,selected)
        scores[offset:offset+len(selected)]=model.forward(full)[0]
        hashes.append(array_hash(full));max_error=max(max_error,error)
    assert np.isfinite(scores).all()
    return scores,hashes,max_error


def run(args):
    assert 1<=args.budget<=2048 and 0<args.seconds<=20
    args.out.mkdir(parents=True,exist_ok=False);started=time.monotonic()
    decoder=WalkDecoder(args.directory);model,metadata=load(args.checkpoint)
    native=Native(args.environment,args.directory,args.out/'learned.tsv',args.view=='overview',True)
    attempts=accepted=0;best=None;reasons=Counter();best_visual=native.initial['visual']
    try:
        features=np.array([row['features'] for row in native.initial['nodes']])
        pairs,support=vocabulary(decoder,features);ranking_started=time.monotonic()
        scores,hashes,error=rank(model,features,decoder,pairs,args.view=='overview')
        ranking_seconds=time.monotonic()-ranking_started;order=np.argsort(-scores,kind='stable')
        np.savez_compressed(args.out/'observations.npz',node_features=features,pairs=pairs,scores=scores)
        action_started=time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for index in order[:args.budget]:
                if time.monotonic()-started>=args.seconds:break
                a,b=map(int,pairs[index]);action=decoder.action(a,b);command=wire(action)
                result=native.request(command)
                stream.write(json.dumps({'rank':attempts,'source':a,'target':b,'score':float(scores[index]),
                    'wireSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result})+'\n')
                attempts+=1;reasons[result['reason']]+=1
                if result['accepted']:accepted+=1;best=action.copy();best_visual=result['visual']
        action_seconds=time.monotonic()-action_started
        assert native.request('SAVE')['saved']
    finally:native.close()
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==accepted and stats['visual']==best_visual
    verify_saved(args.directory,args.out,decoder,best)
    if best is not None:np.save(args.out/'best-action.npy',best)
    report={'kind':'full-context-pressure-pair-policy-v1','schema':SCHEMA,'view':args.view,'support':support,
        'sourceDirectory':str(args.directory),'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),
        'checkpointTraining':metadata,'environment':str(args.environment),'environmentSha256':digest(args.environment),
        'budget':args.budget,'secondsLimit':args.seconds,'rankingSeconds':ranking_seconds,'actionSeconds':action_seconds,
        'wallSeconds':time.monotonic()-started,'attempts':attempts,'accepted':accepted,'reasons':dict(reasons),
        'initialVisual':native.initial['visual'],'initialIndividualVisual':native.initial['individualVisual'],'final':stats,
        'featureChunkSize':2048,'featureChunkHashes':hashes,'scaleFeatureMaxError':error,'allSubmittedPairsTouchDirectConflicts':True,
        'newTrainingUpdatesDuringInference':0,'modelNamesAsFeatures':False,'absoluteCoordinatesAsFeatures':False,
        'coordinateRepairs':0,'heuristicSearchCalls':0,'neuralRankedAllEligiblePairs':True,
        'savedGeometryMatchesModelOutput':True,'causalAblationVerified':False,
        'sourceInputs':{name:digest(args.directory/name) for name in INPUT_FILES},
        'observationsSha256':digest(args.out/'observations.npz'),'actionsSha256':digest(args.out/'actions.jsonl'),
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in ['full_context_pair_model.py','run_full_context_pairs.py',
            'run_anchor_pair_walk.py','run_anchor_pair_policy.py','learn_pair_policy.py','joint_anchor_ray_policy.py','joint_neural_ports.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','support','attempts','accepted','initialVisual','reasons','rankingSeconds',
        'actionSeconds','wallSeconds']}|{'finalVisual':stats['visual']}))


def replay(args):
    report=json.loads((args.out/'report.json').read_text());directory=Path(report['sourceDirectory'])
    assert report['schema']==SCHEMA and digest(report['checkpoint'])==report['checkpointSha256']
    for name,sha in report['sourceInputs'].items():assert digest(directory/name)==sha
    for name,sha in report['codeSha256'].items():assert digest(Path(__file__).parent/name)==sha
    assert digest(args.out/'observations.npz')==report['observationsSha256'] and digest(args.out/'actions.jsonl')==report['actionsSha256']
    decoder=WalkDecoder(directory);model,_=load(Path(report['checkpoint']))
    with np.load(args.out/'observations.npz',allow_pickle=False) as saved:
        features=saved['node_features'].copy();pairs,support=vocabulary(decoder,features)
        assert support==report['support'] and np.array_equal(pairs,saved['pairs'])
        scores,hashes,_=rank(model,features,decoder,pairs,report['view']=='overview',report['featureChunkSize'])
        assert np.array_equal(scores,saved['scores']) and hashes==report['featureChunkHashes']
    order=np.argsort(-scores,kind='stable');attempts=accepted=0;best=None
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row=json.loads(line);assert row['rank']==attempts;index=order[attempts];a,b=map(int,pairs[index])
        assert (a,b)==(row['source'],row['target']) and row['score']==scores[index]
        action=decoder.action(a,b);assert hashlib.sha256(wire(action).encode()).hexdigest()==row['wireSha256']
        if row['result']['accepted']:best=action;accepted+=1
        attempts+=1
    assert attempts==report['attempts'] and accepted==report['accepted']
    if best is not None:assert np.array_equal(best,np.load(args.out/'best-action.npy'))
    verify_saved(directory,args.out,decoder,best)
    result={'status':'pass','allEligibleNeuralScoresReplayed':len(pairs),'modelRankedActionsReplayed':attempts,
        'accepted':accepted,'savedGeometryMatchesModelOutput':True,'nativeScoresRecomputed':False}
    (args.out/'replay.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['run','replay']);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--directory',type=Path);p.add_argument('--checkpoint',type=Path);p.add_argument('--environment',type=Path)
    p.add_argument('--view',choices=['individual','overview']);p.add_argument('--budget',type=int,default=1024)
    p.add_argument('--seconds',type=float,default=20);args=p.parse_args();(run if args.mode=='run' else replay)(args)
