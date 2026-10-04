#!/usr/bin/env python3
"""Model-selected coordinated trajectories; Native only gates and keeps best."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace
import numpy as np
from calibrated_geometry_world import load,digest
from run_calibrated_macro_policy import UnpooledDecoder,vocabulary
from run_fraction_conditioned_policy import sample_order
from run_anchor_pair_policy import Native,wire
from run_anchor_pair_walk import verify_saved
from run_geometry_world_policy import source_geometry
from single_owner_cached_observer_v2 import CachedObserver,rounded
from learned_global_replay import INPUT_FILES


def transforms(width):
    return [(s,False) for s in ([1,2,-1] if width==4 else [1,2,4,-1,-2])]+[(0,True)]


def permutation_hash(p): return hashlib.sha256(np.asarray(p,dtype=np.int32).tobytes()).hexdigest()


def proposal_permutation(current,word,shift,reverse):
    word=np.asarray(word,dtype=np.int32); target=word[::-1] if reverse else np.roll(word,shift)
    proposed=current.copy(); proposed[word]=current[target]; return proposed


def decode(decoder,p):
    assert sorted(p.tolist())==list(range(len(p))) and np.array_equal(decoder.sizes[p],decoder.sizes)
    delta=rounded(decoder.positions[p]-decoder.positions)
    direction=decoder.ray_base_direction+delta[decoder.owner_edges[:,1]]-delta[decoder.owner_edges[:,0]]
    phase=decoder.ray_phase(direction)[0]
    return np.concatenate([delta,np.mod(phase-decoder.ray_initial_phase+.5,1.)-.5])


def run(args):
    assert 1<=args.rounds<=12 and 1<=args.words<=128 and 1<=args.per_round<=32 and 1<=args.budget<=256 and 0<args.seconds<=30
    args.out.mkdir(parents=True,exist_ok=False); started=time.monotonic()
    binding=json.loads(args.source_binding.read_text())['viewSources'][args.view]
    inputs={n:digest(args.directory/n) for n in INPUT_FILES}
    assert str(args.directory)==binding['directory'] and inputs==binding['inputs']
    decoder=UnpooledDecoder(args.directory); observer=CachedObserver(args.directory,decoder)
    proxy,model,metadata=load(args.checkpoint); current=np.arange(len(decoder.positions),dtype=np.int32)
    zero=decode(decoder,current); before,sizes,edges=source_geometry(observer,zero,args.view)
    native=Native(args.environment,args.directory,args.out/'learned.tsv',args.view=='overview',True)
    best=None; best_visual=native.initial['visual']; attempts=accepted=admitted=observations=considered=0
    seen={permutation_hash(current)}; reasons=Counter(); rounds=[]; current_visual=best_visual
    try:
        assert best_visual==binding['expectedVisual']
        baseline=native.request(wire(zero,'MEASURE'))
        assert baseline['legal'] and baseline['hard']==baseline['individualHard']==baseline['spacing']==0
        state=native.initial; active_started=time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for round_id in range(args.rounds):
                if attempts>=args.budget or time.monotonic()-active_started>=args.seconds: break
                nodes=np.array([n['features'] for n in state['nodes']])
                view=SimpleNamespace(groups=decoder.groups,positions=decoder.positions[current])
                words,support=vocabulary(view,nodes,args.seed+round_id,args.words)
                rows=[]; spectra=[]; hashes=set()
                for wi,word in enumerate(words):
                    for shift,reverse in transforms(len(word)):
                        p=proposal_permutation(current,word,shift,reverse); key=permutation_hash(p)
                        if key in seen or key in hashes: continue
                        hashes.add(key); proposed=decode(decoder,p)
                        after=source_geometry(observer,proposed,args.view)[0]
                        spectrum=proxy.spectrum(before,after,sizes,edges); score=-float(model.forward(spectrum[None])[0])
                        rows.append({'candidateIndex':len(rows),'wordIndex':wi,'word':word,'shift':shift,'reverse':reverse,
                            'permutationSha256':key,'score':score,'wireSha256':hashlib.sha256(wire(proposed).encode()).hexdigest()})
                        spectra.append(spectrum)
                if not rows: break
                scores=np.array([r['score'] for r in rows]); assert np.isfinite(scores).all()
                order,noise,policy=sample_order(scores[:,None],args.seed+1000+round_id,args.temperature)
                obsfile=args.out/f'observation-{round_id:02}.npz'
                np.savez_compressed(obsfile,nodes=nodes,permutation=current,spectra=np.array(spectra),scores=scores,order=order,gumbel=noise)
                rankfile=args.out/f'ranking-{round_id:02}.jsonl'
                rankfile.write_text(''.join(json.dumps(r)+'\n' for r in rows)); ranking_hash=digest(rankfile)
                rounds.append({'round':round_id,'sourcePermutationSha256':permutation_hash(current),'sourceVisual':current_visual,
                    'support':support,'candidatesScored':len(rows),'observation':str(obsfile),'observationSha256':digest(obsfile),
                    'ranking':str(rankfile),'rankingSha256':ranking_hash,'policy':policy})
                moved=False
                for rank,index in enumerate(order[:args.per_round]):
                    if attempts>=args.budget or time.monotonic()-active_started>=args.seconds: break
                    row=rows[int(index)]; p=proposal_permutation(current,row['word'],row['shift'],row['reverse'])
                    proposed=decode(decoder,p); command=wire(proposed)
                    assert permutation_hash(p)==row['permutationSha256'] and hashlib.sha256(command.encode()).hexdigest()==row['wireSha256']
                    result=native.request(command); attempts+=1; considered+=1; reasons[result['reason']]+=1
                    walk_admitted=bool(result['legal'])
                    if result['accepted']: best=proposed.copy(); best_visual=result['visual']; accepted+=1
                    stream.write(json.dumps({'round':round_id,'rank':rank,**row,'result':result,'walkAdmitted':walk_admitted})+'\n')
                    if walk_admitted:
                        # Only legality gates a NN-selected exploratory state.
                        # Its visual metric never selects between future candidates.
                        current=p; seen.add(row['permutationSha256']); admitted+=1
                        before,sizes,edges=source_geometry(observer,proposed,args.view); current_visual=result['visual']
                        state=native.request(wire(proposed,'OBS')); observations+=1
                        assert state['observed'] and state['visual']==result['visual'] and state['individualVisual']==result['individualVisual']
                        moved=True; break
                assert digest(rankfile)==ranking_hash; stream.flush()
                if time.monotonic()-active_started>=args.seconds: break
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory,args.out,decoder,best)
    if best is not None: np.save(args.out/'best-action.npy',best)
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'],stats['policyActionsEvaluated'],stats['acceptedActions'])==(best_visual,attempts,accepted)
    assert stats['hardConditions']==stats['individualHardConditions']==stats['overlap']==stats['spacing']==0
    report={'kind':'calibrated-event-world-coordinated-uphill-permutation-walk-v1','view':args.view,
        'sourceDirectory':str(args.directory),'sourceBinding':str(args.source_binding),'sourceBindingSha256':digest(args.source_binding),
        'sourceInputs':inputs,'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),'checkpointMetadata':metadata,
        'environment':str(args.environment),'environmentSha256':digest(args.environment),'seed':args.seed,
        'roundLimit':args.rounds,'wordsPerRound':args.words,'perRound':args.per_round,'budget':args.budget,'temperature':args.temperature,
        'secondsLimit':args.seconds,'rounds':rounds,'attempts':attempts,'accepted':accepted,'admittedStates':admitted,
        'nativeFullFeatureObservations':observations,'reasons':dict(reasons),'initialVisual':baseline['visual'],
        'initialIndividualVisual':baseline['individualVisual'],'final':stats,'lastExploratoryVisual':current_visual,
        'wallSeconds':time.monotonic()-started,'visitedStates':len(seen),'sameSizeRectangleMultisetPreserved':True,
        'modelChoosesAllCoordinates':True,'nativeCoordinateSearchCalls':0,'coordinateRepairs':0,
        'futureNativeMetricsUsedToRank':False,'futureRankFrozenBeforeNativeTry':True,
        'uphillExperimentalStatesAllowed':True,'onlyStrictBestCanReplaceProduct':True,
        'newTrainingUpdatesDuringInference':0,'inferenceFeatureInputDtype':'float32',
        'actionsSha256':digest(args.out/'actions.jsonl'),'savedGeometryMatchesModelProposal':True,
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in ['run_calibrated_macro_walk.py','run_calibrated_macro_policy.py',
            'calibrated_geometry_world.py','geometry_world_model.py','single_owner_cached_observer_v2.py',
            'joint_anchor_ray_policy.py','run_anchor_pair_walk.py','run_anchor_pair_policy.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','attempts','accepted','admittedStates','reasons','lastExploratoryVisual','wallSeconds']}
        |{'initialVisual':baseline['visual'],'finalVisual':best_visual,'modelRankings':sum(r['candidatesScored'] for r in rounds)}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--source-binding',type=Path,required=True)
    p.add_argument('--directory',type=Path,required=True); p.add_argument('--checkpoint',type=Path,required=True)
    p.add_argument('--environment',type=Path,required=True); p.add_argument('--view',choices=['overview','individual'],required=True)
    p.add_argument('--out',type=Path,required=True); p.add_argument('--seed',type=int,default=106103)
    p.add_argument('--rounds',type=int,default=8); p.add_argument('--words',type=int,default=64)
    p.add_argument('--per-round',type=int,default=16); p.add_argument('--budget',type=int,default=128)
    p.add_argument('--temperature',type=float,default=2.); p.add_argument('--seconds',type=float,default=30.)
    run(p.parse_args())
