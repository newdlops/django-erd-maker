"""Independently replay source calibration, all controls and learned selection."""
import ast
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0,str(Path('scripts/erd-poc').resolve()))
from source_star_cell_policy import SourceStarCellPolicy
from run_source_star_cell_reward import source_vocabulary
from run_anchor_pair_walk import WalkDecoder,verify_saved
from run_anchor_pair_policy import Native,wire
from learn_pair_policy import Ranker,KEYS as RANK_KEYS
from joint_reward_training import head_vector,set_head_vector
from geometry_world_model import digest

helper = Path('.tmp/run_source_cell_control_learning.py')
# Execute only the two pure training definitions. The source runner's main
# block would create records and start jobs, so it is intentionally not imported.
tree = ast.parse(helper.read_text())
definitions = [node for node in tree.body if isinstance(node,ast.FunctionDef)]
assert {node.name for node in definitions} == {'parameter_hash','train_selector'}
namespace = dict(np=np,hashlib=hashlib,json=json,Ranker=Ranker,RANK_KEYS=RANK_KEYS)
exec(compile(ast.Module(body=definitions,type_ignores=[]),str(helper),'exec'),namespace)
root = Path('.tmp/visualcross-ml-150-750-20261004/source-star-cell1')
out = root/'controls-validation1'
out.mkdir(exist_ok=False)
audits = []
for name in ('toy-controls1','overview-controls1','individual-controls1'):
    stage = root/name
    report = json.loads((stage/'report.json').read_text())
    for path,sha in report['codeSha256'].items():
        assert digest(path)==sha
    directory = Path(report['sourceDirectory'])
    for path,sha in report['sourceInputs'].items():
        assert digest(directory/path)==sha
    environment = Path('.tmp/visualcross-ml-150-750-20261003/pair-neutral-walk1/environment')
    assert digest(environment)==report['environmentSha256']
    assert digest(stage/'teacher-inputs.npz')==report['teacherInputSha256']
    assert digest(stage/'teachers.jsonl')==report['teacherTraceSha256']
    assert digest(stage/'observations.npz')==report['observationsSha256']
    if report['sourceBindingSha256']:
        binding_path = Path('.tmp/visualcross-ml-150-750-20261004/geometry-world1/source-binding.json')
        assert digest(binding_path)==report['sourceBindingSha256']
        binding = json.loads(binding_path.read_text())
        assert report['sourceInputs']==binding['viewSources'][report['view']]['inputs']
        assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json')==binding['promotedCandidateSha256']
    decoder = WalkDecoder(directory)
    with np.load(stage/'observations.npz',allow_pickle=False) as saved:
        nodes,recorded_active = saved['nodes'].copy(),saved['active'].copy()
    active,support = source_vocabulary(decoder,nodes,report['view'],12)
    np.testing.assert_array_equal(active,recorded_active)
    assert support==report['support']
    model = SourceStarCellPolicy(nodes,decoder,active,2048.,report['seed'])
    fixed = dict(active=active,embedding=model.embedding,w1=model.w1,w2=model.w2,
        mean=model.mean,scale=model.scale,**model.buffers,**model.p)
    with np.load(stage/'initial-cell-model.npz',allow_pickle=False) as saved:
        assert set(saved.files)==set(fixed)|{'metadata'}
        for key,value in fixed.items():
            np.testing.assert_array_equal(saved[key],value)
    with np.load(stage/'teacher-inputs.npz',allow_pickle=False) as saved:
        features,heads,gains = saved['features'].copy(),saved['heads'].copy(),saved['gains'].copy()
    teachers = [json.loads(line) for line in (stage/'teachers.jsonl').read_text().splitlines()]
    assert len(teachers)==4*len(active)==report['teacherControlCount']
    replay = out/name
    replay.mkdir()
    native = Native(environment,directory,replay/'learned.tsv',report['view']=='overview',False)
    measurements = legal = better = accepted = 0
    best = None
    minimum_legal = report['initialVisual']
    try:
        np.testing.assert_array_equal(nodes,np.array([row['features'] for row in native.initial['nodes']]))
        zero = model.forward(nodes)[0]
        assert not np.any(zero)
        baseline = native.request(wire(zero,'MEASURE'))
        assert baseline['legal'] and baseline['visual']==report['initialVisual']
        measurements += 1
        with (replay/'native-replay.jsonl').open('x') as log:
            for target in range(len(active)):
                selector = np.zeros(8)
                for step in range(256):
                    logits = model.embedding@selector
                    probability = np.exp(logits-logits.max())
                    probability /= probability.sum()
                    selector -= .5*(model.embedding.T@probability-model.embedding[target])
                for offset,phase in enumerate((0.,.25,.5,.75)):
                    index = target*4+offset
                    row = teachers[index]
                    assert row['index']==index and row['sourceTarget']==target and row['phase']==phase
                    model.p = dict(wo=np.zeros((8,3)),bo=np.array([0.,3.,
                        -50. if phase==0 else np.arctanh(2*phase-1)]))
                    model.p['wo'][:,0] = selector
                    np.testing.assert_array_equal(head_vector(model,('wo','bo'),(1.,1.)),heads[index])
                    action,info = model.forward(nodes)
                    assert info==row['info'] and info['selectedVocabularyIndex']==target
                    feature = np.r_[nodes[int(active[target])],np.sin(2*np.pi*phase),np.cos(2*np.pi*phase),
                        info['amplitude'],action[int(active[target])]/2048.,info['sourceCellCapacityPixels']/2048.]
                    np.testing.assert_array_equal(feature,features[index])
                    command = wire(action,'MEASURE')
                    assert hashlib.sha256(command.encode()).hexdigest()==row['wireSha256']
                    result = native.request(command)
                    assert result==row['result']
                    measurements += 1
                    gain = report['initialVisual']-result['visual'] if result['legal'] else -1000.
                    assert gain==gains[index]
                    legal += int(result['legal'])
                    better += int(result['legal'] and gain>0)
                    if result['legal']:
                        minimum_legal = min(minimum_legal,result['visual'])
                    log.write(json.dumps(dict(index=index,result=result,info=info))+'\n')
            if np.any(gains>0):
                ranker,initial = namespace['train_selector'](features,gains,report['seed']+1,replay/'ranker-training.jsonl')
                assert digest(replay/'ranker-training.jsonl')==digest(stage/'ranker-training.jsonl')
                with np.load(stage/'ranker.npz',allow_pickle=False) as saved:
                    for key in RANK_KEYS:
                        np.testing.assert_array_equal(saved[key],ranker.p[key])
                        np.testing.assert_array_equal(saved['initial__'+key],initial[key])
                    np.testing.assert_array_equal(saved['mean'],ranker.mean)
                    np.testing.assert_array_equal(saved['scale'],ranker.scale)
                scores = ranker.forward(features)[0]
                np.testing.assert_array_equal(scores,np.load(stage/'scores.npy',allow_pickle=False))
                index = int(np.argmax(scores))
                assert index==report['selectedIndex']
                selected = json.loads((stage/'selected.json').read_text())
                assert selected['index']==index and selected['score']==float(scores[index])
                set_head_vector(model,heads[index],('wo','bo'),(1.,1.))
                action,info = model.forward(nodes)
                command = wire(action)
                assert hashlib.sha256(command.encode()).hexdigest()==selected['wireSha256']
                result = native.request(command)
                assert result==selected['result']==report['trainedResult']
                assert info==selected['info']
                measurements += 1
                accepted = int(result['accepted'])
                if accepted:
                    best = action.copy()
                    np.testing.assert_array_equal(best,np.load(stage/'best-action.npy',allow_pickle=False))
                log.write(json.dumps(dict(learnedSelection=True,index=index,result=result,info=info))+'\n')
                assert report['rankerTrainingUpdates']==300
            else:
                assert report['rankerTrainingUpdates']==0 and report['selectedIndex'] is None
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    assert legal==report['legalTeacherControls'] and better==report['betterLegalTeacherControls']
    assert accepted==report['accepted']
    verify_saved(directory,replay,decoder,best)
    for suffix in ('','.individual','.routes.tsv','.individual.routes.tsv'):
        assert digest(Path(str(replay/'learned.tsv')+suffix))==digest(Path(str(stage/'learned.tsv')+suffix))
    stats = json.loads((replay/'learned.tsv.stats.json').read_text())
    assert stats['visual']==report['final']['visual']
    assert stats['hardConditions']==stats['individualHardConditions']==stats['spacing']==stats['overlap']==0
    audit = dict(stage=name,status='pass',teacherControlsReplayed=len(teachers),
        allTeacherLabelsIndependentlyRemeasured=True,sourcePrototypeCalibrationReplayed=True,
        fullNativeMeasurements=measurements,rankerUpdatesReplayed=report['rankerTrainingUpdates'],
        learnedSelectionReplayed=report['rankerTrainingUpdates']>0,accepted=accepted,
        initialVisual=report['initialVisual'],minimumLegalTeacherVisual=minimum_legal,
        savedVisual=stats['visual'],savedGeometryExactlyReplayed=True,
        reportSha256=digest(stage/'report.json'),nativeReplaySha256=digest(replay/'native-replay.jsonl'))
    audits.append(audit)
    print(json.dumps(audit),flush=True)
proof = dict(kind='source-cell-control-and-learned-selector-full-replay-v1',status='pass',stages=audits,
    allTeacherLabelsIndependentlyRemeasured=True,
    totalFullNativeMeasurements=sum(row['fullNativeMeasurements'] for row in audits),
    codeSha256=digest(__file__),newTrainingUpdates=0,nativeCoordinateSearchOrRepairs=0,
    productCandidatePromoted=False,browserVerified=False)
(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps({key:proof[key] for key in ('status','totalFullNativeMeasurements')}),flush=True)
