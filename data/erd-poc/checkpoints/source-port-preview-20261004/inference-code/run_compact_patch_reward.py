#!/usr/bin/env python3
"""Train a small shared output head from pure Native measurement rewards."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from compact_patch_neural_policy import PatchActor
from geometry_world_model import digest
from joint_reward_training import head_vector,set_head_vector,head_hash,adam_head
from run_anchor_pair_walk import WalkDecoder,verify_saved
from run_anchor_pair_policy import Native,wire
from learned_global_replay import INPUT_FILES

KEYS=('wo','bo'); SCALES=(1.,1.)


def objective(result):
    assert result['measureOnly'] and not result['accepted']
    return float(result['visual']+100*result['spacing']+50*(result['hard']+result['individualHard']))


def patch_support(decoder,nodes,count,root_rank):
    active=np.flatnonzero(np.any(nodes[:,4:6]>0,axis=1))
    priority=np.expm1(nodes[active,4])+np.expm1(nodes[active,5])
    root=int(active[np.lexsort((active,-priority))[root_rank]])
    ids=np.arange(len(decoder.positions)); distance=np.rint(np.sum((decoder.positions-decoder.positions[root])**2,axis=1)*1e6)
    others=ids[ids!=root]; near=others[np.lexsort((others,distance[others]))[:count-1]]
    return np.r_[root,near].astype(np.int32),{'kind':'source conflicting-root vocabulary and nearest physical owners',
        'rootRank':root_rank,'root':root,'owners':np.r_[root,near].tolist(),'sourcePriority':float(priority[np.flatnonzero(active==root)[0]]),
        'supportEmitsNoDisplacements':True,'futureGeometryUsedInSupport':False}


def run(args):
    assert 1<=args.patch_size<=16 and 1<=args.iterations<=256 and 0<args.seconds<=30
    args.out.mkdir(parents=True,exist_ok=False); started=time.monotonic()
    spec=json.loads(args.source_binding.read_text())['viewSources'][args.view]
    inputs={n:digest(args.directory/n) for n in INPUT_FILES}; assert str(args.directory)==spec['directory'] and inputs==spec['inputs']
    decoder=WalkDecoder(args.directory); native=Native(args.environment,args.directory,args.out/'learned.tsv',args.view=='overview',True)
    best=None; best_step=None; attempts=accepted=updates=probes=0; reasons=Counter(); learned_changed=False
    history=[]; best_visual=native.initial['visual']
    try:
        assert best_visual==spec['expectedVisual']
        nodes=np.array([n['features'] for n in native.initial['nodes']]); active,support=patch_support(decoder,nodes,args.patch_size,args.root_rank)
        model=PatchActor(nodes,decoder,active,args.max_step,args.seed)
        np.savez_compressed(args.out/'observations.npz',nodes=nodes,active=active)
        model.save(args.out/'initial-model.npz',{'kind':'compact-patch-initial-encoder','trainedUpdates':0})
        baseline=native.request(wire(model.forward(nodes)[0],'MEASURE'))
        assert baseline['legal'] and baseline['hard']==baseline['individualHard']==baseline['spacing']==0
        assert baseline['visual']==best_visual
        base=head_vector(model,KEYS,SCALES); first=np.zeros_like(base); second=first.copy(); sigma=args.sigma
        train_started=time.monotonic()
        with (args.out/'training.jsonl').open('x') as trace,(args.out/'actions.jsonl').open('x') as actions:
            for step in range(1,args.iterations+1):
                if time.monotonic()-train_started>=args.seconds: break
                base=head_vector(model,KEYS,SCALES); gradient=np.zeros_like(base); samples=[]
                for direction in range(2):
                    seed=args.seed+step*100+direction; noise=np.random.default_rng(seed).normal(size=base.shape); values=[]; pair=[]
                    for sign in (1.,-1.):
                        set_head_vector(model,base+sign*sigma*noise,KEYS,SCALES)
                        command=wire(model.forward(nodes)[0],'MEASURE'); result=native.request(command); probes+=1
                        values.append(objective(result)); pair.append({'sign':sign,'wireSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result})
                    gradient+=(values[0]-values[1])/(4*sigma)*noise
                    samples.append({'seed':seed,'samples':pair})
                set_head_vector(model,base,KEYS,SCALES)
                head,first,second=adam_head(base,gradient,first,second,step,args.rate)
                set_head_vector(model,head,KEYS,SCALES); updates+=1; changed=not np.array_equal(base,head); learned_changed|=changed
                row={'step':step,'sigma':sigma,'rate':args.rate,'baseHeadSha256':head_hash(base),'samples':samples,
                    'gradientNorm':float(np.linalg.norm(gradient)),'trainedHeadSha256':head_hash(head),'headChanged':changed}
                trace.write(json.dumps(row)+'\n'); trace.flush()
                if step%4==0:
                    proposed=model.forward(nodes)[0]; command=wire(proposed); result=native.request(command)
                    attempts+=1; reasons[result['reason']]+=1
                    checkpoint=args.out/f'model-step-{step:03}.npz'; model.save(checkpoint,{'kind':'compact-patch-native-reward-trained','trainedUpdates':step})
                    record={'step':step,'checkpoint':str(checkpoint),'checkpointSha256':digest(checkpoint),
                        'wireSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result}
                    actions.write(json.dumps(record)+'\n'); actions.flush()
                    if result['accepted']: best=proposed.copy(); best_step=step; best_visual=result['visual']; accepted+=1
                if not learned_changed: sigma=min(.05,sigma*2)
                history.append({'step':step,'headChanged':changed,'gradientNorm':row['gradientNorm']})
        assert native.request('SAVE')['saved']
    finally: native.close()
    verify_saved(args.directory,args.out,decoder,best)
    if best is not None: np.save(args.out/'best-action.npy',best)
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'],stats['policyActionsEvaluated'],stats['acceptedActions'])==(best_visual,attempts,accepted)
    assert stats['hardConditions']==stats['individualHardConditions']==stats['overlap']==stats['spacing']==0
    report={'kind':'compact-source-spacing-patch-native-reward-learning-v1','view':args.view,'seed':args.seed,
        'sourceDirectory':str(args.directory),'sourceBinding':str(args.source_binding),'sourceBindingSha256':digest(args.source_binding),
        'sourceInputs':inputs,'environment':str(args.environment),'environmentSha256':digest(args.environment),
        'support':support,'patchSize':args.patch_size,'rootRank':args.root_rank,'maxStep':args.max_step,
        'initialSigma':args.sigma,'rate':args.rate,'directionsPerUpdate':2,'trainableParameters':18,
        'parameterKeys':KEYS,'parameterScales':SCALES,'encoderFixed':True,'decoderBuffersImmutable':True,
        'iterationsLimit':args.iterations,'secondsLimit':args.seconds,'updatesExecuted':updates,'rewardProbes':probes,
        'learnedHeadChanged':learned_changed,'attempts':attempts,'accepted':accepted,'bestStep':best_step,'reasons':dict(reasons),
        'initialVisual':baseline['visual'],'initialIndividualVisual':baseline['individualVisual'],'final':stats,
        'nativeProbesOnlySupplyLearningLabels':True,'cardCoordinatesAreTrainableParameters':False,
        'nativeCoordinateSearchOrRepairs':0,'allSavedGeometryFromTrainedNetworkForward':True,
        'sourceFrameAndOriginalCardSizesRetained':True,'inferenceFeatureInputDtype':'float32',
        'trainingSha256':digest(args.out/'training.jsonl'),'actionsSha256':digest(args.out/'actions.jsonl'),
        'observationsSha256':digest(args.out/'observations.npz'),'initialModelSha256':digest(args.out/'initial-model.npz'),
        'wallSeconds':time.monotonic()-started,'codeSha256':{name:digest(Path(__file__).parent/name) for name in
            ['run_compact_patch_reward.py','compact_patch_neural_policy.py','joint_reward_training.py',
             'joint_separation_policy.py','joint_anchor_ray_policy.py','run_anchor_pair_walk.py','run_anchor_pair_policy.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','support','updatesExecuted','rewardProbes','learnedHeadChanged',
        'attempts','accepted','bestStep','reasons','wallSeconds']}|{'initialVisual':baseline['visual'],'finalVisual':best_visual}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--source-binding',type=Path,required=True); p.add_argument('--directory',type=Path,required=True)
    p.add_argument('--environment',type=Path,required=True); p.add_argument('--view',choices=['overview','individual'],required=True)
    p.add_argument('--out',type=Path,required=True); p.add_argument('--seed',type=int,default=107103)
    p.add_argument('--patch-size',type=int,default=8); p.add_argument('--root-rank',type=int,default=0)
    p.add_argument('--max-step',type=float,default=256.); p.add_argument('--sigma',type=float,default=.005)
    p.add_argument('--rate',type=float,default=.001); p.add_argument('--iterations',type=int,default=128)
    p.add_argument('--seconds',type=float,default=30.); run(p.parse_args())
