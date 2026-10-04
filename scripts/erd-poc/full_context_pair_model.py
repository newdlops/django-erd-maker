"""Full native node context for a shared, coordinate-free neural pair critic."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from learn_pair_policy import Ranker,KEYS,load as load_old
from run_anchor_pair_policy import PairDecoder,pair_features,wire,digest
from train_anchor_pair_outcomes import outcome_target,regression
from learned_global_replay import INPUT_FILES

SCHEMA='relative-pair-full-node-context-v2-136'
EXTRA=np.r_[24:56,60:64]


def expand(base,nodes,vocabulary):
    a,b=vocabulary.T
    result=np.concatenate([base,nodes[a][:,EXTRA],nodes[b][:,EXTRA]],axis=1)
    assert result.shape==(len(vocabulary),136) and np.isfinite(result).all()
    return result


def pressure(nodes,vocabulary):
    return np.any(nodes[vocabulary][:,:,4:6]>0,axis=(1,2))


def load(path):
    with np.load(path,allow_pickle=False) as data:
        metadata=json.loads(str(data['metadata']));assert metadata['schema']==SCHEMA and metadata['selectedCheckpointUpdates']>0
        model=Ranker(features=136);model.p={key:data[key].copy() for key in KEYS}
        model.mean=data['mean'].copy();model.scale=data['scale'].copy()
    assert model.p['w1'].shape==(136,64) and model.mean.shape==model.scale.shape==(136,)
    assert all(np.isfinite(v).all() for v in model.p.values()) and np.isfinite(model.mean).all() and (model.scale>0).all()
    return model,metadata


def warm(parent,x,training):
    model=Ranker(features=136)
    model.p={k:(np.vstack([v,np.zeros((72,v.shape[1]))]) if k=='w1' else v.copy()) for k,v in parent.p.items()}
    model.mean=np.r_[parent.mean,x[training,64:].mean(0)]
    model.scale=np.r_[parent.scale,np.maximum(.2,x[training,64:].std(0))]
    error=float(np.max(abs(model.forward(x)[0]-parent.forward(x[:,:64])[0])))
    assert error<1e-10
    return model,error


def collect(stages):
    records={};sources=[];view_sources={}
    for stage in stages:
        report=json.loads((stage/'report.json').read_text());directory=Path(report['sourceDirectory'])
        assert report['kind']=='frozen-trained-pair-ranker-with-equal-size-anchor-decoder-v1'
        assert digest(stage/'actions.jsonl')==report['actionsSha256'] and digest(stage/'observations.npz')==report['observationsSha256']
        assert digest(report['checkpoint'])==report['checkpointSha256']
        inputs={name:digest(directory/name) for name in INPUT_FILES};assert inputs==report['sourceInputs']
        if report['view'] in view_sources:assert inputs==view_sources[report['view']]
        view_sources[report['view']]=inputs
        decoder=PairDecoder(directory);old,_=load_old(Path(report['checkpoint']))
        with np.load(stage/'observations.npz',allow_pickle=False) as saved:
            nodes=saved['node_features'].copy();vocabulary=saved['pairs'].copy()
            base,_=pair_features(nodes,decoder,vocabulary,report['view']=='overview')
            assert np.array_equal(base,saved['pair_features'])
            scores=old.forward(base)[0];assert np.array_equal(scores,saved['scores'])
        full=expand(base,nodes,vocabulary);active=pressure(nodes,vocabulary);order=np.argsort(-scores,kind='stable')
        stage_id=len(sources)
        for line in (stage/'actions.jsonl').read_text().splitlines():
            row=json.loads(line);index=order[row['rank']];a,b=map(int,vocabulary[index])
            assert (a,b)==(row['source'],row['target'])
            assert hashlib.sha256(wire(decoder.action(a,b)).encode()).hexdigest()==row['wireSha256']
            key=(report['view'],a,b);target=outcome_target(row['result'],report['initialVisual'])
            example={'x':full[index].copy(),'y':target,'active':bool(active[index]),'sources':[stage_id],
                'actualGain':report['initialVisual']-row['result']['visual'] if row['result']['legal'] else None}
            if key in records:
                prior=records[key];assert np.array_equal(prior['x'],example['x']) and prior['y']==target
                prior['sources'].append(stage_id)
            else:records[key]=example
        sources.append({'stage':str(stage),'reportSha256':digest(stage/'report.json'),
            'observationsSha256':report['observationsSha256'],'actionsSha256':report['actionsSha256']})
    keys=sorted(records);rows=[records[key] for key in keys]
    return {'x':np.array([r['x'] for r in rows]),'y':np.array([r['y'] for r in rows]),
        'active':np.array([r['active'] for r in rows]),'view':np.array([key[0] for key in keys]),
        'pairs':np.array([key[1:] for key in keys],dtype=np.int32),
        'actual_gain':np.array([r['actualGain'] if r['actualGain'] is not None else np.nan for r in rows])},rows,sources


def train(args):
    assert 0<args.seconds<=20 and 1<=args.epochs<=100
    args.out.mkdir(parents=True,exist_ok=False)
    parent,parent_metadata=load_old(args.parent)
    data,rows,sources=collect(args.stages);x=data['x'];y=data['y'];active=data['active'];views=data['view']
    assert 32<=len(x)<=4096
    previously_seen_stages={row['stage'] for row in parent_metadata['sourceStages']}
    prior_ids={i for i,row in enumerate(sources) if row['stage'] in previously_seen_stages}
    assert prior_ids and len(prior_ids)<len(sources)
    novel=np.array([not any(i in prior_ids for i in row['sources']) for row in rows])
    rng=np.random.default_rng(args.seed);validation=[]
    for view in sorted(set(views)):
        for value in [False,True]:
            available=np.flatnonzero((views==view)&(active==value)&novel)
            assert len(available)>=4,('insufficient novel validation actions',view,value,len(available))
            validation.extend(rng.permutation(available)[:max(1,int(.25*len(available)))])
    validation=np.array(sorted(validation));training=np.setdiff1d(np.arange(len(x)),validation)
    assert novel[validation].all() and active[validation].any()
    model,warm_error=warm(parent,x,training)
    # Four-fold sampling of active-conflict examples; each gradient still
    # optimizes the exact recorded outcome target, without coordinate labels.
    sampling=np.r_[training,np.tile(training[active[training]],3)]
    active_validation=validation[active[validation]]
    data|={'training':training,'validation':validation,'novel_to_parent':novel,'sampling':sampling}
    np.savez_compressed(args.out/'dataset.npz',**data)
    initial={k:v.copy() for k,v in model.p.items()}
    first={k:np.zeros_like(v) for k,v in model.p.items()};second={k:v.copy() for k,v in first.items()}
    initial_loss=regression(model,x[active_validation],y[active_validation]);best_loss=initial_loss;best=None
    updates=0;history=[];started=time.monotonic()
    for epoch in range(args.epochs):
        order=rng.permutation(sampling)
        for offset in range(0,len(order),128):
            batch=order[offset:offset+128];_,grads=regression(model,x[batch],y[batch],True)
            norm=np.sqrt(sum(np.sum(g*g) for g in grads.values()));updates+=1
            for key in KEYS:
                gradient=grads[key]*min(1,5/max(norm,1e-12))
                first[key]=.9*first[key]+.1*gradient;second[key]=.999*second[key]+.001*gradient*gradient
                model.p[key]-=.001*(first[key]/(1-.9**updates))/(np.sqrt(second[key]/(1-.999**updates))+1e-8)
            if time.monotonic()-started>=args.seconds:break
        loss=regression(model,x[active_validation],y[active_validation])
        history.append({'epoch':epoch+1,'updates':updates,'activeValidationHuber':loss,
            'allValidationHuber':regression(model,x[validation],y[validation])})
        if loss<best_loss:
            best_loss=loss;best={k:v.copy() for k,v in model.p.items()};best_epoch=epoch+1;best_updates=updates
        if time.monotonic()-started>=args.seconds:break
    assert best is not None and np.linalg.norm(best['w1'][64:])>0
    metadata={'schema':SCHEMA,'kind':'full-native-context-pair-outcome-regressor','features':136,
        'parent':str(args.parent),'parentSha256':digest(args.parent),'sourceStages':sources,'seed':args.seed,
        'newTrainingUpdatesExecuted':updates,'selectedCheckpointUpdates':best_updates,'selectedEpoch':best_epoch,
        'omittedNodeFeatureIndicesAdded':EXTRA.tolist(),'activeSamplingMultiplicity':4,
        'validationScope':'novel pairs absent from parent training stages, held out from this adaptation, on the same two source layouts',
        'rows':len(x),'activeRows':int(active.sum()),'validationRows':len(validation),'activeValidationRows':len(active_validation),
        'actualImprovementExamples':int(np.sum(data['actual_gain']>0)),
        'initialActiveValidationHuber':initial_loss,'bestActiveValidationHuber':best_loss,
        'warmPredictionMaxDifference':warm_error,'modelNamesAsFeatures':False,'absoluteCoordinatesAsFeatures':False,
        'datasetSha256':digest(args.out/'dataset.npz'),'seconds':time.monotonic()-started,
        'parameterCount':sum(p.size for p in best.values()),'scriptSha256':digest(__file__)}
    model.p=best
    np.savez_compressed(args.out/'model.npz',**best,**{'initial__'+k:v for k,v in initial.items()},
        mean=model.mean,scale=model.scale,metadata=json.dumps(metadata))
    restored,_=load(args.out/'model.npz');assert np.array_equal(restored.forward(x)[0],model.forward(x)[0])
    report=metadata|{'checkpointSha256':digest(args.out/'model.npz'),'history':history}
    (args.out/'training.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['rows','activeRows','validationRows','activeValidationRows','parameterCount',
        'newTrainingUpdatesExecuted','selectedCheckpointUpdates','actualImprovementExamples','initialActiveValidationHuber',
        'bestActiveValidationHuber','warmPredictionMaxDifference','seconds','checkpointSha256']}))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--parent',required=True,type=Path);p.add_argument('--stages',nargs='+',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);p.add_argument('--epochs',type=int,default=80)
    p.add_argument('--seconds',type=float,default=10);p.add_argument('--seed',type=int,default=84018);train(p.parse_args())
