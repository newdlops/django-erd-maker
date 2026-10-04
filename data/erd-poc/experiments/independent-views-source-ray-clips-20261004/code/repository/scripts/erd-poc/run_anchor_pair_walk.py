#!/usr/bin/env python3
"""Frozen pair-policy trajectories with exact observations after admitted moves.

All positions are cumulative model-selected permutations of same-size slots.
Native geometry only observes, scores and retains strict improvements. Neutral
walk states remain experimental and cannot replace the best saved geometry.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace
import numpy as np
from learn_pair_policy import load
from learned_global_replay import INPUT_FILES,pairs,routes
from joint_neural_ports import perimeter_points
from run_anchor_pair_policy import PairDecoder,Native,pair_features,wire,digest


def array_hash(value):
    return hashlib.sha256(np.ascontiguousarray(value).tobytes()).hexdigest()


class WalkDecoder(PairDecoder):
    def __init__(self,directory):
        super().__init__(directory)
        incident=np.bincount(self.provider.owner_edges.ravel(),minlength=len(self.positions))
        members=np.bincount(self.provider.owner,minlength=len(self.positions))
        # Swapping two isolated single cards of the same size cannot affect
        # any route or the rectangle multiset. Multi-member cards stay eligible.
        self.irrelevant_isolate=(incident==0)&(members==1)

    def eligible_pairs(self,seed,count):
        vocabulary=self.vocabulary(seed,count)
        return vocabulary[~self.irrelevant_isolate[vocabulary].all(1)]

    def decode(self,permutation):
        assert sorted(permutation.tolist())==list(range(len(self.positions)))
        assert np.array_equal(self.sizes[permutation],self.sizes)
        delta=self.positions[permutation]-self.positions
        delta=np.copysign(np.floor(abs(delta)*100+.5),delta)/100
        phases,_=self.endpoint_offsets(delta)
        return np.concatenate([delta,phases])

    def feature_view(self,permutation):
        delta=self.decode(permutation)[:len(self.positions)]
        return SimpleNamespace(positions=self.positions+delta,provider=self.provider)


def verify_saved(directory,out,decoder,action):
    n=len(decoder.positions)
    if action is None:action=decoder.decode(np.arange(n,dtype=np.int32))
    decoded=np.fromstring(wire(action)[4:],sep=' ').reshape(-1,2)
    delta=np.copysign(np.floor(abs(decoded[:n])*100+.5),decoded[:n])/100
    actual=np.array(list(pairs(out/'learned.tsv').values()))
    assert np.max(abs(actual-(decoder.positions+delta)))<1e-8
    expected_full=np.array(list(pairs(directory/'individual.positions.tsv').values()))+delta[decoder.provider.owner]
    actual_full=np.array(list(pairs(out/'learned.tsv.individual').values()))
    assert np.max(abs(actual_full-expected_full))<1e-8
    provider=decoder.provider
    changed=perimeter_points(provider.phase+decoded[n:],provider.endpoint_sizes)[0]-provider.base_boundary
    ports=provider.original_ports+changed+delta[provider.owner_edges]
    ports=np.copysign(np.floor(abs(ports)*100+.5),ports)/100
    actual_ports=np.array(list(routes(out/'learned.tsv.individual.routes.tsv').values()))
    assert np.array_equal(ports,actual_ports)


def run(args):
    assert 1<=args.rounds<=24 and 1<=args.per_round<=128 and 1<=args.budget<=2048
    assert 1<=args.vocabulary<=4096 and 0<args.seconds<=20
    args.out.mkdir(parents=True,exist_ok=False)
    started=time.monotonic();overview=args.view=='overview';decoder=WalkDecoder(args.directory)
    model,metadata=load(args.checkpoint);n=len(decoder.positions)
    native=Native(args.environment,args.directory,args.out/'learned.tsv',overview,True)
    permutation=np.arange(n,dtype=np.int32);visited={array_hash(permutation)};state=native.initial
    attempts=considered=admitted=improved=observations=0;reasons=Counter();rounds=[];best=None
    best_visual=current_visual=state['visual'];current_individual=state['individualVisual']
    init_seconds=time.monotonic()-started;active_started=time.monotonic()
    try:
        with (args.out/'actions.jsonl').open('x') as stream:
            for round_id in range(args.rounds):
                if considered>=args.budget or time.monotonic()-active_started>=args.seconds:break
                features=np.array([row['features'] for row in state['nodes']])
                vocabulary=decoder.eligible_pairs(args.seed+round_id,args.vocabulary)
                inputs,error=pair_features(features,decoder.feature_view(permutation),vocabulary,overview)
                scores=model.forward(inputs)[0];order=np.argsort(-scores,kind='stable')
                observation_file=args.out/f'observation-{round_id:02}.npz'
                np.savez_compressed(observation_file,node_features=features,pairs=vocabulary,permutation=permutation)
                rounds.append({'round':round_id,'file':observation_file.name,'sha256':digest(observation_file),
                    'pairs':len(vocabulary),'excludedIsolatePairs':args.vocabulary-len(vocabulary),
                    'inputHash':array_hash(inputs),'scoreHash':array_hash(scores),'scaleFeatureMaxError':error,
                    'visual':current_visual,'individualVisual':current_individual})
                moved=False
                for rank,index in enumerate(order[:args.per_round]):
                    if considered>=args.budget or time.monotonic()-active_started>=args.seconds:break
                    a,b=map(int,vocabulary[index]);candidate=permutation.copy();candidate[a],candidate[b]=candidate[b],candidate[a]
                    key=array_hash(candidate);considered+=1
                    row={'round':round_id,'rank':rank,'source':a,'target':b,'score':float(scores[index]),'permutationHash':key}
                    if key in visited:
                        stream.write(json.dumps(row|{'skipped':'visited'})+'\n');continue
                    action=decoder.decode(candidate);command=wire(action);result=native.request(command)
                    attempts+=1;reasons[result['reason']]+=1
                    admissible=result['legal'] and result['visual']<=current_visual and (overview or result['individualVisual']<=current_individual)
                    if result['accepted']:
                        improved+=1;best=action.copy();best_visual=result['visual']
                    row|={'wireSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result,'walkAdmitted':admissible}
                    stream.write(json.dumps(row)+'\n')
                    if admissible:
                        permutation=candidate;visited.add(key);admitted+=1;moved=True
                        current_visual=result['visual'];current_individual=result['individualVisual']
                        state=native.request(wire(action,'OBS'));observations+=1
                        assert state['observed'] and state['visual']==current_visual and state['individualVisual']==current_individual
                        break
                if moved:stream.flush()
        active_seconds=time.monotonic()-active_started
        assert native.request('SAVE')['saved']
    finally:native.close()
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==improved and stats['visual']==best_visual
    verify_saved(args.directory,args.out,decoder,best)
    np.save(args.out/'walk-permutation.npy',permutation)
    if best is not None:np.save(args.out/'best-action.npy',best)
    report={'kind':'frozen-anchor-pair-neutral-walk-v1','view':args.view,'sourceDirectory':str(args.directory),
        'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),'checkpointTraining':metadata,
        'environment':str(args.environment),'environmentSha256':digest(args.environment),'newTrainingUpdates':0,
        'seed':args.seed,'vocabulary':args.vocabulary,'roundLimit':args.rounds,'perRound':args.per_round,
        'budget':args.budget,'secondsLimit':args.seconds,'initializationSeconds':init_seconds,
        'activeSeconds':active_seconds,'wallSeconds':time.monotonic()-started,'considered':considered,'attempts':attempts,
        'admittedWalkStates':admitted,'strictImprovements':improved,'observations':observations,
        'initialVisual':native.initial['visual'],'initialIndividualVisual':native.initial['individualVisual'],
        'walkVisual':current_visual,'walkIndividualVisual':current_individual,'final':stats,'reasons':dict(reasons),
        'rounds':rounds,'savedModelOutputGeometryVerified':True,'heuristicSearchCalls':0,'coordinateRepairs':0,
        'neutralWalkNotPromoted':True,'sourceInputs':{name:digest(args.directory/name) for name in INPUT_FILES},
        'actionsSha256':digest(args.out/'actions.jsonl'),'permutationSha256':digest(args.out/'walk-permutation.npy'),
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in ['run_anchor_pair_walk.py','run_anchor_pair_policy.py',
            'learn_pair_policy.py','joint_anchor_ray_policy.py','joint_neural_ports.py','joint_grouped_routes.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','attempts','considered','admittedWalkStates','strictImprovements','observations',
        'initialVisual','walkVisual','reasons','initializationSeconds','activeSeconds','wallSeconds']}|{'savedVisual':stats['visual']}))


def replay(args):
    report=json.loads((args.out/'report.json').read_text());directory=Path(report['sourceDirectory'])
    assert digest(report['checkpoint'])==report['checkpointSha256'] and digest(args.out/'actions.jsonl')==report['actionsSha256']
    assert digest(args.out/'walk-permutation.npy')==report['permutationSha256']
    for name,sha in report['sourceInputs'].items():assert digest(directory/name)==sha
    for name,sha in report['codeSha256'].items():assert digest(Path(__file__).parent/name)==sha
    decoder=WalkDecoder(directory);model,_=load(Path(report['checkpoint']));permutation=np.arange(len(decoder.positions),dtype=np.int32)
    visited={array_hash(permutation)};best=None;attempts=admitted=improved=considered=0
    visual=report['initialVisual'];individual=report['initialIndividualVisual']
    actions=[json.loads(line) for line in (args.out/'actions.jsonl').read_text().splitlines()]
    for record in report['rounds']:
        path=args.out/record['file'];assert digest(path)==record['sha256']
        with np.load(path,allow_pickle=False) as saved:
            assert np.array_equal(permutation,saved['permutation'])
            vocabulary=decoder.eligible_pairs(report['seed']+record['round'],report['vocabulary'])
            assert np.array_equal(vocabulary,saved['pairs'])
            inputs,_=pair_features(saved['node_features'],decoder.feature_view(permutation),vocabulary,report['view']=='overview')
        scores=model.forward(inputs)[0];assert array_hash(inputs)==record['inputHash'] and array_hash(scores)==record['scoreHash']
        order=np.argsort(-scores,kind='stable');moved=False
        rows=[row for row in actions if row['round']==record['round']]
        for rank,row in enumerate(rows):
            assert not moved and rank==row['rank'];a,b=map(int,vocabulary[order[rank]])
            assert (a,b)==(row['source'],row['target']) and scores[order[rank]]==row['score']
            candidate=permutation.copy();candidate[a],candidate[b]=candidate[b],candidate[a]
            key=array_hash(candidate);assert key==row['permutationHash'];considered+=1
            if 'skipped' in row:assert row['skipped']=='visited' and key in visited;continue
            assert key not in visited;action=decoder.decode(candidate)
            assert hashlib.sha256(wire(action).encode()).hexdigest()==row['wireSha256'];attempts+=1
            result=row['result'];accept=result['legal'] and result['visual']<=visual and (report['view']=='overview' or result['individualVisual']<=individual)
            assert accept==row['walkAdmitted']
            if result['accepted']:best=action;improved+=1
            if accept:
                permutation=candidate;visited.add(key);admitted+=1;moved=True;visual=result['visual'];individual=result['individualVisual']
    assert (attempts,considered,admitted,improved)==tuple(report[k] for k in ['attempts','considered','admittedWalkStates','strictImprovements'])
    assert np.array_equal(np.load(args.out/'walk-permutation.npy'),permutation)
    if best is not None:assert np.array_equal(best,np.load(args.out/'best-action.npy'))
    verify_saved(directory,args.out,decoder,best)
    result={'status':'pass','modelRankingsAndCumulativeActionsReplayed':considered,'nativeActions':attempts,
        'admittedWalkStates':admitted,'strictImprovements':improved,'savedGeometryMatchesModelOutput':True,'nativeScoresRecomputed':False}
    (args.out/'replay.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['run','replay']);p.add_argument('--directory',type=Path)
    p.add_argument('--environment',type=Path);p.add_argument('--checkpoint',type=Path);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--view',choices=['individual','overview']);p.add_argument('--seed',type=int,default=98339)
    p.add_argument('--vocabulary',type=int,default=4096);p.add_argument('--budget',type=int,default=1024)
    p.add_argument('--rounds',type=int,default=16);p.add_argument('--per-round',type=int,default=128);p.add_argument('--seconds',type=float,default=20)
    args=p.parse_args();(run if args.mode=='run' else replay)(args)
