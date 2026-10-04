#!/usr/bin/env python3
"""Export an already replayed neutral model trajectory for another ML policy.

This performs one fixed decoding, not a search. The original strict-best file
remains unchanged. An existing one-point experimental admission allowance lets
the batch environment serialize a tie; exact nonregression is asserted below.
"""
import argparse,json,shutil,subprocess
from pathlib import Path
from types import SimpleNamespace
import numpy as np
from learned_global_replay import INPUT_FILES
from run_anchor_pair_policy import Native,wire,digest
from run_anchor_pair_walk import WalkDecoder,replay,verify_saved


class SnapshotNative(Native):
    def __init__(self,environment,directory,out,overview):
        self.process=subprocess.Popen([str(environment),'--directory',str(directory),'--out',str(out),
            '--overview-only',str(int(overview)),'--neural-perimeter-ports','1','--sparse-scoring','1',
            '--regression-limit','1'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
        self.initial=self.receive();assert self.initial['ready']


def main(args):
    assert args.out.resolve().is_relative_to(Path.cwd()/'.tmp')
    replay(SimpleNamespace(out=args.walk))
    report=json.loads((args.walk/'report.json').read_text());directory=Path(report['sourceDirectory'])
    assert report['admittedWalkStates']>0 and report['walkVisual']<=report['initialVisual']
    source_report=json.loads((directory/'experiment.json').read_text());source=Path(source_report['source'])
    assert digest(source)==source_report['sourceSha256'] and digest(args.payload)==source_report['payloadSha256']
    assert digest(report['environment'])==report['environmentSha256']
    args.out.mkdir(parents=True,exist_ok=False)
    for name in INPUT_FILES:shutil.copyfile(directory/name,args.out/name)
    if report['view']=='overview':shutil.copyfile(directory/'export.json',args.out/'export.json')
    decoder=WalkDecoder(directory);permutation=np.load(args.walk/'walk-permutation.npy',allow_pickle=False)
    action=decoder.decode(permutation);native=SnapshotNative(report['environment'],directory,args.out/'learned.tsv',report['view']=='overview')
    try:
        result=native.request(wire(action))
        assert result['legal'] and result['accepted'] and result['visual']==report['walkVisual']
        assert result['visual']<=native.initial['visual']
        if report['view']=='individual':assert result['individualVisual']<=native.initial['individualVisual']
        assert native.request('SAVE')['saved']
    finally:native.close()
    stats=json.loads((args.out/'learned.tsv.stats.json').read_text());verify_saved(directory,args.out,decoder,action)
    policy={'modelKind':'neural-anchor-pair-walk','untrainedControl':False,'checkpoint':report['checkpoint'],
        'checkpointSha256':report['checkpointSha256'],'sourceDirectory':str(directory),'final':stats,
        'overviewOnly':report['view']=='overview','allowNeutral':True,'newTrainingUpdates':0,
        'proposalAuthority':'frozen pair ranker with deterministic cumulative slot/anchor decoding',
        'trajectoryReport':str(args.walk/'report.json'),'trajectoryReportSha256':digest(args.walk/'report.json'),
        'trajectoryReplaySha256':digest(args.walk/'replay.json'),'exportAdmissionAllowance':1,
        'actualSourceObjectiveRegression':0,'heuristicSearchCalls':0,'coordinateRepairs':0}
    (args.out/'learned.tsv.policy.json').write_text(json.dumps(policy,indent=2)+'\n')
    with (args.out/'product-audit.log').open('x') as log:
        def run(command):subprocess.run(list(map(str,command)),check=True,stdout=log,stderr=log)
        if report['view']=='individual':
            run(['node','--max-old-space-size=128','scripts/erd-poc/audit_individual_policy_experiment.cjs',source,args.payload,args.out])
            audit=json.loads((args.out/'individual.audit.json').read_text())
        else:
            run(['node','--max-old-space-size=128','scripts/erd-poc/apply_learned_components.cjs',source,args.payload,args.out,args.out/'learned.tsv','--experimental'])
            run(['node','--max-old-space-size=128','scripts/erd-poc/audit_leaf_card_connections.cjs',args.out/'candidate.layout.json',args.payload,args.out/'product'])
            audit=json.loads((args.out/'product.audit.json').read_text())
    assert audit['visualCrossings']==report['walkVisual']
    exported={'view':report['view'],'source':str(source),'sourceSha256':digest(source),'payloadSha256':digest(args.payload),
        'visualCrossings':audit['visualCrossings'],'candidateSha256':audit['candidateSha256'],'allChecksPassed':True,
        'promoted':False,'browserVerified':False,'trajectoryReport':str(args.walk/'report.json'),
        'modelTrajectoryReplayed':True,'sourceObjectiveNonregressionVerified':True,
        'codeSha256':{name:digest(Path(__file__).parent/name) for name in ['export_anchor_pair_walk.py',
            'apply_learned_components.cjs','audit_individual_policy_experiment.cjs','audit_leaf_card_connections.cjs']}}
    (args.out/'workflow.json').write_text(json.dumps(exported,indent=2)+'\n');print(json.dumps(exported))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--walk',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    p.add_argument('--payload',required=True,type=Path);main(p.parse_args())
