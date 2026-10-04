"""Replay all weights, wires and full Native rewards for the new cell family."""
import json
from pathlib import Path
import sys
import hashlib
import numpy as np

sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from source_star_cell_policy import SourceStarCellPolicy
from run_source_star_cell_reward import source_vocabulary, KEYS, SCALES
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_anchor_pair_policy import Native, wire
from joint_reward_training import head_vector, set_head_vector, head_hash, adam_head, objective
from geometry_world_model import digest

root = Path('.tmp/visualcross-ml-150-750-20261004/source-star-cell1')
out = root / 'validation1'
out.mkdir(exist_ok=False)
audits = []
for view in ('overview', 'individual'):
    stage = root / f'{view}-learning1'
    report = json.loads((stage / 'report.json').read_text())
    for name, sha in report['codeSha256'].items():
        assert digest(Path('scripts/erd-poc') / name) == sha
    assert digest(report['sourceBinding']) == report['sourceBindingSha256']
    binding = json.loads(Path(report['sourceBinding']).read_text())
    assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
    assert digest(report['environment']) == report['environmentSha256']
    directory = Path(report['sourceDirectory'])
    for name, sha in report['sourceInputs'].items():
        assert digest(directory / name) == sha
    for key, name in [('trainingSha256','training.jsonl'), ('actionsSha256','actions.jsonl'),
            ('observationsSha256','observations.npz'), ('initialModelSha256','initial-model.npz')]:
        assert digest(stage / name) == report[key]
    decoder = WalkDecoder(directory)
    with np.load(stage / 'observations.npz',allow_pickle=False) as saved:
        features = saved['nodes'].copy()
        recorded_active = saved['active'].copy()
    active, support = source_vocabulary(decoder,features,view,report['vocabulary'])
    np.testing.assert_array_equal(active,recorded_active)
    assert support == report['support']
    model = SourceStarCellPolicy(features,decoder,active,report['maxStep'],report['seed'])
    fixed = dict(active=model.active,embedding=model.embedding,w1=model.w1,w2=model.w2,
        mean=model.mean,scale=model.scale,**model.buffers)
    def checkpoint_matches(path, expected_head, step):
        with np.load(path,allow_pickle=False) as saved:
            assert set(saved.files) == set(fixed) | set(model.p) | {'metadata'}
            for key, value in fixed.items():
                np.testing.assert_array_equal(saved[key],value)
            assert int(json.loads(str(saved['metadata']))['trainedUpdates']) == step
            restored = np.concatenate([saved[key].ravel()/scale for key,scale in zip(KEYS,SCALES)])
            np.testing.assert_array_equal(restored,expected_head)
    checkpoint_matches(stage/'initial-model.npz',head_vector(model,KEYS,SCALES),0)
    rows = [json.loads(line) for line in (stage/'training.jsonl').read_text().splitlines()]
    attempts = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
    by_step = {row['step']:row for row in attempts}
    assert len(by_step) == len(attempts) == report['attempts']
    replay = out / view
    replay.mkdir()
    native = Native(Path(report['environment']),directory,replay/'learned.tsv',view=='overview',False)
    measurements = legal_probes = better_legal = accepted = 0
    minimum_legal = report['initialVisual']
    best = best_step = None
    try:
        fresh = np.array([node['features'] for node in native.initial['nodes']])
        np.testing.assert_array_equal(fresh,features)
        zero = model.forward(features)[0]
        assert not np.any(zero)
        assert native.request(wire(zero,'MEASURE')) == report['zeroHeadControl']
        measurements += 1
        first = np.zeros_like(head_vector(model,KEYS,SCALES))
        second = first.copy()
        with (replay/'native-replay.jsonl').open('x') as log:
            for step,row in enumerate(rows,1):
                assert row['iteration'] == step
                assert tuple(row['parameterKeys']) == KEYS and tuple(row['parameterScales']) == SCALES
                assert row['sigma'] == report['initialSigma'] * report['sigmaScheduleMultipliers'][((step-1)//8)%4]
                assert row['rate'] == report['rate']
                base = head_vector(model,KEYS,SCALES)
                assert head_hash(base) == row['baseHeadSha256']
                gradient = np.zeros_like(base)
                assert len(row['directions']) == report['directionsPerUpdate'] == 2
                for direction, probe in enumerate(row['directions']):
                    assert probe['noiseSeed'] == report['seed'] + step*100 + direction
                    noise = np.random.default_rng(probe['noiseSeed']).normal(size=base.shape)
                    scores = []
                    for sign,sample in zip((1.,-1.),probe['samples']):
                        set_head_vector(model,base+sign*row['sigma']*noise,KEYS,SCALES)
                        action,info = model.forward(features)
                        assert not np.any(action[len(decoder.positions):])
                        assert info['movedOwners'] <= 1
                        command = wire(action,'MEASURE')
                        assert hashlib.sha256(command.encode()).hexdigest() == sample['actionSha256']
                        result = native.request(command)
                        assert result == sample['result'],(view,step,direction,sign,result,sample['result'])
                        measurements += 1
                        legal_probes += int(result['legal'])
                        better_legal += int(result['legal'] and result['visual'] < report['initialVisual'])
                        if result['legal']:
                            minimum_legal = min(minimum_legal,result['visual'])
                        scores.append(objective(result))
                        log.write(json.dumps(dict(step=step,direction=direction,sign=sign,result=result,info=info))+'\n')
                    gradient += (scores[0]-scores[1])/(2*row['sigma']*len(row['directions']))*noise
                head,first,second = adam_head(base,gradient,first,second,step,row['rate'])
                set_head_vector(model,head,KEYS,SCALES)
                assert head_hash(head_vector(model,KEYS,SCALES)) == row['trainedHeadSha256']
                if step in by_step:
                    attempted = by_step[step]
                    assert digest(attempted['checkpoint']) == attempted['checkpointSha256']
                    checkpoint_matches(Path(attempted['checkpoint']),head,step)
                    action,info = model.forward(features)
                    command = wire(action)
                    assert hashlib.sha256(command.encode()).hexdigest() == attempted['wireSha256']
                    assert info == attempted['policyInfo']
                    result = native.request(command)
                    assert result == attempted['result']
                    measurements += 1
                    if result['accepted']:
                        accepted += 1
                        best,best_step = action.copy(),step
                    log.write(json.dumps(dict(step=step,trainedCheckpoint=True,result=result,info=info))+'\n')
                if step % 8 == 0:
                    print(json.dumps(dict(view=view,updatesReplayed=step,fullNativeMeasurements=measurements,
                        minimumLegalProbeVisual=minimum_legal,betterLegalProbes=better_legal)),flush=True)
            final_action,final_info = model.forward(features)
            final_result = native.request(wire(final_action,'MEASURE'))
            measurements += 1
            assert not np.any(final_action[len(decoder.positions):])
            model.save(replay/'final-model.npz',dict(kind='source-star-cell-final-replayed',trainedUpdates=len(rows)))
            np.save(replay/'final-action.npy',final_action)
            log.write(json.dumps(dict(finalForward=True,result=final_result,info=final_info))+'\n')
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    assert accepted == report['accepted'] and best_step == report['bestStep']
    assert len(rows) == report['updatesExecuted'] and 4*len(rows) == report['rewardProbes']
    verify_saved(directory,replay,decoder,best)
    if best is not None:
        np.testing.assert_array_equal(best,np.load(stage/'best-action.npy',allow_pickle=False))
    for suffix in ('','.individual','.routes.tsv','.individual.routes.tsv'):
        assert digest(Path(str(replay/'learned.tsv')+suffix)) == digest(Path(str(stage/'learned.tsv')+suffix))
    stats = json.loads((replay/'learned.tsv.stats.json').read_text())
    assert stats['visual'] == report['final']['visual']
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    audit = dict(view=view,status='pass',updatesReplayed=len(rows),
        teacherWiresReplayed=report['rewardProbes'],fullNativeMeasurements=measurements,
        allTeacherLabelsIndependentlyRemeasured=True,legalTeacherProbes=legal_probes,
        minimumLegalProbeVisual=minimum_legal,betterLegalProbes=better_legal,
        trainedTryOutputs=len(attempts),accepted=accepted,bestStep=best_step,
        sourceVisual=report['initialVisual'],savedBestVisual=stats['visual'],
        savedBestGeometryExactlyReplayed=True,checkpointBuffersExactlyMatched=True,
        finalNetworkForward=final_result,finalNetworkInfo=final_info,
        finalCheckpointSha256=digest(replay/'final-model.npz'),finalActionSha256=digest(replay/'final-action.npy'),
        finalWireSha256=hashlib.sha256(wire(final_action,'MEASURE').encode()).hexdigest(),
        reportSha256=digest(stage/'report.json'),nativeReplaySha256=digest(replay/'native-replay.jsonl'))
    audits.append(audit)
    print(json.dumps(audit),flush=True)
proof = dict(kind='full-native-source-star-cell-family-replay-v1',status='pass',views=audits,
    totalUpdates=sum(x['updatesReplayed'] for x in audits),totalTeacherProbes=sum(x['teacherWiresReplayed'] for x in audits),
    totalFullNativeMeasurements=sum(x['fullNativeMeasurements'] for x in audits),
    allTeacherLabelsIndependentlyRemeasured=True,codeSha256=digest(__file__),
    newTrainingUpdates=0,nativeCoordinateSearchOrRepairs=0,promoted=False,browserVerified=False)
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({k:proof[k] for k in ('status','totalUpdates','totalTeacherProbes','totalFullNativeMeasurements')}),flush=True)
