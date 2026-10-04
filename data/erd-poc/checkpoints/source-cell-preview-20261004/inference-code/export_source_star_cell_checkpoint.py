"""Replay source-cell training and export the actual final neural head."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import numpy as np
from geometry_world_model import digest
from joint_reward_training import head_vector, set_head_vector, head_hash, verify_trace
from source_star_cell_policy import SourceStarCellPolicy
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder, verify_saved
from single_owner_cached_observer_v2 import CachedObserver


def save_json(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


def run(args):
    assert os.environ.get('OMP_NUM_THREADS') == '1'
    report = json.loads((args.stage / 'report.json').read_text())
    directory = Path(report['sourceDirectory'])
    for name, sha in report['sourceInputs'].items():
        assert digest(directory / name) == sha
    for name, sha in report['codeSha256'].items():
        assert digest(Path(__file__).parent / name) == sha
    for key in ('environment','sourceBinding'):
        assert digest(report[key]) == report[key + 'Sha256']
    binding = json.loads(Path(report['sourceBinding']).read_text())
    assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
    for name,key in [('training.jsonl','trainingSha256'),('actions.jsonl','actionsSha256'),
                     ('observations.npz','observationsSha256'),('initial-model.npz','initialModelSha256')]:
        assert digest(args.stage / name) == report[key]
    decoder = WalkDecoder(directory)
    with np.load(args.stage / 'observations.npz', allow_pickle=False) as saved:
        nodes = saved['nodes'].copy()
    model = SourceStarCellPolicy(nodes,decoder,report['support']['owners'],report['maxStep'],report['seed'])
    rows = [json.loads(line) for line in (args.stage / 'training.jsonl').read_text().splitlines()]
    actions = [json.loads(line) for line in (args.stage / 'actions.jsonl').read_text().splitlines()]
    assert len(rows) == report['updatesExecuted']
    args.out.mkdir(parents=True, exist_ok=False)
    model.save(args.out / 'reconstructed-initial.npz', dict(trainedUpdates=0))
    with np.load(args.out / 'reconstructed-initial.npz', allow_pickle=False) as saved:
        fixed = {key:saved[key].copy() for key in saved.files if key not in model.p and key != 'metadata'}
    expected_heads = {}
    for step,path in [(0,args.stage / 'initial-model.npz')] + [(row['step'],Path(row['checkpoint'])) for row in actions]:
        with np.load(path, allow_pickle=False) as saved:
            assert set(saved.files) == set(fixed) | set(model.p) | {'metadata'}
            for key,value in fixed.items():
                assert value.dtype == saved[key].dtype and np.array_equal(value,saved[key]), (step,key)
            assert json.loads(str(saved['metadata']))['trainedUpdates'] == step
            for key in model.p:
                assert saved[key].shape == model.p[key].shape and saved[key].dtype == model.p[key].dtype
                model.p[key] = saved[key].copy()
            if step:
                expected_heads[step] = head_hash(head_vector(model,('wo','bo'),(1.,1.)))
            else:
                assert not np.any(head_vector(model,('wo','bo'),(1.,1.)))
    set_head_vector(model,np.zeros(27),('wo','bo'),(1.,1.))
    for step,row in enumerate(rows,1):
        assert row['sigma'] == report['initialSigma'] * report['sigmaScheduleMultipliers'][((step-1)//8)%4]
        assert row['rate'] == report['rate']
        assert [p['noiseSeed'] for p in row['directions']] == [report['seed']+step*100+i for i in range(2)]
    probes = verify_trace(model,nodes,rows,wire,expected_heads)
    assert probes == report['rewardProbes']
    final_head = head_vector(model,('wo','bo'),(1.,1.)).copy()
    checkpoint = args.out / f"model-step-{len(rows):03}.npz"
    model.save(checkpoint,dict(kind=report['kind'],trainedUpdates=len(rows),finalTrainingHead=True))
    native = Native(Path(report['environment']),directory,args.out/'unused.tsv',report['view']=='overview',False)
    full_rewards = 0
    try:
        assert np.array_equal(nodes,np.array([n['features'] for n in native.initial['nodes']]))
        set_head_vector(model,np.zeros(27),('wo','bo'),(1.,1.))
        assert native.request(wire(model.forward(nodes)[0],'MEASURE')) == report['zeroHeadControl']
        for index in sorted({0,len(rows)-1}):
            set_head_vector(model,np.zeros(27),('wo','bo'),(1.,1.))
            verify_trace(model,nodes,rows[:index],wire)
            base = head_vector(model,('wo','bo'),(1.,1.)).copy()
            row = rows[index]
            for probe in row['directions']:
                noise = np.random.default_rng(probe['noiseSeed']).normal(size=27)
                for sign,sample in zip((1.,-1.),probe['samples']):
                    set_head_vector(model,base+sign*row['sigma']*noise,('wo','bo'),(1.,1.))
                    command = wire(model.forward(nodes)[0],'MEASURE')
                    assert hashlib.sha256(command.encode()).hexdigest() == sample['actionSha256']
                    assert native.request(command) == sample['result']
                    full_rewards += 1
        best = None
        for row in actions:
            assert digest(row['checkpoint']) == row['checkpointSha256']
            with np.load(row['checkpoint'],allow_pickle=False) as saved:
                model.p = {key:saved[key].copy() for key in ('wo','bo')}
            action,info = model.forward(nodes)
            assert info == row['policyInfo']
            assert hashlib.sha256(wire(action).encode()).hexdigest() == row['wireSha256']
            measured = native.request(wire(action))
            assert measured == row['result']
            if measured['accepted']:
                best = action.copy()
        verify_saved(directory,args.stage,decoder,best)
        set_head_vector(model,final_head,('wo','bo'),(1.,1.))
        proposed,info = model.forward(nodes)
        result = native.request(wire(proposed,'MEASURE'))
        assert result['legal'] and result['hard'] == result['individualHard'] == result['spacing'] == 0
    finally:
        native.close()
    observer = CachedObserver(directory,decoder)
    physical,full,physical_lines,full_lines,_ = observer.decode(proposed)
    proposal = args.out / 'learned.tsv'
    def write_rows(path,ids,values,routes=False):
        assert len(ids) == len(values)
        with path.open('x') as stream:
            for key,value in zip(ids,values):
                text = (' '.join(','.join(format(float(v),'.17g') for v in point) for point in value)
                        if routes else '\t'.join(format(float(v),'.17g') for v in value))
                stream.write(key+'\t'+text+'\n')
    ids = lambda name:[line.split('\t')[0] for line in (directory/name).read_text().splitlines()]
    write_rows(proposal,ids('nodes.tsv'),physical)
    write_rows(Path(str(proposal)+'.individual'),observer.full_ids,full)
    write_rows(Path(str(proposal)+'.routes.tsv'),ids('groups.tsv'),physical_lines,True)
    write_rows(Path(str(proposal)+'.individual.routes.tsv'),observer.edge_ids,full_lines,True)
    np.save(args.out/'action.npy',proposed)
    stats = dict(initialVisual=report['initialVisual'],visual=result['visual'],
        initialIndividualVisual=report['initialIndividualVisual'],individualVisual=result['individualVisual'],
        initialHardConditions=0,hardConditions=0,initialIndividualHardConditions=0,individualHardConditions=0,
        overlap=0,spacing=0,heuristicSearchCalls=0,proposalAuthority='external-policy',
        latestCheckpointPreview=True,strictBestFallback=False)
    save_json(Path(str(proposal)+'.stats.json'),stats)
    save_json(Path(str(proposal)+'.policy.json'),dict(modelKind=report['kind'],checkpoint=str(checkpoint),
        checkpointSha256=digest(checkpoint),trainedUpdates=len(rows),untrainedControl=False,newTrainingUpdates=0,final=stats))
    exported = dict(view=report['view'],sourceFile=str(args.source_layout),sourceSha256=digest(args.source_layout),
        payloadFile=str(args.payload),payloadSha256=digest(args.payload),stage=str(args.stage),stageReportSha256=digest(args.stage/'report.json'),
        checkpoint=str(checkpoint),checkpointSha256=digest(checkpoint),trainedUpdates=len(rows),modelKind=report['kind'],
        finalForwardWireSha256=hashlib.sha256(wire(proposed).encode()).hexdigest(),fullNativeMeasurement=result,
        immutableCheckpointAndDecoderArraysMatched=True,sourceObservationsMatched=True,latestNetworkForwardExported=True,
        strictBestFallback=False,newTrainingUpdates=0,coordinateSearchOrRepairs=0,policyInfo=info,
        allAdamUpdatesReplayed=len(rows),allRewardProbeWiresReplayed=probes,fullRewardMeasurements=full_rewards,
        teacherLabelsReplayedWithoutFullRemeasurement=probes-full_rewards,fullTrainedTryOutputs=len(actions),
        fullNativeMeasurements=2+full_rewards+len(actions),exporterSha256=digest(__file__))
    save_json(args.out/'export.json',exported)
    print(json.dumps(dict(view=report['view'],visual=result['visual'],individualVisual=result['individualVisual'],
        checkpointStep=len(rows),replayedUpdates=len(rows),replayedProbeWires=probes,fullNativeMeasurements=exported['fullNativeMeasurements'])),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('stage','source-layout','payload','out'):
        parser.add_argument('--'+name,type=Path,required=True)
    run(parser.parse_args())
