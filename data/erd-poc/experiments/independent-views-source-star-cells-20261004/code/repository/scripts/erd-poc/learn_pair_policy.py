#!/usr/bin/env python3
"""Frozen neural policy over uniformly sampled card-pair actions; NumPy only."""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import time

import numpy as np
from learn_card_policy import digest, sigmoid
from learned_global_replay import INPUT_FILES, PairSwapReplay

SCHEMA = 'relative-pair-swap-context-v1-64'
KEYS = ('w1','b1','w2','b2','wo','bo')


class Ranker:
    def __init__(self, seed=37, features=64, hidden=64):
        rng=np.random.default_rng(seed)
        self.p={'w1':rng.normal(0,1/math.sqrt(features),(features,hidden)), 'b1':np.zeros(hidden),
                'w2':rng.normal(0,1/math.sqrt(hidden),(hidden,hidden)), 'b2':np.zeros(hidden),
                'wo':rng.normal(0,.02,hidden), 'bo':np.zeros(1)}
        self.mean=np.zeros(features);self.scale=np.ones(features)

    def forward(self, features):
        x=np.clip((features-self.mean)/self.scale,-8,8)
        h1=np.tanh(x@self.p['w1']+self.p['b1']);h2=np.tanh(h1@self.p['w2']+self.p['b2'])
        return h2@self.p['wo']+self.p['bo'],(x,h1,h2)

    def loss(self, features, gains, weights, gradients=False):
        z,(x,h1,h2)=self.forward(features);positive=(gains>0).astype(float)
        weights=weights/weights.sum()
        loss=float(np.sum(weights*(np.logaddexp(0,z)-positive*z)))
        if not gradients:return loss
        dz=weights*(sigmoid(z)-positive);g2=dz[:,None]*self.p['wo'][None,:]*(1-h2*h2)
        g1=(g2@self.p['w2'].T)*(1-h1*h1)
        return loss,{'wo':h2.T@dz,'bo':np.array([dz.sum()]),'w2':h1.T@g2,'b2':g2.sum(0),'w1':x.T@g1,'b1':g1.sum(0)}


def self_test():
    rng=np.random.default_rng(7891);model=Ranker(7,5,6)
    features=rng.normal(size=(9,5));gains=np.array([0,0,1,2,0,5,0,1,0]);weights=np.maximum(1,gains).astype(float)
    _,grads=model.loss(features,gains,weights,True);largest=0;checked=0
    for key in KEYS:
        for flat in rng.choice(model.p[key].size,min(8,model.p[key].size),replace=False):
            index=np.unravel_index(flat,model.p[key].shape);value=model.p[key][index]
            model.p[key][index]=value+1e-5;hi=model.loss(features,gains,weights)
            model.p[key][index]=value-1e-5;lo=model.loss(features,gains,weights);model.p[key][index]=value
            error=abs((hi-lo)/2e-5-grads[key][index]);assert error<2e-6,(key,index,error)
            largest=max(largest,error);checked+=1
    print(json.dumps({'gradientChecks':checked,'maxAbsoluteError':largest,'status':'pass'}))


def train(args):
    rows=[];graphs=[];gains=[]
    with args.dataset.open() as stream:
        for line in stream:
            row=json.loads(line);assert len(row['features'])==64 and row['gain']>=0
            rows.append(row['features']);graphs.append(row['graph']);gains.append(row['gain'])
            assert len(rows)<=60000,'pair dataset memory budget'
    features=np.asarray(rows);del rows
    graphs=np.asarray(graphs);gains=np.asarray(gains,dtype=float);assert np.isfinite(features).all()
    ids=np.unique(graphs[graphs!=100000]);assert len(ids)>=8
    split=max(1,int(.8*len(ids)));training=np.isin(graphs,ids[:split]);adapt=graphs==100000
    if adapt.any():assert args.adapt_source and args.adapt_source.is_file();training|=adapt
    train_ids=np.flatnonzero(training);val_ids=np.flatnonzero(~training)
    assert gains[train_ids].sum()>0 and gains[val_ids].sum()>0
    weights=np.maximum(1,np.minimum(gains,args.gain_cap));weights[adapt&(gains>0)]*=args.adapt_weight
    model=Ranker(args.seed);model.mean=features[train_ids].mean(0);model.scale=np.maximum(.2,features[train_ids].std(0))
    initial={key:value.copy() for key,value in model.p.items()}
    first={key:np.zeros_like(value) for key,value in model.p.items()};second={key:value.copy() for key,value in first.items()}
    rng=np.random.default_rng(args.seed);started=time.monotonic();updates=0;best_loss=float('inf');best=None;history=[]
    initial_loss=model.loss(features[val_ids],gains[val_ids],weights[val_ids])
    for epoch in range(args.epochs):
        order=rng.permutation(train_ids)
        for offset in range(0,len(order),256):
            batch=order[offset:offset+256];loss,grads=model.loss(features[batch],gains[batch],weights[batch],True);assert math.isfinite(loss)
            norm=math.sqrt(sum(np.sum(g*g) for g in grads.values()));updates+=1
            for key in KEYS:
                g=grads[key]*min(1,5/max(norm,1e-12));first[key]=.9*first[key]+.1*g;second[key]=.999*second[key]+.001*g*g
                model.p[key]-=.001*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started>=args.seconds:break
        loss=model.loss(features[val_ids],gains[val_ids],weights[val_ids])
        record={'epoch':epoch+1,'updates':updates,'validationWeightedBce':loss,'seconds':time.monotonic()-started};history.append(record)
        if loss<best_loss:best_loss=loss;best={key:value.copy() for key,value in model.p.items()};best_epoch=epoch+1
        if epoch%5==0:print(json.dumps(record),flush=True)
        if time.monotonic()-started>=args.seconds:break
    assert best is not None;model.p=best
    scores=model.forward(features[val_ids])[0];count=max(1,len(scores)//10);top=np.argsort(-scores,kind='stable')[:count]
    metadata={'kind':'pair-swap-ranker-v1','schema':SCHEMA,'features':64,'trainedUpdates':updates,'bestEpoch':best_epoch,
              'seed':args.seed,'gainCap':args.gain_cap,'datasetSha256':digest(args.dataset),'trainingGraphIds':ids[:split].tolist()+([100000] if adapt.any() else []),
              'validationGraphIds':ids[split:].tolist(),'captainTrainingExamples':int(adapt.sum()),
              'adaptationSourceSha256':digest(args.adapt_source) if adapt.any() else None,'adaptationWeight':args.adapt_weight if adapt.any() else None,
              'trainingData':'synthetic pair rewards'+(' plus Captain pair rewards' if adapt.any() else ' only'),
              'absoluteCoordinatesAsInput':False,'modelNamesAsInput':False,'actionSelection':'descending trained score over a uniformly sampled pair vocabulary'}
    args.checkpoint.parent.mkdir(parents=True,exist_ok=True);assert not args.checkpoint.exists()
    np.savez_compressed(args.checkpoint,**best,**{'initial__'+k:v for k,v in initial.items()},mean=model.mean,scale=model.scale,metadata=json.dumps(metadata))
    report={**metadata,'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),'rows':len(features),'positiveRows':int((gains>0).sum()),
            'parameterCount':sum(p.size for p in best.values()),'initialValidationLoss':initial_loss,'bestValidationLoss':best_loss,
            'validationTopDecileGainLift':float(gains[val_ids[top]].mean()/gains[val_ids].mean()),
            'validationTopDecilePositiveRate':float((gains[val_ids[top]]>0).mean()),'seconds':time.monotonic()-started,'history':history}
    args.checkpoint.with_suffix('.training.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['history','trainingGraphIds','validationGraphIds']}),flush=True)


def load(path,untrained=False):
    with np.load(path,allow_pickle=False) as data:
        metadata=json.loads(str(data['metadata']));assert metadata['schema']==SCHEMA and metadata['trainedUpdates']>0
        model=Ranker();model.p={key:data[('initial__' if untrained else '')+key].copy() for key in KEYS}
        model.mean=data['mean'].copy();model.scale=data['scale'].copy()
    return model,metadata


def infer(args):
    assert 1<=args.budget<=2048 and 1<=args.observations<=4096 and 0<args.seconds<=20
    model,metadata=load(args.checkpoint,args.untrained);args.out.parent.mkdir(parents=True,exist_ok=True)
    if args.overview_only:assert args.out.resolve().is_relative_to(Path(__file__).resolve().parents[2]/'.tmp')
    child=subprocess.Popen([str(args.environment),'--directory',str(args.directory),'--out',str(args.out),'--seed',str(args.seed),
                            '--overview-only','1' if args.overview_only else '0'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
    def receive():
        text=child.stdout.readline()
        if not text:raise RuntimeError(f'pair environment exited: {child.poll()}')
        return json.loads(text)
    def request(text):child.stdin.write(text+'\n');child.stdin.flush();return receive()
    started=time.monotonic();attempts=accepted=0
    try:
        initial=receive();assert initial['ready']
        with Path(str(args.out)+'.observations.jsonl').open('w') as observations,Path(str(args.out)+'.actions.jsonl').open('w') as actions:
            for round_id in range(args.rounds):
                if attempts>=args.budget or time.monotonic()-started>=args.seconds:break
                state=request(f'OBS {args.observations}');observations.write(json.dumps({'round':round_id,**state})+'\n');observations.flush()
                scores=model.forward(np.asarray([row['features'] for row in state['pairs']]))[0]
                order=np.argsort(-scores,kind='stable')
                for rank,index in enumerate(order[:args.per_round]):
                    if attempts>=args.budget or time.monotonic()-started>=args.seconds:break
                    row=state['pairs'][index];result=request(f"TRY {row['source']} {row['target']}");attempts+=1;accepted+=int(result['accepted'])
                    actions.write(json.dumps({'round':round_id,'rank':rank,'source':row['source'],'target':row['target'],'score':float(scores[index]),'result':result})+'\n')
                    if result['accepted']:break
                print(json.dumps({'round':round_id,'attempts':attempts,'accepted':accepted,'seconds':time.monotonic()-started}),flush=True)
        assert request('SAVE')['saved'];child.stdin.write('QUIT\n');child.stdin.flush();assert child.wait(timeout=5)==0
    finally:
        if child.poll() is None:child.kill();child.wait()
    stats=json.loads(Path(str(args.out)+'.stats.json').read_text())
    assert stats['pairActions'] and stats['swapSlots'] and stats['overviewOnly']==args.overview_only
    assert stats['policyActionsEvaluated']==attempts and stats['acceptedActions']==accepted and stats['heuristicSearchCalls']==0
    report={'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),'untrainedControl':args.untrained,
            'sourceDirectory':str(args.directory),'policyProposalSource':'trained neural pair ranker','heuristicSearchCalls':0,'overviewOnly':args.overview_only,
            'initial':initial,'final':stats,'budget':args.budget,'seed':args.seed,'seconds':time.monotonic()-started,
            'observationsSha256':digest(str(args.out)+'.observations.jsonl'),'actionsSha256':digest(str(args.out)+'.actions.jsonl'),
            'pairInputHashes':{name:digest(args.directory/name) for name in INPUT_FILES}}
    Path(str(args.out)+'.policy.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)


def replay(args):
    report=json.loads(Path(str(args.out)+'.policy.json').read_text());assert digest(args.checkpoint)==report['checkpointSha256']
    for suffix,key in [('actions','actionsSha256'),('observations','observationsSha256')]:assert digest(str(args.out)+'.'+suffix+'.jsonl')==report[key]
    directory=Path(report['sourceDirectory'])
    for name,sha in report['pairInputHashes'].items():assert digest(directory/name)==sha
    model,metadata=load(args.checkpoint,report['untrainedControl']);geometry=PairSwapReplay(directory,1.5e9)
    observations={};rank_next={}
    with Path(str(args.out)+'.observations.jsonl').open() as stream:
        for line in stream:
            row=json.loads(line);scores=model.forward(np.asarray([pair['features'] for pair in row['pairs']]))[0]
            observations[row['round']]=([(pair['source'],pair['target']) for pair in row['pairs']],scores,np.argsort(-scores,kind='stable'));rank_next[row['round']]=0
    checked=0;accepted=0
    with Path(str(args.out)+'.actions.jsonl').open() as stream:
        for line in stream:
            action=json.loads(line);pairs,scores,order=observations[action['round']];assert action['rank']==rank_next[action['round']]
            rank_next[action['round']]+=1;index=order[action['rank']];pair=pairs[index]
            assert pair==(action['source'],action['target'])
            assert abs(float(scores[index])-action['score'])<1e-9 and action['result']['decodedTarget']==action['target']
            if action['result']['accepted']:geometry.apply_pair(action['source'],action['target']);accepted+=1;rank_next[action['round']]=-1
            checked+=1
    assert checked==report['final']['policyActionsEvaluated'] and accepted==report['final']['acceptedActions'];geometry.verify(args.out)
    print(json.dumps({'frozenCheckpointActionReplay':'pass','actions':checked,'accepted':accepted,'neuralPairRankingReplay':True,'pairPositionsAndRoutesReplay':True,'untrainedControl':report['untrainedControl']}))


def main():
    parser=argparse.ArgumentParser();sub=parser.add_subparsers(dest='command',required=True);sub.add_parser('self-test')
    p=sub.add_parser('train');p.add_argument('--dataset',type=Path,required=True);p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--epochs',type=int,default=30);p.add_argument('--seconds',type=float,default=15);p.add_argument('--seed',type=int,default=241)
    p.add_argument('--gain-cap',type=float,default=32);p.add_argument('--adapt-source',type=Path);p.add_argument('--adapt-weight',type=float,default=16)
    p=sub.add_parser('infer');p.add_argument('--checkpoint',type=Path,required=True);p.add_argument('--environment',type=Path,required=True)
    p.add_argument('--directory',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--budget',type=int,default=512);p.add_argument('--rounds',type=int,default=16);p.add_argument('--observations',type=int,default=2048)
    p.add_argument('--per-round',type=int,default=128);p.add_argument('--seconds',type=float,default=20);p.add_argument('--seed',type=int,default=173)
    p.add_argument('--overview-only',action='store_true');p.add_argument('--untrained',action='store_true')
    p=sub.add_parser('replay');p.add_argument('--checkpoint',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if os.environ.get('OMP_NUM_THREADS')!='1':parser.error('run under run_memory_bounded.py')
    if args.command=='self-test':self_test()
    elif args.command=='train':train(args)
    elif args.command=='infer':infer(args)
    else:replay(args)


if __name__=='__main__':main()
