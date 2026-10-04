#!/usr/bin/env python3
"""Adapt shared pair-network weights to measured anchor-decoder outcomes.

Native results are labels only. No positions are trained or selected here.
Validation holds out actions on the same layouts, not unseen graphs.
"""
import argparse
import json
from pathlib import Path
import time
import numpy as np
from learn_pair_policy import Ranker,KEYS,SCHEMA,load
from run_anchor_pair_policy import digest


def outcome_target(row,baseline):
    value=float(np.clip((baseline-row['visual'])/64,-8,8))
    if not row['legal']:
        value=-8-min(8,np.log1p(sum(row[k] for k in ['hard','individualHard','spacing'])))
    return value


def regression(model,features,targets,gradient=False):
    predicted,(x,h1,h2)=model.forward(features);error=predicted-targets
    loss=float(np.mean(np.where(abs(error)<=1,.5*error**2,abs(error)-.5)))
    if not gradient:return loss
    dz=np.clip(error,-1,1)/len(error)
    g2=dz[:,None]*model.p['wo'][None,:]*(1-h2*h2)
    g1=(g2@model.p['w2'].T)*(1-h1*h1)
    return loss,{'wo':h2.T@dz,'bo':np.array([dz.sum()]),'w2':h1.T@g2,'b2':g2.sum(0),'w1':x.T@g1,'b1':g1.sum(0)}


def self_test():
    rng=np.random.default_rng(78331);model=Ranker(19,5,6)
    x=rng.normal(size=(9,5));targets=np.array([-8.,-2.,-.2,0.,.1,2.,3.,.4,-.8])
    _,grads=regression(model,x,targets,True);largest=0;count=0
    for key in KEYS:
        for flat in rng.choice(model.p[key].size,min(6,model.p[key].size),replace=False):
            index=np.unravel_index(flat,model.p[key].shape);value=model.p[key][index]
            model.p[key][index]=value+1e-5;hi=regression(model,x,targets)
            model.p[key][index]=value-1e-5;lo=regression(model,x,targets);model.p[key][index]=value
            error=abs((hi-lo)/2e-5-grads[key][index]);assert error<2e-7
            largest=max(largest,error);count+=1
    print(json.dumps({'huberGradientCheck':'pass','comparisons':count,'maxError':largest}))


def train(args):
    assert 0<args.seconds<=20 and 1<=args.epochs<=100
    model,original_metadata=load(args.checkpoint)
    initial={k:v.copy() for k,v in model.p.items()};features=[];targets=[];views=[];true_improvements=0;inputs=[]
    for stage in args.stages:
        report=json.loads((stage/'report.json').read_text())
        assert report['checkpointSha256']==digest(args.checkpoint)
        assert digest(stage/'actions.jsonl')==report['actionsSha256']
        assert digest(stage/'observations.npz')==report['observationsSha256']
        with np.load(stage/'observations.npz',allow_pickle=False) as saved:
            f=saved['pair_features'].copy();scores=model.forward(f)[0]
            assert np.array_equal(scores,saved['scores']);order=np.argsort(-scores,kind='stable')
        for line in (stage/'actions.jsonl').read_text().splitlines():
            row=json.loads(line);features.append(f[order[row['rank']]]);views.append(report['view'])
            targets.append(outcome_target(row['result'],report['initialVisual']))
            true_improvements+=row['result']['legal'] and row['result']['visual']<report['initialVisual']
        inputs.append({'stage':str(stage),'reportSha256':digest(stage/'report.json'),
            'actionsSha256':report['actionsSha256'],'observationsSha256':report['observationsSha256']})
    x=np.array(features);y=np.array(targets);views=np.array(views);assert 32<=len(y)<=4096
    rng=np.random.default_rng(args.seed);train_ids=[];validation_ids=[]
    for view in sorted(set(views)):
        ids=rng.permutation(np.flatnonzero(views==view));split=max(1,int(.8*len(ids)))
        train_ids.extend(ids[:split]);validation_ids.extend(ids[split:])
    train_ids=np.array(train_ids);validation_ids=np.array(validation_ids)
    initial_loss=regression(model,x[validation_ids],y[validation_ids])
    first={k:np.zeros_like(v) for k,v in model.p.items()};second={k:v.copy() for k,v in first.items()}
    updates=0;best_loss=initial_loss;best={k:v.copy() for k,v in initial.items()};best_epoch=0;history=[]
    started=time.monotonic()
    for epoch in range(args.epochs):
        ids=rng.permutation(train_ids)
        for offset in range(0,len(ids),128):
            batch=ids[offset:offset+128];loss,grads=regression(model,x[batch],y[batch],True)
            norm=np.sqrt(sum(np.sum(g*g) for g in grads.values()));updates+=1
            for k in KEYS:
                g=grads[k]*min(1,5/max(norm,1e-12));first[k]=.9*first[k]+.1*g;second[k]=.999*second[k]+.001*g*g
                model.p[k]-=.001*(first[k]/(1-.9**updates))/(np.sqrt(second[k]/(1-.999**updates))+1e-8)
            if time.monotonic()-started>=args.seconds:break
        score=regression(model,x[validation_ids],y[validation_ids]);history.append({'epoch':epoch+1,'updates':updates,'validationHuber':score})
        if score<best_loss:best_loss=score;best_epoch=epoch+1;best={k:v.copy() for k,v in model.p.items()}
        if time.monotonic()-started>=args.seconds:break
    assert best_epoch>0 and all(np.isfinite(p).all() for p in best.values())
    metadata={'kind':'anchor-pair-measured-outcome-regressor-v1','schema':SCHEMA,
        'trainedUpdates':original_metadata['trainedUpdates']+updates,'adaptationUpdates':updates,
        'pretrainedCheckpoint':str(args.checkpoint),'pretrainedCheckpointSha256':digest(args.checkpoint),
        'pretrainedMetadata':original_metadata,'features':64,'seed':args.seed,'bestEpoch':best_epoch,
        'target':'legal: clip((sourceVisual - measuredVisual)/64, -8, 8); illegal: -8 - min(8, log1p(hard + individualHard + spacing))',
        'trainingData':'frozen model proposals measured with equal-size anchor decoder',
        'validationScope':'held-out actions from the same two source layouts; not unseen graphs',
        'sourceStages':inputs,'absoluteCoordinatesAsInput':False,'modelNamesAsInput':False,
        'actionSelection':'descending trained outcome score over uniformly sampled equal-size pairs'}
    args.out.parent.mkdir(parents=True,exist_ok=True);assert not args.out.exists()
    np.savez_compressed(args.out,**best,**{'initial__'+k:v for k,v in initial.items()},mean=model.mean,scale=model.scale,metadata=json.dumps(metadata))
    reloaded,_=load(args.out);model.p=best
    assert np.array_equal(model.forward(x)[0],reloaded.forward(x)[0])
    report={**metadata,'rows':len(y),'trainingRows':len(train_ids),'validationRows':len(validation_ids),
        'actualImprovementExamples':int(true_improvements),'initialValidationHuber':initial_loss,'bestValidationHuber':best_loss,
        'seconds':time.monotonic()-started,'checkpointSha256':digest(args.out),'history':history,
        'trainingRowIndices':train_ids.tolist(),'validationRowIndices':validation_ids.tolist(),
        'scriptSha256':digest(__file__),'modelImplementationSha256':digest(Path(__file__).parent/'learn_pair_policy.py')}
    args.out.with_suffix('.training.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['rows','trainingRows','validationRows','adaptationUpdates','actualImprovementExamples',
        'initialValidationHuber','bestValidationHuber','bestEpoch','seconds','checkpointSha256']}))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--checkpoint',type=Path);parser.add_argument('--stages',nargs='+',type=Path)
    parser.add_argument('--out',type=Path);parser.add_argument('--epochs',type=int,default=40)
    parser.add_argument('--seconds',type=float,default=10);parser.add_argument('--seed',type=int,default=84017)
    args=parser.parse_args();self_test() if args.self_test else train(args)
