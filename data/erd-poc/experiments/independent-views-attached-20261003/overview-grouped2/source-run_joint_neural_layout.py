#!/usr/bin/env python3
"""Train and audit a small coordinated layout network under the RSS wrapper.

This is explicitly Captain-specific optimization of network weights, not an
unseen-graph generalization result. No gradient directly edits card positions.
Every tested coordinate batch is a replayable frozen network forward pass.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from learned_global_replay import INPUT_FILES, pairs, routes, port, rounded

ROOT=Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def action_text(delta):
    return 'TRY '+' '.join(f'{float(value):.12g}' for value in delta.ravel())


def verify_geometry(directory,output,delta):
    positions=[pairs(directory/(prefix+'positions.tsv')) for prefix in ['', 'individual.']]
    sizes=pairs(directory/'individual.nodes.tsv')
    mapping={row[0]:row[1:] for row in (line.split('\t') for line in (directory/'components.tsv').read_text().splitlines())}
    if delta is not None:
        for (node,position),move in zip(positions[0].items(),delta):
            offset=[rounded(float(f'{v:.12g}')) for v in move]
            for k in range(2):position[k]+=offset[k]
            for member in mapping[node]:
                for k in range(2):positions[1][member][k]+=offset[k]
    for expected,suffix in zip(positions,['','.individual']):
        actual=pairs(Path(str(output)+suffix));assert actual.keys()==expected.keys()
        assert all(abs(actual[key][k]-p[k])<1e-7 for key,p in expected.items() for k in range(2))
    expected_routes=routes(directory/'individual.routes.tsv')
    if delta is not None:
        for key,source,target in (line.split('\t') for line in (directory/'individual.edges.tsv').read_text().splitlines()):
            expected_routes[key]=[port(positions[1][source],sizes[source],positions[1][target]),
                                  port(positions[1][target],sizes[target],positions[1][source])]
    actual=routes(Path(str(output)+'.individual.routes.tsv'));assert actual.keys()==expected_routes.keys()
    assert all(abs(actual[key][j][k]-points[j][k])<1e-8 for key,points in expected_routes.items() for j in range(2) for k in range(2))


def complete_product_audit(args,source,started):
    report=json.loads((args.out/'joint-worker-result.json').read_text())
    assert report['neuralChecksPassed'] and report['view']==args.view
    assert report['sourceSha256']==digest(source) and report['payloadSha256']==digest(args.payload)
    assert report['temporaryRegressionLimit']==args.regression_limit
    for name,value in report['verifiedOutputHashes'].items():assert digest(args.out/name)==value
    for name,value in report['inputHashes'].items():assert digest(args.out/name)==value
    with (args.out/'workflow.log').open('a') as log:
        def run(command):subprocess.run(list(map(str,command)),stdout=log,stderr=log,check=True)
        if args.view=='individual':
            run(['node','--max-old-space-size=128','scripts/erd-poc/audit_individual_policy_experiment.cjs',source,args.payload,args.out])
            audit=json.loads((args.out/'individual.audit.json').read_text())
        else:
            candidate=args.out/'candidate.layout.json'
            if candidate.exists():
                applied=json.loads((args.out/'proposal.audit.json').read_text())
                assert applied['candidateSha256']==digest(candidate) and applied['sourceSha256']==digest(source)
                assert applied['learnedPolicy']==json.loads((args.out/'learned.tsv.policy.json').read_text())
            else:
                run(['node','--max-old-space-size=128','scripts/erd-poc/apply_learned_components.cjs',source,args.payload,args.out,args.out/'learned.tsv','--experimental'])
            run(['node','--max-old-space-size=128','scripts/erd-poc/audit_leaf_card_connections.cjs',candidate,args.payload,args.out/'product'])
            audit=json.loads((args.out/'product.audit.json').read_text())
    assert audit['visualCrossings']<=report['sourceVisual']+args.regression_limit
    report.update(candidate=audit.get('candidate',str(args.out/'candidate.layout.json')),candidateSha256=audit['candidateSha256'],
                  visualCrossings=audit['visualCrossings'],improvesSource=audit['visualCrossings']<report['sourceVisual'],
                  allChecksPassed=True,seconds=time.monotonic()-started,browserVerified=False,
                  trainingExitedBeforeProductAudit=True,resumedProductAudit=args.resume_audit)
    if args.view=='individual':report['individualVisual']=audit['visualCrossings']
    else:report['productFileLoadVerified']=True
    report_file=args.out/('workflow.audit.json' if args.view=='individual' else 'workflow.json')
    report_file.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({key:report[key] for key in ['view','visualCrossings','improvesSource','candidateSha256','allChecksPassed','seconds','trainingExitedBeforeProductAudit']}),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--environment',type=Path,required=True)
    parser.add_argument('--payload',type=Path,required=True)
    parser.add_argument('--view',choices=['individual','overview'],default='individual')
    parser.add_argument('--max-step',type=float,default=512.)
    parser.add_argument('--iterations',type=int,default=256)
    parser.add_argument('--seconds',type=float,default=20.)
    parser.add_argument('--lr',type=float,default=.0005)
    parser.add_argument('--spacing-weight',type=float,default=20.)
    parser.add_argument('--temperature-start',type=float,default=1.,help='initial loss smoothing factor; anneals to one')
    parser.add_argument('--anneal-fraction',type=float,default=.6,help='fraction of training budget used to reduce smoothing')
    parser.add_argument('--grouped-route-loss',action='store_true',help='use actual grouped member anchors and representative selection in the overview loss')
    parser.add_argument('--seed',type=int,default=517)
    parser.add_argument('--initial-checkpoint',type=Path)
    parser.add_argument('--regression-limit',type=int,default=0,help='isolated intermediate candidate; must recover before promotion')
    parser.add_argument('--inference-only',action='store_true')
    parser.add_argument('--resume-audit',action='store_true',help='audit saved and replay-verified worker output without repeating training')
    parser.add_argument('--train-worker',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if os.environ.get('OMP_NUM_THREADS')!='1':parser.error('run inside run_memory_bounded.py')
    if not args.out.resolve().is_relative_to(ROOT/'.tmp') or ROOT!=Path.cwd():parser.error('use a fresh repository .tmp directory')
    if not 0<args.seconds<=30 or not 0<args.iterations<=1024 or not 0<args.max_step<=4096:parser.error('invalid bounded training budget')
    if not 0<args.lr<=.01 or not 0<args.spacing_weight<=1000:parser.error('invalid optimizer controls')
    if not 1<=args.temperature_start<=256 or not 0<args.anneal_fraction<=1:parser.error('invalid loss temperature schedule')
    if not 0<=args.regression_limit<=200:parser.error('temporary conflict regression limit must be between 0 and 200')
    if args.inference_only and not args.initial_checkpoint:parser.error('inference-only requires a trained checkpoint')
    if args.grouped_route_loss and args.view!='overview':parser.error('grouped route loss requires the overview')
    individual=args.view=='individual'
    source=args.previous/('candidate.individual.layout.json' if individual else 'candidate.layout.json')
    previous_audit=json.loads((args.previous/('individual.audit.json' if individual else 'product.audit.json')).read_text())
    assert digest(source)==previous_audit['candidateSha256']
    assert previous_audit['spacingViolations']==0
    assert previous_audit['actualProductRendererVerified'] if individual else previous_audit['productCoordinatesPreserved']
    if not args.train_worker:
        started=time.monotonic()
        if not args.resume_audit:
            subprocess.run([sys.executable,str(Path(__file__).resolve()),*sys.argv[1:],'--train-worker'],check=True)
        complete_product_audit(args,source,started)
        return
    assert not args.resume_audit
    import numpy as np
    from joint_layout_proxy import JointPolicy, LayoutProxy
    args.out.mkdir(parents=True,exist_ok=False)
    with (args.out/'workflow.log').open('w') as log:
        def run(command):
            subprocess.run(list(map(str,command)),stdout=log,stderr=log,check=True)
        if individual:
            files={'nodes.tsv':'individual.nodes.tsv','edges.tsv':'individual.edges.tsv',
                   'positions.tsv':'learned.tsv.individual','routes.tsv':'learned.tsv.individual.routes.tsv'}
            for target,old in files.items():
                for prefix in ['', 'individual.']:shutil.copyfile(args.previous/old,args.out/(prefix+target))
            for old,target in [('nodes.tsv','components.tsv'),('edges.tsv','groups.tsv')]:
                ids=[line.split('\t')[0] for line in (args.out/old).read_text().splitlines()]
                (args.out/target).write_text(''.join(f'{key}\t{key}\n' for key in ids))
        else:
            run(['node','--max-old-space-size=128','scripts/erd-poc/probe_overview_boundary.cjs','export',source,args.payload,args.out,'--max-routes','256','--with-individual'])
            run(['node','--max-old-space-size=128','scripts/erd-poc/export_learned_components.cjs',source,args.payload,args.out])
        output=args.out/'learned.tsv'
        report={'scope':'Captain-trained joint neural batch','view':args.view,'promoted':False,
                'source':str(source),'sourceSha256':digest(source),'payloadSha256':digest(args.payload),
                'environmentSha256':digest(args.environment),'inputHashes':{name:digest(args.out/name) for name in INPUT_FILES},
                'proposalAuthority':'frozen neural network batches','heuristicSearchCalls':0,
                'sourceVisual':previous_audit['visualCrossings'],'maxStep':args.max_step,
                'seed':args.seed,'learningRate':args.lr,'spacingWeight':args.spacing_weight,
                'lossTemperatureStart':args.temperature_start,'lossAnnealFraction':args.anneal_fraction,
                'groupedRouteLoss':args.grouped_route_loss,
                'temporaryRegressionLimit':args.regression_limit,
                'inferenceOnly':args.inference_only,
                'lossKind':'boundary-clipped separating-axis margins v4',
                'implementationHashes':{name:digest(ROOT/'scripts/erd-poc'/name) for name in ['run_joint_neural_layout.py','joint_layout_proxy.py','joint_grouped_routes.py','ml_joint_batch_environment.cpp','ml_component_environment.cpp']},
                'trainingScope':'Captain-specific differentiable training; no unseen-graph evaluation'}
        for name,expected in report['implementationHashes'].items():
            snapshot=args.out/('source-'+name)
            shutil.copyfile(ROOT/'scripts/erd-poc'/name,snapshot)
            assert digest(snapshot)==expected
        (args.out/'experiment.json').write_text(json.dumps(report,indent=2)+'\n')
        process=subprocess.Popen([str(args.environment),'--directory',str(args.out),'--out',str(output),
                                  '--overview-only','0' if individual else '1','--regression-limit',str(args.regression_limit)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,bufsize=1)
        def request(text):
            process.stdin.write(text+'\n');process.stdin.flush()
            line=process.stdout.readline()
            if not line:raise RuntimeError('joint environment exited: '+str(process.poll()))
            return json.loads(line)
        best_checkpoint=None;proposals=[];history=[]
        started=time.monotonic()
        try:
            observation=process.stdout.readline();initial=json.loads(observation)
            assert initial['ready'] and initial['visual']==previous_audit['visualCrossings']
            observation_file=args.out/'joint-observations.json';observation_file.write_text(observation)
            report['observationsSha256']=digest(observation_file)
            features=np.array([row['features'] for row in initial['nodes']])
            size_map=pairs(args.out/'nodes.tsv');position_map=pairs(args.out/'positions.tsv')
            ids=list(size_map);by_id={key:i for i,key in enumerate(ids)}
            positions=np.array([position_map[key] for key in ids]);sizes=np.array([size_map[key] for key in ids])
            edges=np.array([[by_id[row[1]],by_id[row[2]]] for row in
                            (line.split('\t') for line in (args.out/'edges.tsv').read_text().splitlines())])
            assert [row['id'] for row in initial['nodes']]==list(range(len(ids)))
            provider=None
            if args.grouped_route_loss:
                from joint_grouped_routes import GroupedRoutes
                provider=GroupedRoutes(args.out)
                assert provider.physical_ids==ids
            proxy=LayoutProxy(positions,sizes,edges,args.max_step,args.spacing_weight,provider)
            model=JointPolicy(features,positions,sizes,args.max_step,args.seed)
            prior_updates=0
            if args.initial_checkpoint:
                with np.load(args.initial_checkpoint) as weights: parent=json.loads(str(weights['metadata']))
                assert parent['sourceSha256']==report['sourceSha256'] and parent['inputHashes']==report['inputHashes']
                assert parent['observationsSha256']==report['observationsSha256']
                loaded=JointPolicy.load(args.initial_checkpoint)
                for key in ['mean','scale','negative','positive']:
                    np.testing.assert_allclose(getattr(loaded,key),getattr(model,key),rtol=0,atol=1e-10)
                model=loaded;prior_updates=parent['trainedUpdates']
                report.update(initialCheckpoint=str(args.initial_checkpoint),initialCheckpointSha256=digest(args.initial_checkpoint),priorTrainingUpdates=prior_updates)
            first={key:np.zeros_like(value) for key,value in model.p.items()};second={key:np.zeros_like(value) for key,value in model.p.items()}
            report['parameterCount']=sum(value.size for value in model.p.values())
            report['proxyCandidatePairs']={name:len(getattr(proxy,name)) for name in ['cross_pairs','hit_pairs','near_pairs']}
            training_start=time.monotonic()
            with (args.out/'joint-batches.jsonl').open('w') as trace:
                for iteration in range(1,(1 if args.inference_only else args.iterations)+1):
                    progress=max((iteration-1)/max(1,args.iterations-1),(time.monotonic()-training_start)/args.seconds)
                    cooling=min(1.,progress/args.anneal_fraction)
                    temperature=args.temperature_start**(1.-cooling)
                    delta,cache=model.forward(features);loss,gradient=proxy.loss(positions+delta,temperature)
                    if not args.inference_only:
                        gradients=model.backward(cache,gradient)
                        assert math.isfinite(loss['total']) and all(np.isfinite(g).all() for g in gradients.values())
                        norm=math.sqrt(sum(float(np.sum(g*g)) for g in gradients.values()));factor=min(1.,10/max(norm,1e-12))
                        for key in model.keys:
                            gradient=gradients[key]*factor
                            first[key]=.9*first[key]+.1*gradient;second[key]=.999*second[key]+.001*gradient**2
                            model.p[key]-=args.lr*(first[key]/(1-.9**iteration))/(np.sqrt(second[key]/(1-.999**iteration))+1e-8)
                    expired=time.monotonic()-training_start>=args.seconds
                    if iteration%4==0 or iteration==1 or iteration==args.iterations or expired:
                        checkpoint=args.out/f'joint-policy-{iteration:04d}.npz'
                        model.save(checkpoint,{**report,'trainedUpdates':prior_updates+(0 if args.inference_only else iteration),'absoluteCoordinatesAsInput':False,'modelNamesAsInput':False})
                        command=action_text(model.forward(features)[0]);result=request(command)
                        record={'iteration':iteration,'checkpoint':str(checkpoint),'checkpointSha256':digest(checkpoint),
                                'actionSha256':hashlib.sha256(command.encode()).hexdigest(),'result':result}
                        proposals.append(record);trace.write(json.dumps(record)+'\n');trace.flush()
                        if result['accepted']:best_checkpoint=checkpoint
                        history.append({'iteration':iteration,'lossTemperature':temperature,'preUpdateProxy':loss,'geometry':result})
                    if iteration%32==0:print(json.dumps({'iteration':iteration,'proxy':loss['total'],'bestCheckpoint':str(best_checkpoint),'seconds':time.monotonic()-training_start}),flush=True)
                    if expired:break
            assert request('SAVE')['saved'];process.stdin.write('QUIT\n');process.stdin.flush();assert process.wait(timeout=5)==0
        finally:
            if process.poll() is None:process.kill();process.wait()
        # Check every frozen network, including rejected candidate batches.
        assert digest(observation_file)==report['observationsSha256']
        for name,value in report['inputHashes'].items():assert digest(args.out/name)==value
        for proposal in proposals:
            assert digest(proposal['checkpoint'])==proposal['checkpointSha256']
            frozen=JointPolicy.load(proposal['checkpoint'])
            assert hashlib.sha256(action_text(frozen.forward(features)[0]).encode()).hexdigest()==proposal['actionSha256']
        winning_delta=JointPolicy.load(best_checkpoint).forward(features)[0] if best_checkpoint else None
        verify_geometry(args.out,output,winning_delta)
        stats=json.loads(Path(str(output)+'.stats.json').read_text())
        assert stats['temporaryRegressionBudget']==args.regression_limit
        checkpoint=best_checkpoint or Path(proposals[-1]['checkpoint'])
        policy={'modelKind':'coordinated-displacement-network','untrainedControl':False,
                'checkpoint':str(checkpoint),'checkpointSha256':digest(checkpoint),'final':stats,
                'overviewOnly':not individual,'policyProposalSource':'frozen coordinated neural batches',
                'heuristicSearchCalls':0,'retainedSource':best_checkpoint is None,
                'winningCheckpoint':str(best_checkpoint) if best_checkpoint else None,
                'allFrozenBatchesReplayed':len(proposals),'frozenWinningGeometryReplayed':best_checkpoint is not None,
                'observationsSha256':report['observationsSha256'],'actionsSha256':digest(args.out/'joint-batches.jsonl')}
        Path(str(output)+'.policy.json').write_text(json.dumps(policy,indent=2)+'\n')
        report.update(frozenNetworkBatchesReplayed=len(proposals),frozenWinningGeometryReplayed=best_checkpoint is not None,
                      unchangedSourceVerified=best_checkpoint is None,bestCheckpoint=str(best_checkpoint) if best_checkpoint else None,
                      bestCheckpointSha256=digest(best_checkpoint) if best_checkpoint else None,
                      iterations=0 if args.inference_only else iteration,neuralChecksPassed=True,
                      neuralSeconds=time.monotonic()-started,browserVerified=False,history=history,
                      verifiedOutputHashes={name:digest(args.out/name) for name in ['learned.tsv','learned.tsv.individual','learned.tsv.routes.tsv',
                        'learned.tsv.individual.routes.tsv','learned.tsv.stats.json','learned.tsv.policy.json']})
        (args.out/'joint-worker-result.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'neuralWorker':'complete','frozenBatchesReplayed':len(proposals),'bestCheckpoint':str(best_checkpoint),'neuralSeconds':report['neuralSeconds']}),flush=True)


if __name__=='__main__':main()
