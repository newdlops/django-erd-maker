#!/usr/bin/env python3
"""Export -> frozen neural policy -> action replay -> product apply/load audit.

Invoke under run_memory_bounded.py. Every subprocess runs serially in the
same monitored process group. There is no heuristic-optimizer fallback.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--payload',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--checkpoint',type=Path,default=Path('data/erd-poc/checkpoints/leaf-card-policy-v1.npz'))
    p.add_argument('--budget',type=int,default=12000)
    p.add_argument('--seed',type=int,default=123)
    p.add_argument('--components',action='store_true',help='learned rigid components with both complete views')
    p.add_argument('--environment',type=Path,help='reuse a previously compiled environment; its hash is recorded')
    p.add_argument('--observations',type=int,default=192)
    p.add_argument('--canonical-start',action='store_true',help='experimental center-ray representation; final must still beat source')
    p.add_argument('--experimental',action='store_true',help='retain an unpromoted .tmp snapshot for further learned rollouts')
    p.add_argument('--overview-only',action='store_true',help='isolated overview objective; requires --experimental')
    p.add_argument('--allow-area-growth',action='store_true',help='bounded neural spacing experiment; requires --experimental')
    p.add_argument('--pair-policy',action='store_true')
    p.add_argument('--allow-neutral',action='store_true')
    p.add_argument('--branch-moves',action='store_true')
    p.add_argument('--area-exploration',action='store_true')
    p.add_argument('--exploration-limit',type=int,default=0)
    p.add_argument('--temperature',type=float,default=2.)
    args = p.parse_args()
    if (args.experimental or args.canonical_start) and not args.components:
        p.error('experimental geometry modes require --components')
    if args.overview_only and not args.experimental:
        p.error('--overview-only requires --experimental')
    if args.allow_area_growth and not args.experimental:
        p.error('--allow-area-growth requires --experimental')
    if args.pair_policy and (not args.components or not args.environment or args.canonical_start):
        p.error('pair policy requires --components and its compiled --environment, without a representation reset')
    if args.pair_policy and (args.budget > 2048 or args.allow_area_growth):
        p.error('pair policy requires a budget <= 2048 and uses the fixed source frame')
    if args.allow_neutral and (not args.experimental or args.pair_policy):
        p.error('neutral admission requires an experimental component-policy stage')
    if args.branch_moves and (not args.components or not args.experimental or args.pair_policy or args.canonical_start or args.allow_area_growth):
        p.error('branch contexts require isolated component translations without a representation reset')
    if args.area_exploration and (not args.experimental or not args.allow_area_growth or args.allow_neutral or args.pair_policy):
        p.error('area exploration requires an isolated area-growth stage')
    if args.exploration_limit and (not args.allow_neutral or args.area_exploration or args.pair_policy or args.canonical_start
                                  or not 0 < args.exploration_limit <= 200 or not 0 < args.temperature <= 10):
        p.error('exploratory admission requires a neutral component policy, limit <= 200, and temperature <= 10')
    if os.environ.get('OMP_NUM_THREADS') != '1':
        p.error('run this command inside scripts/erd-poc/run_memory_bounded.py')
    if not 1 <= args.budget <= 20000:
        p.error('proposal budget must be between 1 and 20000')
    assert args.source.is_file() and args.payload.is_file() and args.checkpoint.is_file()
    assert not (args.out/'candidate.layout.json').exists(), 'preserve an existing candidate'
    args.out.mkdir(parents=True,exist_ok=True)
    start, stages = time.monotonic(), []
    def run(stage, command):
        before = time.monotonic()
        with (args.out/(stage+'.stdout')).open('wb') as stdout, (args.out/(stage+'.stderr')).open('wb') as stderr:
            result = subprocess.run(command,cwd=ROOT,stdout=stdout,stderr=stderr,check=False)
        stages.append({'stage':stage,'exitCode':result.returncode,'seconds':time.monotonic()-before})
        print(json.dumps(stages[-1]),flush=True)
        if result.returncode:
            raise RuntimeError(f'{stage} failed; inspect {args.out/(stage+".stderr")}')
    binary = args.environment or args.out/'policy-environment'
    if args.environment:
        assert binary.is_file()
    else:
        run('compile',['nice','-n','10','c++','-std=c++17','-O2','-ffp-contract=off',
            'scripts/erd-poc/ml_component_environment.cpp' if args.components else 'scripts/erd-poc/ml_card_environment.cpp','-o',str(binary)])
    run('export',['node','scripts/erd-poc/probe_overview_boundary.cjs','export',str(args.source),str(args.payload),str(args.out),
                  '--max-routes','256','--with-individual'])
    if args.components:
        run('components',['node','scripts/erd-poc/export_learned_components.cjs',str(args.source),str(args.payload),str(args.out)])
    branch_map=None
    if args.branch_moves:
        from learned_branch_map import prepare_branch_map
        branch_map=prepare_branch_map(args.out,digest(args.source))
    proposal = args.out/'learned.tsv'
    python = str(ROOT/'.venv-ml/bin/python')
    learner='scripts/erd-poc/learn_pair_policy.py' if args.pair_policy else 'scripts/erd-poc/learn_card_policy.py'
    run('inference',['nice','-n','10',python,learner,'infer',
        '--checkpoint',str(args.checkpoint),'--environment',str(binary),'--directory',str(args.out),'--out',str(proposal),
        '--budget',str(args.budget),'--rounds','16','--seconds','20','--seed',str(args.seed),'--observations',str(args.observations),
        *(['--canonical-start'] if args.canonical_start else []),
        *(['--overview-only'] if args.overview_only else []),
        *(['--allow-neutral'] if args.allow_neutral else []),
        *(['--branch-moves'] if args.branch_moves else []),
        *(['--area-exploration'] if args.area_exploration else []),
        *(['--exploration-limit',str(args.exploration_limit),'--temperature',str(args.temperature)] if args.exploration_limit else [])])
    run('replay',[python,learner,'replay','--checkpoint',str(args.checkpoint),'--out',str(proposal)])
    run('apply',['node','scripts/erd-poc/apply_learned_components.cjs' if args.components else 'scripts/erd-poc/apply_dual_node_proposal.cjs',
        str(args.source),str(args.payload),str(args.out),str(proposal),*(['--experimental'] if args.experimental else []),
        *(['--allow-area-growth'] if args.allow_area_growth else [])])
    run('product',['node','scripts/erd-poc/audit_leaf_card_connections.cjs',str(args.out/'candidate.layout.json'),str(args.payload),str(args.out/'product')])
    audit = json.loads((args.out/'product.audit.json').read_text())
    policy = json.loads(Path(str(proposal)+'.policy.json').read_text())
    assert not policy['untrainedControl'] and policy['heuristicSearchCalls'] == 0
    if not args.experimental:
        assert audit['visualCrossings'] <= policy['initial'].get('sourceVisual',policy['initial']['visual'])
        assert audit['individualVisualCrossings'] <= policy['initial'].get('sourceIndividualVisual',policy['initial']['individualVisual'])
        assert (audit['visualCrossings'] < policy['initial'].get('sourceVisual',policy['initial']['visual'])
                or audit['individualVisualCrossings'] < policy['initial'].get('sourceIndividualVisual',policy['initial']['individualVisual']))
    report = {'source':str(args.source),'sourceSha256':digest(args.source),
        'payloadSha256':digest(args.payload),'checkpoint':str(args.checkpoint),'checkpointSha256':digest(args.checkpoint),
        'nativeBinarySha256':digest(binary),'candidateSha256':audit['candidateSha256'],
        'visualCrossings':audit['visualCrossings'],'individualVisualCrossings':audit['individualVisualCrossings'],
        'proposalAuthority':'frozen learned policy','heuristicSearchCalls':0,'frozenActionReplay':True,
        'productFileLoadVerified':True,'browserVerified':False,'experimental':args.experimental,'allowAreaGrowth':args.allow_area_growth,
        'allowNeutral':args.allow_neutral,'pairPolicy':args.pair_policy,
        'branchMoves':args.branch_moves,'branchMap':branch_map,
        'areaExploration':args.area_exploration,
        'explorationLimit':args.exploration_limit,'initialTemperature':args.temperature,
        'seconds':time.monotonic()-start,'stages':stages}
    (args.out/'workflow.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)


if __name__ == '__main__':
    main()
