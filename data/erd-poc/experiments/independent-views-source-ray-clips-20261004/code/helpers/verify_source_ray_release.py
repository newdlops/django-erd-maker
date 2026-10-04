"""Reconstruct every retained source-ray control and remeasure its full label."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path('scripts/erd-poc').resolve()))
from geometry_world_model import digest
from run_source_ray_reward import source_vocabulary, action_features, HEAD_KEYS, HEAD_SCALES
from source_ray_clip_policy import SourceRayClipPolicy
from joint_reward_training import head_vector
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_anchor_pair_policy import Native, wire

family = Path('.tmp/visualcross-ml-150-750-20261004/source-ray-clip1')
out = family / 'release-validation1'
out.mkdir(exist_ok=False)
proofs = []
for view in ('overview', 'individual'):
    stage = family / (view + '-learning1')
    report = json.loads((stage / 'report.json').read_text())
    assert report['accepted'] == report['attempts'] == report['neuralGainTrainingUpdates'] == 0
    assert report['selectedIndex'] is None and report['betterLegalTeacherControls'] == 0
    source = Path(report['sourceDirectory'])
    for name, sha in report['sourceInputs'].items():
        assert digest(source / name) == sha
    for name, sha in report['codeSha256'].items():
        assert digest(Path('scripts/erd-poc') / name) == sha
    for key in ('environment', 'sourceBinding'):
        assert digest(report[key]) == report[key + 'Sha256']
    for name, key in (('observations.npz', 'observationsSha256'),
            ('initial-geometry-model.npz', 'initialModelSha256'),
            ('teacher-inputs.npz', 'teacherInputSha256'), ('teachers.jsonl', 'teacherTraceSha256')):
        assert digest(stage / name) == report[key]
    binding = json.loads(Path(report['sourceBinding']).read_text())
    assert digest('data/erd-poc/candidates/captain-ml-independent-views.layout.json') == binding['promotedCandidateSha256']
    decoder = WalkDecoder(source)
    with np.load(stage / 'observations.npz', allow_pickle=False) as data:
        nodes, recorded_active = data['nodes'].copy(), data['active'].copy()
    active, support = source_vocabulary(decoder, nodes, view, 2048.)
    assert support == report['support'] and np.array_equal(active, recorded_active)
    model = SourceRayClipPolicy(nodes, decoder, active, 2048., report['seed'])
    reconstructed = out / (view + '-initial.npz')
    model.save(reconstructed, {})
    with np.load(reconstructed, allow_pickle=False) as actual, np.load(stage / 'initial-geometry-model.npz', allow_pickle=False) as expected:
        assert set(actual.files) == set(expected.files)
        for key in actual.files:
            if key != 'metadata':
                assert actual[key].dtype == expected[key].dtype and np.array_equal(actual[key], expected[key]), key
    rows = [json.loads(line) for line in (stage / 'teachers.jsonl').read_text().splitlines()]
    assert len(rows) == report['teacherControls']
    with np.load(stage / 'teacher-inputs.npz', allow_pickle=False) as data:
        features, heads, gains = (data[key].copy() for key in ('features', 'heads', 'gains'))
    native = Native(Path(report['environment']), source, out / (view + '.tsv'), view == 'overview', False)
    try:
        assert np.array_equal(nodes, np.array([row['features'] for row in native.initial['nodes']]))
        assert native.request(wire(model.forward(nodes)[0], 'MEASURE')) == report['sourceControl']
        with (out / (view + '-native-replay.jsonl')).open('x') as log:
            for index, row in enumerate(rows):
                assert row['index'] == index
                model.p = dict(wo=np.zeros((8, 3)), bo=np.zeros(3))
                model.p['wo'][:, 0] = model.embedding[row['target']]
                model.p['bo'][row['axis'] + 1] = row['sign'] * 3.
                assert np.array_equal(head_vector(model, HEAD_KEYS, HEAD_SCALES), heads[index])
                action, info = model.forward(nodes)
                assert info == row['info']
                assert np.array_equal(action_features(nodes, model, action, info), features[index])
                assert np.array_equal(action, model.forward(np.full_like(nodes, 99))[0])
                command = wire(action, 'MEASURE')
                assert hashlib.sha256(command.encode()).hexdigest() == row['wireSha256']
                measured = native.request(command)
                assert measured == row['result']
                gain = report['sourceVisual'] - measured['visual'] if measured['legal'] else -1000.
                assert gains[index] == gain
                log.write(json.dumps(dict(index=index, result=measured)) + '\n')
    finally:
        native.close()
    verify_saved(source, stage, decoder, None)
    proofs.append(dict(view=view, teacherControls=len(rows), fullNativeMeasurements=1 + len(rows),
        initialCheckpointArraysMatched=True, allTeacherLabelsIndependentlyRemeasured=True,
        neuralControlHeadsAndFeaturesMatched=True, futureObservationFeaturesIgnored=True,
        savedSourceGeometryMatched=True, sourceVisual=report['sourceVisual']))
    print(json.dumps(proofs[-1]), flush=True)
proof = dict(status='pass', views=proofs, fullNativeMeasurements=sum(row['fullNativeMeasurements'] for row in proofs),
    accepted=0, allTeacherLabelsIndependentlyRemeasured=True, newTrainingUpdates=0,
    coordinateSearchOrRepairs=0, browserVerified=False, verifierSha256=digest(__file__))
(out / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
print(json.dumps(proof), flush=True)
