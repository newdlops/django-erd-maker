#!/usr/bin/env python3
"""Replay real native rewards without rerunning geometry measurements."""
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import time

ROOT=Path.cwd()
sys.path.insert(0,str(ROOT/'scripts/erd-poc'))
import numpy as np
from joint_slot_anchor_policy import SlotAnchorRayPolicy
from joint_reward_training import RewardHeadTrainer,verify_trace,head_hash,head_vector
from run_joint_neural_layout import action_text

BASE=ROOT/'.tmp/visualcross-ml-150-750-20261003'
OUT=BASE/'reward-cache-validation1'
OUT.mkdir(exist_ok=False)
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
names=['joint_reward_training.py','run_joint_neural_layout.py','joint_slot_anchor_policy.py',
       'joint_slot_policy.py','joint_anchor_ray_policy.py','joint_separation_policy.py','joint_neural_ports.py']
sources={name:digest(ROOT/'scripts/erd-poc'/name) for name in names}
for name in names:shutil.copyfile(ROOT/'scripts/erd-poc'/name,OUT/('source-'+name))
shutil.copyfile(__file__,OUT/'validation-driver.py')
report={'allChecksPassed':False,'sourceHashes':sources,'newNativeGeometryEvaluations':0,
        'cases':[],'fixtureChecks':{},'limitation':'Recorded native results establish trajectory parity; this is not a wall-clock speedup measurement.'}
started=time.monotonic()

def case(name,cache_size):
    directory=BASE/name
    workflow=directory/('workflow.json' if name.startswith('overview') else 'workflow.audit.json')
    metadata=json.loads(workflow.read_text())
    assert metadata['allChecksPassed'] and metadata['unchangedSourceVerified']
    assert metadata['rewardTraceSha256']==digest(directory/'reward-measurements.jsonl')
    assert metadata['rewardInitialCheckpointSha256']==digest(directory/'reward-initial-policy.npz')
    records=[json.loads(line) for line in (directory/'reward-measurements.jsonl').read_text().splitlines()]
    oracle={}
    for record in records:
        for direction in record['directions']:
            for sample in direction['samples']:
                key=sample['actionSha256']
                if key in oracle:assert oracle[key]==sample['result']
                oracle[key]=sample['result']
    model=SlotAnchorRayPolicy.load(directory/'reward-initial-policy.npz')
    features=np.load(directory/'joint-input-features.npy',allow_pickle=False)
    calls=[]
    def request(command):
        assert command.startswith('MEASURE ')
        key=hashlib.sha256(command.encode()).hexdigest()
        assert key in oracle
        calls.append(key)
        return copy.deepcopy(oracle[key])
    trace=OUT/f'{name}-cache{cache_size}.jsonl'
    with trace.open('x') as stream:
        trainer=RewardHeadTrainer(model,features,request,action_text,stream,metadata['seed']+31000,
            metadata['rewardSigma'],metadata['rewardDirections'],metadata['learningRate'],
            metadata['rewardTrainableKeys'],metadata['rewardParameterScales'],cache_size)
        for record in records:
            trainer.step()
            assert head_hash(head_vector(model,trainer.keys,trainer.scales))==record['trainedHeadSha256']
    cached_records=[json.loads(line) for line in trace.read_text().splitlines()]
    for old,new in zip(records,cached_records):
        stripped=copy.deepcopy(new)
        stripped.pop('measurementCacheSize',None)
        for direction in stripped['directions']:
            for sample in direction['samples']:sample.pop('cacheHit',None)
        assert old==stripped
    assert len(records)==len(cached_records)
    assert trainer.native_measurements==len(calls)
    assert trainer.native_measurements+trainer.cache_hits==trainer.measurements
    if cache_size==128:assert len(calls)==len(oracle)
    if not cache_size:assert trainer.cache_hits==0 and len(calls)==trainer.measurements
    assert len(trainer.measurement_cache)<=cache_size
    replay=SlotAnchorRayPolicy.load(directory/'reward-initial-policy.npz')
    checked=verify_trace(replay,features,cached_records,action_text)
    assert checked==metadata['rewardMeasurementsReplayed']
    frozen_record=json.loads((directory/'joint-batches.jsonl').read_text().splitlines()[-1])
    assert frozen_record['iteration']==len(records)
    assert digest(frozen_record['checkpoint'])==frozen_record['checkpointSha256']
    frozen=SlotAnchorRayPolicy.load(frozen_record['checkpoint'])
    for key in model.p:
        np.testing.assert_array_equal(replay.p[key],model.p[key])
        np.testing.assert_array_equal(frozen.p[key],model.p[key])
    final_action=hashlib.sha256(action_text(model.forward(features)[0]).encode()).hexdigest()
    assert final_action==frozen_record['actionSha256']
    result={'stage':name,'cacheSize':cache_size,'logicalProbes':trainer.measurements,
        'recordedResultLookups':len(calls),'cacheHits':trainer.cache_hits,
        'distinctRecordedActions':len(oracle),'adamUpdatesExactlyMatched':len(records),
        'allTraceFieldsUnchangedExceptCacheAccounting':True,'allFinalParametersExactlyMatched':True,
        'finalActionSha256':final_action,'sourceWorkflowSha256':digest(workflow),
        'sourceRewardTraceSha256':digest(directory/'reward-measurements.jsonl'),
        'replayTraceSha256':digest(trace),'peakAllowedEntries':cache_size}
    report['cases'].append(result)
    print(json.dumps(result),flush=True)
    return cached_records,features,directory

try:
    for name in ['individual-slot-anchor1','overview-slot-anchor1','individual-slot-graph1']:
        case(name,128)
    case('individual-slot-anchor1',0)
    records,features,directory=case('overview-slot-anchor1',2)
    # A real recorded cache hit must be rejected if its result or hit flag is changed.
    bad=copy.deepcopy(records)
    bad[0]['directions'][0]['samples'][0]['cacheHit']=True
    try:verify_trace(SlotAnchorRayPolicy.load(directory/'reward-initial-policy.npz'),features,bad,action_text)
    except AssertionError:pass
    else:raise AssertionError('false cache hit was accepted')
    bad=copy.deepcopy(records);changed=False
    for record in bad:
        for direction in record['directions']:
            for sample in direction['samples']:
                if sample['cacheHit'] and not changed:
                    sample['result']['visual']+=1;changed=True
    assert changed
    try:verify_trace(SlotAnchorRayPolicy.load(directory/'reward-initial-policy.npz'),features,bad,action_text)
    except AssertionError:pass
    else:raise AssertionError('changed cached measurement was accepted')
    class Fixture:
        def __init__(self):self.p={'wo':np.zeros((1,1))}
    requests=[]
    def request(command):
        requests.append(command)
        return {'measureOnly':True,'accepted':False,'legal':True,'visual':ord(command[-1]),'extra':[7]}
    trainer=RewardHeadTrainer(Fixture(),None,request,None,io.StringIO(),1,.001,1,.01,cache_size=2)
    commands=['MEASURE a','MEASURE b','MEASURE a','MEASURE c','MEASURE b']
    hits=[]
    for command in commands:
        result,hit=trainer.measure(command)
        assert result['visual']==ord(command[-1]) and result['extra']==[7]
        result['visual']=-1;result['extra'][0]=-1
        hits.append(hit)
    assert hits==[False,False,True,False,False] and len(requests)==4
    before=len(requests)
    try:trainer.measure('TRY a')
    except AssertionError:pass
    else:raise AssertionError('TRY reached the measurement cache')
    assert before==len(requests)
    second=RewardHeadTrainer(Fixture(),None,request,None,io.StringIO(),1,.001,1,.01,cache_size=2)
    assert not second.measure('MEASURE b')[1]
    for size in [-1,129]:
        try:RewardHeadTrainer(Fixture(),None,request,None,io.StringIO(),1,.001,1,.01,cache_size=size)
        except AssertionError:pass
        else:raise AssertionError('unbounded cache was accepted')
    report['fixtureChecks']={'lruEviction':True,'returnedResultMutationIsolation':True,
        'trainerSourceIsolation':True,'tryRejectedBeforeRequest':True,'invalidSizesRejected':True,
        'tamperedCacheHitRejected':True,'tamperedCachedResultRejected':True}
    assert all(digest(ROOT/'scripts/erd-poc'/name)==value for name,value in sources.items())
    report['allChecksPassed']=True
finally:
    report['seconds']=time.monotonic()-started
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
