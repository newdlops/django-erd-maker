#!/usr/bin/env python3
"""Transfer a trained pair ranker to equal-size swaps with interior-anchor ports.

The model alone orders a uniformly sampled, objective-independent vocabulary.
Each action is decoded from the immutable source. Native code only measures and
accepts a legal improvement. This is frozen transfer, not new model training.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import time

import numpy as np
from learn_pair_policy import load
from learned_global_replay import INPUT_FILES, pairs
from joint_anchor_ray_policy import InteriorAnchorEndpoints
from joint_neural_ports import PerimeterRoutes


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class PairDecoder(InteriorAnchorEndpoints):
    def __init__(self, directory):
        self.positions=np.array(list(pairs(directory/'positions.tsv').values()))
        self.sizes=np.array(list(pairs(directory/'nodes.tsv').values()))
        self.provider=PerimeterRoutes(directory)
        self.initialize_anchors(self.positions,self.sizes,self.provider)
        grouped={}
        for n,size in enumerate(self.sizes):grouped.setdefault(tuple(size),[]).append(n)
        self.groups=[np.array(group,dtype=np.int32) for group in grouped.values() if len(group)>1]

    def action(self,n,m):
        assert n!=m and np.array_equal(self.sizes[n],self.sizes[m])
        delta=np.zeros_like(self.positions)
        delta[n]=self.positions[m]-self.positions[n];delta[m]=-delta[n]
        delta=np.copysign(np.floor(abs(delta)*100+.5),delta)/100
        assert np.count_nonzero(np.any(delta,axis=1))==2
        phases,_=self.endpoint_offsets(delta)
        return np.concatenate([delta,phases])

    def vocabulary(self,seed,count):
        rng=np.random.default_rng(seed)
        sizes=np.array([len(g)*(len(g)-1)//2 for g in self.groups],dtype=np.int64)
        cumulative=np.cumsum(sizes);total=int(cumulative[-1]);assert total>=count
        # One random integer maps to exactly one unordered equal-size pair.
        chosen=rng.choice(total,count,replace=False);result=[]
        for index in chosen:
            gi=int(np.searchsorted(cumulative,index,side='right'))
            offset=int(index-(cumulative[gi-1] if gi else 0));group=self.groups[gi]
            n=0
            while offset>=len(group)-n-1:offset-=len(group)-n-1;n+=1
            result.append((int(group[n]),int(group[n+1+offset])))
        return np.array(result,dtype=np.int32)


def pair_features(features,decoder,vocabulary,overview):
    provider=decoder.provider;positions=decoder.positions
    full=positions[provider.owner]+provider.offsets
    lengths=[[] for _ in positions];neighbors=[set() for _ in positions]
    for (s,t),(a,b) in zip(provider.full_edges,provider.owner_edges):
        if a==b:continue
        lengths[a].append(float(np.hypot(*(full[t]-positions[a]))))
        lengths[b].append(float(np.hypot(*(full[s]-positions[b]))))
        neighbors[a].add(int(b));neighbors[b].add(int(a))
    scales=np.array([max(512.,sorted(row)[len(row)//2] if row else 512.) for row in lengths])
    # Independent reconstruction, checked against the observed native feature.
    error=float(np.max(abs(np.minimum(8,np.log1p(scales/512))-features[:,62])))
    assert error<1e-9,('native scale feature mismatch',error)
    a,b=vocabulary.T;delta=positions[b]-positions[a]
    pair=np.concatenate([features[a,:24],features[b,:24],
        np.clip(delta/scales[a,None],-8,8),np.clip(-delta/scales[b,None],-8,8),
        features[a,56:60],features[b,56:60],np.clip(np.log(scales[a]/scales[b]),-8,8)[:,None],
        np.array([[b0 in neighbors[a0],np.log1p(len(neighbors[a0]&neighbors[b0])),overview]
                  for a0,b0 in vocabulary])],axis=1)
    assert pair.shape==(len(vocabulary),64) and np.isfinite(pair).all()
    return pair,error


def wire(action,op='TRY'):
    return op+' '+' '.join(f'{float(value):.12g}' for value in action.ravel())


class Native:
    def __init__(self,environment,directory,out,overview,sparse):
        self.process=subprocess.Popen([str(environment),'--directory',str(directory),'--out',str(out),
            '--overview-only',str(int(overview)),'--neural-perimeter-ports','1','--sparse-scoring',str(int(sparse))],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
        self.initial=self.receive()
        assert self.initial['ready']

    def receive(self):
        line=self.process.stdout.readline()
        if not line:raise RuntimeError(f'native exit: {self.process.poll()}')
        return json.loads(line)

    def request(self,command):
        self.process.stdin.write(command+'\n');self.process.stdin.flush();return self.receive()

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.write('QUIT\n');self.process.stdin.flush()
            try:self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:self.process.kill();self.process.wait();raise
        assert self.process.returncode==0


def run(args):
    assert 1<=args.budget<=1024 and args.budget<=args.vocabulary<=4096 and 0<args.seconds<=20
    args.out.mkdir(parents=True,exist_ok=False)
    started=time.monotonic();overview=args.view=='overview'
    model,metadata=load(args.checkpoint)
    decoder=PairDecoder(args.directory)
    native=Native(args.environment,args.directory,args.out/'learned.tsv',overview,True)
    features=np.array([row['features'] for row in native.initial['nodes']])
    assert features.shape==(len(decoder.positions),64)
    vocabulary=decoder.vocabulary(args.seed,args.vocabulary)
    inputs,error=pair_features(features,decoder,vocabulary,overview)
    scores=model.forward(inputs)[0];order=np.argsort(-scores,kind='stable')
    np.savez_compressed(args.out/'observations.npz',node_features=features,pairs=vocabulary,pair_features=inputs,scores=scores)
    init_seconds=time.monotonic()-started
    attempts=accepted=0;reasons=Counter();best_rank=None;best_visual=native.initial['visual']
    action_started=time.monotonic()
    try:
        with (args.out/'actions.jsonl').open('x') as stream:
            for rank,index in enumerate(order[:args.budget]):
                if time.monotonic()-action_started>=args.seconds:break
                n,m=map(int,vocabulary[index]);action=decoder.action(n,m);command=wire(action)
                result=native.request(command);attempts+=1;accepted+=result['accepted'];reasons[result['reason']]+=1
                if result['accepted']:best_rank=rank;best_visual=result['visual']
                stream.write(json.dumps({'rank':rank,'source':n,'target':m,'score':float(scores[index]),
                    'wireSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result})+'\n')
        action_seconds=time.monotonic()-action_started
        assert native.request('SAVE')['saved']
    finally:native.close()
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==accepted
    assert stats['visual']==best_visual
    report={'kind':'frozen-trained-pair-ranker-with-equal-size-anchor-decoder-v1','view':args.view,
        'sourceDirectory':str(args.directory),'environment':str(args.environment),'environmentSha256':digest(args.environment),
        'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),'checkpointTraining':metadata,
        'newTrainingUpdates':0,'modelNamesAsFeatures':False,'absoluteCoordinatesAsFeatures':False,
        'seed':args.seed,'vocabulary':args.vocabulary,'budget':args.budget,'secondsLimit':args.seconds,
        'initializationSeconds':init_seconds,'actionSeconds':action_seconds,'wallSeconds':time.monotonic()-started,
        'initialVisual':native.initial['visual'],'initialIndividualVisual':native.initial['individualVisual'],
        'attempts':attempts,'accepted':accepted,'bestRank':best_rank,'final':stats,'reasons':dict(reasons),
        'identityActions':0,'sparseScoring':True,'scaleFeatureMaxError':error,
        'heuristicSearchCalls':0,'geometryRepairCalls':0,'sourceInputs':{name:digest(args.directory/name) for name in INPUT_FILES},
        'observationsSha256':digest(args.out/'observations.npz'),'actionsSha256':digest(args.out/'actions.jsonl'),
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in
            ['run_anchor_pair_policy.py','learn_pair_policy.py','joint_anchor_ray_policy.py','joint_neural_ports.py','joint_grouped_routes.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','attempts','accepted','identityActions','initialVisual','reasons',
        'initializationSeconds','actionSeconds','wallSeconds','scaleFeatureMaxError']}|{'finalVisual':stats['visual']}),flush=True)


def replay(args):
    report=json.loads((args.out/'report.json').read_text());directory=Path(report['sourceDirectory'])
    assert digest(report['checkpoint'])==report['checkpointSha256']
    for name,expected in report['sourceInputs'].items():assert digest(directory/name)==expected
    for key,name in [('observationsSha256','observations.npz'),('actionsSha256','actions.jsonl')]:assert digest(args.out/name)==report[key]
    for name,expected in report['codeSha256'].items():assert digest(Path(__file__).parent/name)==expected
    decoder=PairDecoder(directory);model,_=load(Path(report['checkpoint']))
    with np.load(args.out/'observations.npz',allow_pickle=False) as saved:
        vocabulary=decoder.vocabulary(report['seed'],report['vocabulary']);assert np.array_equal(vocabulary,saved['pairs'])
        inputs,error=pair_features(saved['node_features'],decoder,vocabulary,report['view']=='overview')
        assert np.array_equal(inputs,saved['pair_features'])
        scores=model.forward(inputs)[0];assert np.array_equal(scores,saved['scores'])
    order=np.argsort(-scores,kind='stable');count=accepted=0;best=None
    for line in (args.out/'actions.jsonl').read_text().splitlines():
        row=json.loads(line);assert row['rank']==count;index=order[count];n,m=map(int,vocabulary[index])
        assert (n,m)==(row['source'],row['target']) and scores[index]==row['score']
        action=decoder.action(n,m);assert hashlib.sha256(wire(action).encode()).hexdigest()==row['wireSha256']
        if row['result']['accepted']:best=action;accepted+=1
        count+=1
    assert count==report['attempts'] and accepted==report['accepted']
    expected=decoder.positions.copy() if best is None else decoder.positions+best[:len(decoder.positions)]
    actual=np.array(list(pairs(args.out/'learned.tsv').values()))
    assert np.max(abs(expected-actual))<1e-8
    print(json.dumps({'frozenPairRankingReplay':'pass','actionWireReplay':'pass','savedPositionsReplay':'pass',
        'actions':count,'accepted':accepted,'nativeResultsRecomputed':False,'newTrainingUpdates':0}))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['run','replay'])
    parser.add_argument('--directory',type=Path);parser.add_argument('--environment',type=Path)
    parser.add_argument('--checkpoint',type=Path);parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--view',choices=['individual','overview']);parser.add_argument('--seed',type=int,default=98337)
    parser.add_argument('--vocabulary',type=int,default=4096);parser.add_argument('--budget',type=int,default=512)
    parser.add_argument('--seconds',type=float,default=20)
    args=parser.parse_args()
    (run if args.mode=='run' else replay)(args)
