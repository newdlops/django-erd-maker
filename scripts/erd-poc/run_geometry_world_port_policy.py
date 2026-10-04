#!/usr/bin/env python3
"""NN-ranked same-face port candidates and NN-composed multi-line proposals."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from geometry_world_model import load, digest
from geometry_world_port_model import PortDecoder, WorldProxy, PATTERNS, same_face_patterns
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved
from single_owner_cached_observer_v2 import CachedObserver

PREFIXES = [2,4,8,16,32,64]


def decode_row(zero, n, row, singles):
    proposed = zero.copy()
    parts = [row] if row['kind'] == 'single' else [singles[i] for i in row['parts']]
    for part in parts: proposed[n+part['edge']] += PATTERNS[part['pattern']]
    return proposed


def run(args):
    assert 1 <= args.edges <= 256 and 1 <= args.budget <= 128 and 0 < args.seconds <= 30
    args.out.mkdir(parents=True,exist_ok=False); started = time.monotonic()
    binding = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    assert str(args.directory) == binding['directory']
    inputs = {n:digest(args.directory/n) for n in INPUT_FILES}; assert inputs == binding['inputs']
    decoder = WalkDecoder(args.directory); observer = CachedObserver(args.directory,decoder)
    zero = decoder.decode(np.arange(len(decoder.positions),dtype=np.int32))
    ports = PortDecoder(observer,zero,args.view); before = ports.geometry(zero)
    sizes, edges = (observer.sizes,ports.physical_edges) if args.view == 'overview' else (observer.full_sizes,observer.edges)
    models, metadata = load(args.world,args.untrained); proxy = WorldProxy(models)
    native = Native(args.environment,args.directory,args.out/'learned.tsv',args.view == 'overview',True)
    best = None; attempts = accepted = improving = ties = 0; reasons = Counter(); rows = []; words = []
    best_visual = native.initial['visual']; geometry_controls = 0; wires = set()
    try:
        assert best_visual == binding['expectedVisual']
        baseline = native.request(wire(zero,'MEASURE')); assert baseline['legal']
        assert baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        nodes = np.array([n['features'] for n in native.initial['nodes']]); active = np.any(nodes[:,4:6]>0,axis=1)
        eligible = np.flatnonzero(active[decoder.provider.owner_edges].any(1))
        if args.view == 'overview': eligible = eligible[ports.group_index[eligible]>=0]
        chosen_edges = np.random.default_rng(args.seed).choice(eligible,min(args.edges,len(eligible)),replace=False)
        rank_started = time.monotonic()
        for edge in chosen_edges:
            for pattern in same_face_patterns(ports,int(edge)):
                row = {'kind':'single','edge':int(edge),'pattern':int(pattern),'candidateIndex':len(rows)}
                proposed = decode_row(zero,ports.n,row,rows); after = ports.geometry(proposed)
                if geometry_controls < 32:
                    pos,full_pos,physical,full,_ = observer.decode(proposed)
                    expected = (pos,physical) if args.view == 'overview' else (full_pos,full)
                    assert all(np.array_equal(a,b) for a,b in zip(after,expected)); geometry_controls += 1
                prediction = proxy.delta(before,after,sizes,edges)
                row.update({'prediction':prediction,'score':-prediction['predictedDelta'],
                    'wireSha256':hashlib.sha256(wire(proposed).encode()).hexdigest()})
                rows.append(row); words.append([int(edge),int(pattern)])
            if time.monotonic()-rank_started >= args.seconds: break
        assert rows; singles = rows.copy(); single_order = np.argsort(-np.array([r['score'] for r in singles]),kind='stable')
        unique = []; seen_edges = set()
        for index in single_order:
            row = singles[int(index)]
            if row['score'] <= .5: continue
            if row['edge'] in seen_edges: continue
            unique.append(int(index)); seen_edges.add(row['edge'])
            if len(unique) == max(PREFIXES): break
        for count in PREFIXES:
            if count > len(unique): break
            row = {'kind':'joint','parts':unique[:count],'candidateIndex':len(rows)}
            proposed = decode_row(zero,ports.n,row,singles); after = ports.geometry(proposed)
            prediction = proxy.delta(before,after,sizes,edges)
            row.update({'prediction':prediction,'score':-prediction['predictedDelta'],
                'wireSha256':hashlib.sha256(wire(proposed).encode()).hexdigest()}); rows.append(row)
        rank_seconds = time.monotonic()-rank_started
        scores = np.array([r['score'] for r in rows]); assert np.isfinite(scores).all()
        order = np.argsort(-scores,kind='stable')
        np.savez_compressed(args.out/'observations.npz',nodes=nodes,eligible_edges=eligible,chosen_edges=chosen_edges,
            words=np.array(words,dtype=np.int32),scores=scores,order=order,positive_unique_indices=np.array(unique,dtype=np.int32))
        with (args.out/'ranking.jsonl').open('x') as stream:
            for row in rows: stream.write(json.dumps(row)+'\n')
        ranking_hash = digest(args.out/'ranking.jsonl'); evaluation_started = time.monotonic()
        with (args.out/'actions.jsonl').open('x') as stream:
            for rank,index in enumerate(order[:args.budget]):
                if time.monotonic()-evaluation_started >= args.seconds: break
                row = rows[int(index)]; proposed = decode_row(zero,ports.n,row,singles); command = wire(proposed)
                h = hashlib.sha256(command.encode()).hexdigest(); assert h == row['wireSha256'] and h not in wires; wires.add(h)
                result = native.request(command); attempts += 1; reasons[result['reason']] += 1
                improving += int(result['legal'] and result['visual']<baseline['visual'])
                ties += int(result['legal'] and result['visual']==baseline['visual'])
                if result['accepted']: best = proposed.copy(); best_visual = result['visual']; accepted += 1
                stream.write(json.dumps({'rank':rank,**row,'result':result})+'\n')
        assert digest(args.out/'ranking.jsonl') == ranking_hash; assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(args.directory,args.out,decoder,best)
    if best is not None: np.save(args.out/'best-action.npy',best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert (stats['visual'],stats['policyActionsEvaluated'],stats['acceptedActions']) == (best_visual,attempts,accepted)
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['overlap'] == stats['spacing'] == 0
    report = {'kind':'learned-geometric-event-world-boundary-port-and-joint-prefix-policy-v1','view':args.view,
        'sourceDirectory':str(args.directory),'sourceBinding':str(args.source_binding),'sourceBindingSha256':digest(args.source_binding),
        'sourceInputs':inputs,'worldCheckpoint':str(args.world),'worldSha256':digest(args.world),'worldMetadata':metadata,
        'untrainedWorldControl':args.untrained,'environment':str(args.environment),'environmentSha256':digest(args.environment),
        'seed':args.seed,'edgesRequested':args.edges,'edgesChosen':len(chosen_edges),'eligibleEdges':len(eligible),
        'singleCandidatesScored':len(singles),'jointCandidatesScored':len(rows)-len(singles),
        'positiveNeuralSingleEdges':len(unique),'jointPrefixVocabulary':PREFIXES,'jointPositiveScoreThreshold':.5,
        'patterns':PATTERNS.tolist(),'budget':args.budget,'secondsLimitPerRankingOrEvaluation':args.seconds,
        'fastPortDecoderExactSlowControls':geometry_controls,'initialVisual':baseline['visual'],
        'initialIndividualVisual':baseline['individualVisual'],'attempts':attempts,'accepted':accepted,
        'improvingLegalActions':improving,'legalTies':ties,'reasons':dict(reasons),'final':stats,
        'rankingSeconds':rank_seconds,'wallSeconds':time.monotonic()-started,
        'sourceBoundaryFacePreservedByVocabulary':True,'allCoordinatesChosenByNeuralPredictedEventDelta':True,
        'futureNativeMeasurementsBeforeScheduleFrozen':0,'exactFuturePredicatesUsedDuringRanking':False,
        'nativeSearchCalls':0,'coordinateRepairs':0,'newTrainingUpdatesDuringInference':0,
        'inferenceFeatureInputDtype':'float32','savedGeometryMatchesModelOutput':True,
        'rankingSha256':ranking_hash,'observationsSha256':digest(args.out/'observations.npz'),'actionsSha256':digest(args.out/'actions.jsonl'),
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in
            ['run_geometry_world_port_policy.py','geometry_world_port_model.py','geometry_world_multi_owner.py',
             'geometry_world_model.py','single_owner_cached_observer_v2.py','run_anchor_pair_walk.py','run_anchor_pair_policy.py']}}
    (args.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['view','untrainedWorldControl','singleCandidatesScored','jointCandidatesScored',
        'positiveNeuralSingleEdges','attempts','accepted','improvingLegalActions','reasons','rankingSeconds','wallSeconds']}
        |{'initialVisual':baseline['visual'],'finalVisual':best_visual}),flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--source-binding',type=Path,required=True)
    p.add_argument('--directory',type=Path,required=True); p.add_argument('--world',type=Path,required=True)
    p.add_argument('--environment',type=Path,required=True); p.add_argument('--view',choices=['overview','individual'],required=True)
    p.add_argument('--out',type=Path,required=True); p.add_argument('--untrained',action='store_true')
    p.add_argument('--seed',type=int,default=104709); p.add_argument('--edges',type=int,default=128)
    p.add_argument('--budget',type=int,default=128); p.add_argument('--seconds',type=float,default=30.)
    run(p.parse_args())
