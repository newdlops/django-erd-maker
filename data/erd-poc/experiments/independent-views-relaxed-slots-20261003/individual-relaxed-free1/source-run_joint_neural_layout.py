#!/usr/bin/env python3
"""Train and audit a small coordinated layout network under the RSS wrapper.

This is explicitly Captain-specific optimization of network weights, not an
unseen-graph generalization result. No gradient directly edits card positions.
Every tested coordinate batch is a replayable frozen network forward pass.
"""
import argparse
from contextlib import nullcontext
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


def verify_geometry(directory,output,delta,attached_ports=False,neural_perimeter_ports=False):
    positions=[pairs(directory/(prefix+'positions.tsv')) for prefix in ['', 'individual.']]
    sizes=pairs(directory/'individual.nodes.tsv')
    mapping={row[0]:row[1:] for row in (line.split('\t') for line in (directory/'components.tsv').read_text().splitlines())}
    member_moves={}
    if delta is not None:
        for (node,position),move in zip(positions[0].items(),delta):
            offset=[rounded(float(f'{v:.12g}')) for v in move]
            for k in range(2):position[k]+=offset[k]
            for member in mapping[node]:
                member_moves[member]=offset
                for k in range(2):positions[1][member][k]+=offset[k]
    for expected,suffix in zip(positions,['','.individual']):
        actual=pairs(Path(str(output)+suffix));assert actual.keys()==expected.keys()
        assert all(abs(actual[key][k]-p[k])<1e-7 for key,p in expected.items() for k in range(2))
    expected_routes=routes(directory/'individual.routes.tsv')
    if delta is not None and neural_perimeter_ports:
        import numpy as np
        from joint_neural_ports import PerimeterRoutes
        provider=PerimeterRoutes(directory)
        decoded=np.array([[float(f'{value:.12g}') for value in row] for row in delta[len(positions[0]):]])
        physical=np.array([positions[0][key] for key in provider.physical_ids])
        provider.set_actions(decoded,quantized=True,positions=physical)
        full_positions=physical[provider.owner]+provider.offsets
        decoded_ports=provider.full_provider.forward(full_positions,provider.sizes)[0]
        edge_ids=[line.split('\t')[0] for line in (directory/'individual.edges.tsv').read_text().splitlines()]
        expected_routes=dict(zip(edge_ids,decoded_ports.tolist()))
    elif delta is not None:
        for key,source,target in (line.split('\t') for line in (directory/'individual.edges.tsv').read_text().splitlines()):
            if attached_ports:
                expected_routes[key]=[[rounded(point[k]+member_moves[node][k]) for k in range(2)]
                                      for point,node in zip(expected_routes[key],[source,target])]
            else:
                expected_routes[key]=[port(positions[1][source],sizes[source],positions[1][target]),
                                      port(positions[1][target],sizes[target],positions[1][source])]
    actual=routes(Path(str(output)+'.individual.routes.tsv'));assert actual.keys()==expected_routes.keys()
    assert all(abs(actual[key][j][k]-points[j][k])<1e-8 for key,points in expected_routes.items() for j in range(2) for k in range(2))


def complete_product_audit(args,source,started):
    report=json.loads((args.out/'joint-worker-result.json').read_text())
    assert report['neuralChecksPassed'] and report['view']==args.view
    assert report['sourceSha256']==digest(source) and report['payloadSha256']==digest(args.payload)
    assert report['temporaryRegressionLimit']==args.regression_limit
    assert report.get('attachedPorts',False)==args.attached_ports
    assert report.get('neuralPerimeterPorts',False)==args.neural_perimeter_ports
    assert report.get('sharedSourceEndpoints',False)==args.shared_endpoints
    assert report.get('rayConditionedPorts',False)==args.ray_conditioned_ports
    assert report.get('localizedPatch',False)==bool(args.patch_nodes)
    assert report.get('rigidPatch',False)==args.rigid_patch
    assert report.get('separationDag',False)==args.separation_dag
    assert report.get('anchorRays',False)==args.anchor_rays
    assert report.get('lossTemperatureEnd',1.)==args.temperature_end
    if args.patch_nodes:
        assert report['patchNodesSha256']==digest(args.patch_nodes)==digest(args.out/'patch-nodes.json')
    assert report.get('gridOrder',False)==args.grid_order
    assert report.get('slotPermutation',False)==args.slot_permutation
    assert report.get('slotRelaxation',False)==args.slot_relaxation
    if args.slot_relaxation:
        assert report['slotTemperatureStart']==args.slot_temperature_start
        assert report['slotTemperatureEnd']==args.slot_temperature_end
    effective_spacing=args.spacing_weight if args.relaxed_spacing_weight is None else args.relaxed_spacing_weight
    assert report.get('effectiveSpacingWeight',report['spacingWeight'])==effective_spacing
    assert report.get('exactRewardTraining',False)==args.reward_es
    assert report.get('rewardMeasurementCacheSize',0)==args.reward_cache_size
    assert report.get('rewardEndpointHead',False)==args.reward_endpoint_head
    assert report.get('latentGraphStress',False)==args.latent_stress
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
    parser.add_argument('--spacing-padding',type=float,default=2.,help='extra training gap above 56x42; native acceptance is unchanged')
    parser.add_argument('--temperature-start',type=float,default=1.,help='initial loss smoothing factor')
    parser.add_argument('--temperature-end',type=float,default=1.,help='final loss smoothing factor, down to 0.01 scene units')
    parser.add_argument('--anneal-fraction',type=float,default=.6,help='fraction of training budget used to reduce smoothing')
    parser.add_argument('--grouped-route-loss',action='store_true',help='use actual grouped member anchors and representative selection in the overview loss')
    parser.add_argument('--attached-ports',action='store_true',help='translate existing endpoint attachments with their cards')
    parser.add_argument('--neural-perimeter-ports',action='store_true',help='learn shared edge-network perimeter offsets jointly with card translations')
    parser.add_argument('--shared-endpoints',action='store_true',help='mean-pool neural outputs for originally coincident endpoints on the same card')
    parser.add_argument('--ray-conditioned-ports',action='store_true',help='rotate endpoint phase references with the predicted card-center directions')
    parser.add_argument('--separation-dag',action='store_true',help='jointly decode neural translations within source-order spacing constraints')
    parser.add_argument('--anchor-rays',action='store_true',help='derive source-preserving endpoint phases from fixed interior anchors and DAG or slot-order node outputs')
    parser.add_argument('--patch-nodes',type=Path,help='immutable source-bound physical-node mask for shared neural outputs')
    parser.add_argument('--rigid-patch',action='store_true',help='pool branch features and predict one translation shared by its members')
    parser.add_argument('--port-span',type=float,default=.125,help='maximum learned endpoint movement as a fraction of its card perimeter')
    parser.add_argument('--hard-weight',type=float,default=0.,help='full attached-route hard-geometry loss weight')
    parser.add_argument('--quantized-loss',action='store_true',help='use decoder-rounded displacement in the loss with a straight-through training gradient')
    parser.add_argument('--graph-channels',type=int,default=0,help='append up to 32 structural graph inputs; no coordinate targets')
    parser.add_argument('--grid-order',action='store_true',help='learn hard card order and within-cell offsets with a soft-rank training gradient')
    parser.add_argument('--slot-permutation',action='store_true',help='learn permutations among equal-size existing card slots')
    parser.add_argument('--slot-relaxation',action='store_true',help='train shared weights through a separate source-corrected Sinkhorn geometry; submit only hard slot outputs')
    parser.add_argument('--slot-temperature-start',type=float,default=1.)
    parser.add_argument('--slot-temperature-end',type=float,default=.1)
    parser.add_argument('--relaxed-spacing-weight',type=float,help='optional training-only spacing weight for soft slot coordinates; hard spacing acceptance is unchanged')
    parser.add_argument('--latent-stress',action='store_true',help='train grid-order continuous codes against graph distances before hard decoding')
    parser.add_argument('--reward-es',action='store_true',help='train only the shared output head from exact noncommitting native rewards')
    parser.add_argument('--reward-endpoint-head',action='store_true',help='jointly train node and endpoint output matrices from native rewards')
    parser.add_argument('--reward-port-scale',type=float,default=4.,help='relative endpoint-head scale in joint reward parameter space')
    parser.add_argument('--reward-sigma',type=float,default=.001)
    parser.add_argument('--reward-directions',type=int,default=2)
    parser.add_argument('--reward-cache-size',type=int,default=0,help='reuse up to 128 identical immutable-source MEASURE results for slot reward training')
    parser.add_argument('--sampled-pairs',type=int,default=0,help='bounded random pairs for training only; exact native validation is unchanged')
    parser.add_argument('--active-pairs',action='store_true',help='evaluate all current near-pair losses in bounded chunks with explicit logistic-tail truncation')
    parser.add_argument('--cross-depth-weight',type=float,default=0.,help='training-only log-depth signal for deeply intersecting lines')
    parser.add_argument('--head-std',type=float,default=0.,help='random neural output-head initialization before training')
    parser.add_argument('--seed',type=int,default=517)
    parser.add_argument('--initial-checkpoint',type=Path)
    parser.add_argument('--regression-limit',type=int,default=0,help='isolated intermediate candidate; must recover before promotion')
    parser.add_argument('--inference-only',action='store_true')
    parser.add_argument('--resume-audit',action='store_true',help='audit saved and replay-verified worker output without repeating training')
    parser.add_argument('--train-worker',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if os.environ.get('OMP_NUM_THREADS')!='1':parser.error('run inside run_memory_bounded.py')
    if not args.out.resolve().is_relative_to(ROOT/'.tmp') or ROOT!=Path.cwd():parser.error('use a fresh repository .tmp directory')
    if not 0<args.seconds<=30 or not 0<args.iterations<=1024 or not 0<args.max_step<=(100000 if args.sampled_pairs or args.active_pairs or args.slot_permutation else 4096):parser.error('invalid bounded training budget')
    if not 0<args.lr<=.01 or not 0<args.spacing_weight<=1000:parser.error('invalid optimizer controls')
    if not 0<=args.spacing_padding<=2:parser.error('spacing padding must be between zero and two')
    if not .01<=args.temperature_end<=args.temperature_start<=256 or not 0<args.anneal_fraction<=1:parser.error('invalid loss temperature schedule')
    if not 0<=args.regression_limit<=200:parser.error('temporary conflict regression limit must be between 0 and 200')
    if args.inference_only and not args.initial_checkpoint:parser.error('inference-only requires a trained checkpoint')
    if args.grouped_route_loss and args.view!='overview':parser.error('grouped route loss requires the overview')
    if not 0<args.port_span<=.5:parser.error('port span must be positive and at most half a perimeter')
    if args.neural_perimeter_ports and args.attached_ports:parser.error('choose one endpoint decoder')
    if args.shared_endpoints and not args.neural_perimeter_ports:parser.error('shared endpoints require neural perimeter ports')
    if args.ray_conditioned_ports and (not args.neural_perimeter_ports or args.shared_endpoints):parser.error('ray conditioning requires neural perimeter ports without shared-endpoint pooling')
    if args.anchor_rays and (not (args.separation_dag or args.slot_permutation) or not args.neural_perimeter_ports or args.ray_conditioned_ports or not args.shared_endpoints):
        parser.error('anchor rays require DAG or slot outputs, perimeter decoding, and shared endpoints without another endpoint network')
    if args.separation_dag and (args.patch_nodes or args.grid_order or args.slot_permutation or args.reward_es or args.attached_ports
                               or (args.neural_perimeter_ports and not (args.ray_conditioned_ports or args.anchor_rays))
                               or (not args.neural_perimeter_ports and args.view=='overview' and not args.grouped_route_loss)):
        parser.error('separation DAG requires full ray-conditioned outputs or canonical card outputs with grouped overview routes')
    if args.patch_nodes and (not args.shared_endpoints or not args.neural_perimeter_ports):parser.error('patch outputs require shared neural perimeter ports')
    if args.rigid_patch and not args.patch_nodes:parser.error('rigid branch inference requires a patch mask')
    if not 0<=args.hard_weight<=1e6 or args.hard_weight and not (args.attached_ports or args.neural_perimeter_ports):parser.error('hard loss requires attached or neural ports and a weight up to 1e6')
    if not 0<=args.graph_channels<=32:parser.error('graph channels must be between zero and 32')
    if args.sampled_pairs and not 1024<=args.sampled_pairs<=32768:parser.error('sampled training requires 1024..32768 pairs')
    if args.sampled_pairs and args.hard_weight and args.view!='individual':parser.error('sampled hard-loss training is bounded only for the ungrouped individual view')
    if args.active_pairs and args.sampled_pairs:parser.error('choose active pairs or random sampling')
    if args.grid_order and (args.view!='individual' or not args.active_pairs or args.neural_perimeter_ports or args.attached_ports or args.hard_weight):parser.error('grid ordering requires the individual view, active pairs, and canonical center-ray ports')
    if args.slot_permutation and (not (args.reward_es or args.slot_relaxation) or args.grid_order or args.attached_ports or args.hard_weight or args.patch_nodes
                                  or (args.neural_perimeter_ports and not args.anchor_rays)
                                  or (args.view!='individual' and not args.anchor_rays)):
        parser.error('slot permutations require reward or relaxation training and canonical individual routes or shared interior-anchor endpoints')
    if args.slot_relaxation and (not args.slot_permutation or not args.anchor_rays or not args.active_pairs or args.reward_es or args.quantized_loss):
        parser.error('slot relaxation requires shared anchor slots, active-pair continuous loss and no reward/quantized training')
    if not .05<=args.slot_temperature_end<=args.slot_temperature_start<=4:parser.error('invalid slot relaxation temperature schedule')
    if args.relaxed_spacing_weight is not None and (not args.slot_relaxation or not 0<=args.relaxed_spacing_weight<=1000):
        parser.error('relaxed spacing weight requires slot relaxation and a value in 0..1000')
    if args.reward_es and not (args.slot_permutation or args.grid_order or args.patch_nodes):parser.error('exact reward training requires an ordering model or a localized neural patch')
    if not 0<=args.reward_cache_size<=128 or args.reward_cache_size and not (args.reward_es and args.slot_permutation):
        parser.error('measurement caching requires slot reward training and a size in 0..128')
    if args.reward_endpoint_head and not (args.reward_es and args.patch_nodes):parser.error('joint reward heads require a localized neural patch')
    if not 0<args.reward_port_scale<=16:parser.error('reward port scale must be in (0,16]')
    if args.latent_stress and (not args.grid_order or args.reward_es):parser.error('latent graph stress requires grid ordering without reward-head training')
    if not 0<args.reward_sigma<=.05 or not 1<=args.reward_directions<=4:parser.error('invalid bounded reward probes')
    if args.active_pairs and args.view=='overview' and args.max_step>4096 and (not (args.reward_es or args.slot_relaxation) or args.hard_weight):parser.error('overview hard projection pairs require a bounded move radius')
    if not 0<=args.cross_depth_weight<=10:parser.error('crossing depth weight must be in 0..10')
    if not 0<=args.head_std<=.5 or args.head_std and args.initial_checkpoint:parser.error('head initialization requires a fresh network and std <= .5')
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
                'effectiveSpacingWeight':args.spacing_weight if args.relaxed_spacing_weight is None else args.relaxed_spacing_weight,
                'spacingPadding':args.spacing_padding,
                'lossTemperatureStart':args.temperature_start,'lossAnnealFraction':args.anneal_fraction,
                'lossTemperatureEnd':args.temperature_end,
                'groupedRouteLoss':args.grouped_route_loss,
                'attachedPorts':args.attached_ports,
                'separationDag':args.separation_dag,'neuralPerimeterPorts':args.neural_perimeter_ports,'portSpan':args.port_span if args.neural_perimeter_ports else None,
                'anchorRays':args.anchor_rays,'trainableEndpointHead':args.neural_perimeter_ports and not args.anchor_rays,
                'sharedSourceEndpoints':args.shared_endpoints,
                'rayConditionedPorts':args.ray_conditioned_ports,
                'localizedPatch':bool(args.patch_nodes),
                'rigidPatch':args.rigid_patch,
                'hardGeometryWeight':args.hard_weight,'quantizedLoss':args.quantized_loss,
                'roundingGradientEstimator':'straight-through' if args.quantized_loss else None,
                'graphChannels':args.graph_channels,
                'gridOrder':args.grid_order,
                'slotPermutation':args.slot_permutation,
                'slotRelaxation':args.slot_relaxation,
                'slotTemperatureStart':args.slot_temperature_start if args.slot_relaxation else None,
                'slotTemperatureEnd':args.slot_temperature_end if args.slot_relaxation else None,
                'trainingGeometry':'source-corrected finite Sinkhorn relaxation' if args.slot_relaxation else 'hard model outputs',
                'latentGraphStress':args.latent_stress,
                'exactRewardTraining':args.reward_es,'rewardSigma':args.reward_sigma if args.reward_es else None,
                'rewardEndpointHead':args.reward_endpoint_head,
                'rewardDirections':args.reward_directions if args.reward_es else 0,
                'rewardMeasurementCacheSize':args.reward_cache_size,
                'sortingGradientEstimator':'straight-through soft ranks with fixed current column groups' if args.grid_order and not (args.reward_es or args.latent_stress) else None,
                'sampledTrainingPairs':args.sampled_pairs,'initialHeadStd':args.head_std,
                'activePairLoss':args.active_pairs and not (args.reward_es or args.latent_stress),
                'crossingDepthWeight':args.cross_depth_weight,
                'samplingSeed':args.seed+9000 if args.sampled_pairs else None,
                'exactNativeValidation':True,
                'temporaryRegressionLimit':args.regression_limit,
                'inferenceOnly':args.inference_only,
                'lossKind':'graph-distance stress on continuous ordering codes' if args.latent_stress else 'exact native reward with antithetic neural-head probes' if args.reward_es else 'boundary-clipped separating-axis margins v4',
                'implementationHashes':{name:digest(ROOT/'scripts/erd-poc'/name) for name in ['run_joint_neural_layout.py','joint_layout_proxy.py','joint_grouped_routes.py','joint_neural_ports.py','joint_separation_policy.py','joint_anchor_ray_policy.py','joint_grid_policy.py','joint_slot_policy.py','joint_slot_anchor_policy.py','joint_relaxed_slot_policy.py','joint_reward_training.py','joint_graph_stress.py','joint_hard_geometry.py','joint_graph_features.py','joint_sampled_proxy.py','joint_active_proxy.py','ml_joint_batch_environment.cpp','ml_component_environment.cpp','constrained_boundary_sweep.h','audit_individual_policy_experiment.cjs']},
                'trainingScope':'Captain-specific differentiable training; no unseen-graph evaluation'}
        for name,expected in report['implementationHashes'].items():
            snapshot=args.out/('source-'+name)
            shutil.copyfile(ROOT/'scripts/erd-poc'/name,snapshot)
            assert digest(snapshot)==expected
        if args.patch_nodes:
            patch=json.loads(args.patch_nodes.read_text())
            assert patch['sourceSha256']==report['sourceSha256'] and patch['view']==args.view
            assert patch['sourceVisual']==report['sourceVisual']
            assert patch['positionsProposed'] is False and patch['coordinatesAsTargets'] is False
            assert patch['nodeIds'] and len(set(patch['nodeIds']))==len(patch['nodeIds'])
            shutil.copyfile(args.patch_nodes,args.out/'patch-nodes.json')
            report.update(patchNodesSha256=digest(args.out/'patch-nodes.json'),patchSelection=patch['selection'])
        (args.out/'experiment.json').write_text(json.dumps(report,indent=2)+'\n')
        process=subprocess.Popen([str(args.environment),'--directory',str(args.out),'--out',str(output),
                                  '--overview-only','0' if individual else '1','--regression-limit',str(args.regression_limit),
                                  '--attached-ports','1' if args.attached_ports else '0',
                                  '--neural-perimeter-ports','1' if args.neural_perimeter_ports else '0'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,bufsize=1)
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
            if args.graph_channels:
                from joint_graph_features import laplacian_features
                graph_features,graph_report=laplacian_features(len(ids),edges,args.graph_channels)
                features=np.concatenate([features,graph_features],axis=1)
                report['graphFeatureReport']=graph_report
            feature_file=args.out/'joint-input-features.npy'
            np.save(feature_file,features)
            report['featureMatrixSha256']=digest(feature_file)
            provider=None
            model_class=JointPolicy
            if args.slot_permutation:
                from joint_slot_policy import SlotPermutationPolicy
                model_class=SlotPermutationPolicy
                if args.anchor_rays:
                    from joint_neural_ports import PerimeterRoutes
                    from joint_slot_anchor_policy import SlotAnchorRayPolicy
                    provider=PerimeterRoutes(args.out)
                    model_class=SlotAnchorRayPolicy
                    if args.slot_relaxation:
                        from joint_relaxed_slot_policy import RelaxedSlotAnchorPolicy
                        model_class=RelaxedSlotAnchorPolicy
                    assert provider.physical_ids==ids
            elif args.grid_order:
                from joint_grid_policy import GridOrderPolicy
                model_class=GridOrderPolicy
            elif args.separation_dag and not args.neural_perimeter_ports:
                from joint_separation_policy import SeparationDagPolicy
                model_class=SeparationDagPolicy
                if args.grouped_route_loss:
                    from joint_grouped_routes import GroupedRoutes
                    provider=GroupedRoutes(args.out)
                    assert provider.physical_ids==ids
                    assert (abs(provider.offsets)+provider.sizes/2<=sizes[provider.owner]/2+1e-7).all(), 'full cards escape physical owners'
            elif args.neural_perimeter_ports:
                from joint_neural_ports import PerimeterRoutes, JointPortPolicy, SharedEndpointPolicy, RayConditionedPolicy
                provider=PerimeterRoutes(args.out)
                model_class=RayConditionedPolicy if args.ray_conditioned_ports else SharedEndpointPolicy if args.shared_endpoints else JointPortPolicy
                if args.separation_dag:
                    from joint_separation_policy import SeparationDagPortPolicy
                    model_class=SeparationDagPortPolicy
                if args.anchor_rays:
                    from joint_anchor_ray_policy import AnchorRayPolicy
                    model_class=AnchorRayPolicy
                if args.patch_nodes:
                    from joint_neural_ports import PatchPortPolicy,RigidPatchPortPolicy
                    model_class=RigidPatchPortPolicy if args.rigid_patch else PatchPortPolicy
                    assert set(patch['nodeIds'])<=set(ids)
                    node_mask=np.array([key in set(patch['nodeIds']) for key in ids])
                assert provider.physical_ids==ids
            elif args.grouped_route_loss or args.attached_ports:
                from joint_grouped_routes import GroupedRoutes
                provider=GroupedRoutes(args.out,attached=args.attached_ports)
                assert provider.physical_ids==ids
            if args.reward_es or args.latent_stress:
                proxy=None
            elif args.active_pairs:
                from joint_active_proxy import ActiveLayoutProxy
                proxy=ActiveLayoutProxy(positions,sizes,edges,args.spacing_weight,provider,
                    chunk_size=4096 if args.slot_relaxation else 16384)
                report.update(visualLossTailCutoff=proxy.visual_cutoff_sigmas,visualLossTailBound=proxy.loss_tail_bound,
                              visualLossChunkSize=proxy.chunk_size)
            elif args.sampled_pairs:
                from joint_sampled_proxy import SampledLayoutProxy
                proxy=SampledLayoutProxy(positions,sizes,edges,args.spacing_weight,provider,args.sampled_pairs,args.seed+9000)
            else:
                proxy=LayoutProxy(positions,sizes,edges,args.max_step,args.spacing_weight,provider)
            if proxy is not None:
                proxy.cross_depth_weight=args.cross_depth_weight
                proxy.spacing_padding=args.spacing_padding
                if args.relaxed_spacing_weight is not None:proxy.spacing_weight=args.relaxed_spacing_weight
            hard_proxy=None
            if args.latent_stress:
                from joint_graph_stress import GraphStress
                stress=GraphStress(len(positions),edges,args.seed+37000)
                report['graphStressObjective']=stress.report
            if args.hard_weight:
                from joint_hard_geometry import FullHardGeometry
                hard_proxy=FullHardGeometry(provider,positions,sizes,edges,args.max_step,args.hard_weight)
                if args.sampled_pairs or args.active_pairs and individual:
                    # The individual hard graph only includes adjacent edge
                    # pairs and own-card endpoints, regardless of move radius.
                    assert hard_proxy.projected is None
                    assert not getattr(hard_proxy.proxy.route_provider,'dynamic_edges',False)
                    report['sampledHardPairCounts']={name:len(getattr(hard_proxy.proxy,name)) for name in ['cross_pairs','hit_pairs']}
            model=(model_class(features,positions,sizes,args.max_step,args.seed,provider,args.port_span,node_mask) if args.patch_nodes
                   else model_class(features,positions,sizes,args.max_step,args.seed,provider,args.port_span) if args.neural_perimeter_ports
                   else model_class(features,positions,sizes,args.max_step,args.seed))
            if args.head_std:
                model.p['wo']=np.random.default_rng(args.seed+18000).normal(0,args.head_std,model.p['wo'].shape)
            prior_updates=0
            if args.initial_checkpoint:
                with np.load(args.initial_checkpoint) as weights: parent=json.loads(str(weights['metadata']))
                assert parent['sourceSha256']==report['sourceSha256'] and parent['inputHashes']==report['inputHashes']
                assert parent['observationsSha256']==report['observationsSha256']
                assert parent.get('attachedPorts',False)==args.attached_ports
                assert parent.get('neuralPerimeterPorts',False)==args.neural_perimeter_ports
                assert parent.get('sharedSourceEndpoints',False)==args.shared_endpoints
                assert parent.get('rayConditionedPorts',False)==args.ray_conditioned_ports
                assert parent.get('localizedPatch',False)==bool(args.patch_nodes)
                assert parent.get('rigidPatch',False)==args.rigid_patch
                assert parent.get('separationDag',False)==args.separation_dag
                assert parent.get('anchorRays',False)==args.anchor_rays
                if args.patch_nodes:assert parent['patchNodesSha256']==report['patchNodesSha256']
                assert parent.get('gridOrder',False)==args.grid_order
                assert parent.get('slotPermutation',False)==args.slot_permutation
                assert parent.get('slotRelaxation',False)==args.slot_relaxation
                assert parent.get('graphChannels',0)==args.graph_channels
                if parent.get('featureMatrixSha256'):
                    assert parent['featureMatrixSha256']==report['featureMatrixSha256']
                loaded=model_class.load(args.initial_checkpoint)
                for key in getattr(model_class,'buffers',['mean','scale','negative','positive']):
                    np.testing.assert_allclose(getattr(loaded,key),getattr(model,key),rtol=0,atol=1e-10)
                model=loaded;prior_updates=parent['trainedUpdates']
                report.update(initialCheckpoint=str(args.initial_checkpoint),initialCheckpointSha256=digest(args.initial_checkpoint),priorTrainingUpdates=prior_updates)
            first={key:np.zeros_like(value) for key,value in model.p.items()};second={key:np.zeros_like(value) for key,value in model.p.items()}
            report['separationDag']=args.separation_dag
            if args.separation_dag:
                report.update(nodeOutputDecoder='source-order-separation-dag-v1',nodeQuantizationGradient='straight-through floor-to-cent with ties toward positive infinity',
                    separationConstraints=sum(len(getattr(model,'dag_'+axis+'_parents')) for axis in ['x','y']))
            report['parameterCount']=sum(value.size for value in model.p.values())
            if args.anchor_rays:
                report.update(endpointOutputDecoder='source-interior-anchor-ray-v1',
                    anchorInset=float(model.anchor_inset),degenerateSourceRays=int(model.degenerate_source_rays),
                    portSpan=None)
            if args.patch_nodes:
                report.update(activeNodeOutputs=int(model.node_mask.sum()),activeEndpointOutputs=int(model.port_mask.sum()))
            if args.grid_order:
                report.update(gridShape=model.grid_shape.tolist(),cellSize=model.cell_size.tolist(),
                    jitterRoom=model.jitter_room.tolist(),rankTemperature=float(model.rank_temperature))
            if args.shared_endpoints:
                report['sourceEndpointGroups']=len(model.endpoint_counts)
                report['sharedEndpointGroups']=int(np.count_nonzero(model.endpoint_counts>1))
            report['proxyCandidatePairs']={name:len(getattr(proxy,name)) for name in ['cross_pairs','hit_pairs','near_pairs']} if proxy is not None else {}
            if args.slot_permutation:
                report.update(nodeOutputDecoder='equal-size-source-slot-permutation-v1',
                    shapeGroups=len(model.group_bounds)-1,
                    movableNodes=int(sum(end-begin for begin,end in zip(model.group_bounds[:-1],model.group_bounds[1:]) if end-begin>1)))
            training_start=time.monotonic()
            with (args.out/'joint-batches.jsonl').open('w') as trace, ((args.out/'reward-measurements.jsonl').open('w') if args.reward_es else nullcontext()) as reward_trace:
                if args.reward_es:
                    from joint_reward_training import RewardHeadTrainer,verify_trace,head_hash,head_vector
                    reward_keys=('wo','ewo') if args.reward_endpoint_head else ('wo',)
                    reward_scales=(1.,args.reward_port_scale) if args.reward_endpoint_head else (1.,)
                    report.update(rewardTrainableKeys=list(reward_keys),rewardParameterScales=list(reward_scales))
                    initial_model=args.out/'reward-initial-policy.npz'
                    model.save(initial_model,{**report,'initialForRewardTraining':True,'trainedUpdates':prior_updates})
                    report.update(rewardInitialCheckpoint=str(initial_model),rewardInitialCheckpointSha256=digest(initial_model),
                        rewardHeadParameters=sum(model.p[key].size for key in reward_keys),rewardEncoderFixed=True)
                    reward_trainer=RewardHeadTrainer(model,features,request,action_text,reward_trace,args.seed+31000,
                        args.reward_sigma,args.reward_directions,args.lr,reward_keys,reward_scales,args.reward_cache_size)
                for iteration in range(1,(1 if args.inference_only else args.iterations)+1):
                    progress=max((iteration-1)/max(1,args.iterations-1),(time.monotonic()-training_start)/args.seconds)
                    cooling=min(1.,progress/args.anneal_fraction)
                    temperature=args.temperature_end*(args.temperature_start/args.temperature_end)**(1.-cooling)
                    if args.reward_es:
                        loss=reward_trainer.step() if not args.inference_only else {'total':0.,'inferenceOnly':True}
                    else:
                        slot_temperature=args.slot_temperature_end*(args.slot_temperature_start/args.slot_temperature_end)**(1.-cooling)
                        action,cache=(model.forward_relaxed(features,slot_temperature) if args.slot_relaxation else model.forward(features))
                        delta=action[:len(positions)]
                        decoded=(np.array([[rounded(float(f'{value:.12g}')) for value in row] for row in delta])
                                 if args.quantized_loss else delta)
                        training_positions=positions+decoded
                        if args.neural_perimeter_ports:
                            phase_action=action[len(positions):]
                            if args.quantized_loss:phase_action=np.array([[float(f'{v:.12g}') for v in row] for row in phase_action])
                            provider.set_actions(phase_action,quantized=args.quantized_loss,positions=training_positions)
                        if args.sampled_pairs:proxy.refresh(training_positions)
                        loss,gradient=stress.loss(cache[4]) if args.latent_stress else proxy.loss(training_positions,temperature)
                        if args.slot_relaxation:loss.update(model.relaxation_report)
                        if hard_proxy is not None:
                            hard_loss,hard_gradient=hard_proxy.loss(training_positions)
                            loss['hardGeometryPenalty']=hard_loss['total'];loss['total']+=hard_loss['total']
                            loss['hardGeometryDetails']=hard_loss
                            gradient+=hard_gradient
                        if not args.inference_only:
                            gradients=(model.backward_latent(cache,gradient) if args.latent_stress else model.backward(cache,gradient,provider.action_gradient()) if args.neural_perimeter_ports
                                       else model.backward(cache,gradient))
                            assert math.isfinite(loss['total']) and all(np.isfinite(g).all() for g in gradients.values())
                            norm=math.sqrt(sum(float(np.sum(g*g)) for g in gradients.values()));factor=min(1.,10/max(norm,1e-12))
                            for key in model.keys:
                                gradient=gradients[key]*factor
                                first[key]=.9*first[key]+.1*gradient;second[key]=.999*second[key]+.001*gradient**2
                                model.p[key]-=args.lr*(first[key]/(1-.9**iteration))/(np.sqrt(second[key]/(1-.999**iteration))+1e-8)
                        if args.slot_relaxation:del cache
                    expired=time.monotonic()-training_start>=args.seconds
                    if (iteration%4==0 or iteration==1 or iteration==args.iterations or expired) and (not args.reward_es or args.inference_only or reward_trainer.ever_updated):
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
        assert digest(feature_file)==report['featureMatrixSha256']
        np.testing.assert_array_equal(np.load(feature_file),features)
        for name,value in report['inputHashes'].items():assert digest(args.out/name)==value
        for proposal in proposals:
            assert digest(proposal['checkpoint'])==proposal['checkpointSha256']
            frozen=model_class.load(proposal['checkpoint'])
            assert hashlib.sha256(action_text(frozen.forward(features)[0]).encode()).hexdigest()==proposal['actionSha256']
        assert proposals,'no trained neural output was produced within the budget'
        if args.reward_es:
            assert digest(initial_model)==report['rewardInitialCheckpointSha256']
            initial_network=model_class.load(initial_model)
            expected_heads={proposal['iteration']:head_hash(head_vector(model_class.load(proposal['checkpoint']),reward_keys,reward_scales)) for proposal in proposals}
            for proposal in proposals:
                frozen=model_class.load(proposal['checkpoint'])
                for key in model.keys:
                    if key not in reward_keys:np.testing.assert_array_equal(frozen.p[key],initial_network.p[key])
            checked=verify_trace(initial_network,features,map(json.loads,(args.out/'reward-measurements.jsonl').read_text().splitlines()),action_text,expected_heads)
            assert checked==reward_trainer.measurements
            assert checked==reward_trainer.native_measurements+reward_trainer.cache_hits
            for key in reward_keys:np.testing.assert_array_equal(initial_network.p[key],model.p[key])
            report.update(rewardMeasurementsReplayed=checked,rewardAdamUpdatesReplayed=reward_trainer.iterations,
                rewardNativeEvaluations=reward_trainer.native_measurements,
                rewardMeasurementCacheHits=reward_trainer.cache_hits,
                rewardMeasurementCacheEntries=len(reward_trainer.measurement_cache),
                rewardTraceSha256=digest(args.out/'reward-measurements.jsonl'))
        winning_delta=model_class.load(best_checkpoint).forward(features)[0] if best_checkpoint else None
        verify_geometry(args.out,output,winning_delta,args.attached_ports,args.neural_perimeter_ports)
        stats=json.loads(Path(str(output)+'.stats.json').read_text())
        assert stats['temporaryRegressionBudget']==args.regression_limit
        expected_decoder=('model card translations and boundary-perimeter endpoint offsets' if args.neural_perimeter_ports
                          else 'rigid component translation with every port attached' if args.attached_ports
                          else 'rigid component translation plus center-ray incident ports')
        assert stats['decoder']==expected_decoder
        checkpoint=best_checkpoint or Path(proposals[-1]['checkpoint'])
        policy={'modelKind':'neural-slot-permutation-network' if args.slot_permutation else 'neural-grid-order-network' if args.grid_order else 'joint-node-perimeter-network' if args.neural_perimeter_ports else 'coordinated-displacement-network','untrainedControl':False,
                'networkClass':model_class.__name__,'sharedSourceEndpoints':args.shared_endpoints,
                'rayConditionedPorts':args.ray_conditioned_ports,
                'anchorRays':args.anchor_rays,
                'localizedPatch':bool(args.patch_nodes),'patchNodesSha256':report.get('patchNodesSha256'),
                'rigidPatch':args.rigid_patch,
                'gridOrder':args.grid_order,
                'slotPermutation':args.slot_permutation,'slotRelaxation':args.slot_relaxation,
                'exactRewardTraining':args.reward_es,
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
                      verifiedOutputHashes={name:digest(args.out/name) for name in ['joint-input-features.npy','learned.tsv','learned.tsv.individual','learned.tsv.routes.tsv',
                        'learned.tsv.individual.routes.tsv','learned.tsv.stats.json','learned.tsv.policy.json']})
        (args.out/'joint-worker-result.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'neuralWorker':'complete','frozenBatchesReplayed':len(proposals),'bestCheckpoint':str(best_checkpoint),'neuralSeconds':report['neuralSeconds']}),flush=True)


if __name__=='__main__':main()
