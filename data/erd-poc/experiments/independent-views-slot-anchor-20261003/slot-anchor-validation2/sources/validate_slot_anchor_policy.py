"""Validate joint slot/endpoint outputs and exact-reward replay on both sources."""
import hashlib
import json
import shutil
import subprocess
import sys
import traceback
from pathlib import Path
sys.path.insert(0, 'scripts/erd-poc')
import numpy as np
from joint_slot_anchor_policy import SlotAnchorRayPolicy
from joint_neural_ports import JointPortPolicy, PerimeterRoutes
from joint_reward_training import RewardHeadTrainer, verify_trace
from run_joint_neural_layout import action_text, verify_geometry
from learned_global_replay import INPUT_FILES, pairs

base = Path(__file__).parent
out = base / 'slot-anchor-validation2'
out.mkdir(exist_ok=False)
binary = base / 'joint-batch-environment-v7'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
names = ['joint_slot_anchor_policy.py', 'joint_anchor_ray_policy.py', 'joint_slot_policy.py',
         'joint_separation_policy.py', 'joint_neural_ports.py', 'joint_layout_proxy.py',
         'joint_grouped_routes.py', 'joint_reward_training.py', 'run_joint_neural_layout.py']
sources = out / 'sources'
sources.mkdir()
hashes = {}
for name in names:
    path = Path('scripts/erd-poc') / name
    shutil.copyfile(path, sources / name)
    hashes[name] = digest(path)
shutil.copyfile(Path(__file__), sources / Path(__file__).name)
report = {'nativeBinarySha256': digest(binary), 'implementationHashes': hashes, 'views': {},
          'nativeHardValidityGuaranteedByDecoder': False, 'sortingGradientUsed': False,
          'fixturesOnly': True, 'sourceGeometryPreserved': True}
try:
    legacy = []
    for view in ['individual', 'overview']:
        previous = base / (view + '-anchor-ray1')
        features = np.load(previous / 'joint-input-features.npy')
        rows = [json.loads(line) for line in (previous / 'joint-batches.jsonl').open()]
        for row in [rows[0], rows[-1]]:
            assert digest(Path(row['checkpoint'])) == row['checkpointSha256']
            model = JointPortPolicy.load(row['checkpoint'])
            assert hashlib.sha256(action_text(model.forward(features)[0]).encode()).hexdigest() == row['actionSha256']
            legacy.append({'checkpointSha256': row['checkpointSha256'], 'actionSha256': row['actionSha256']})
    report['legacyAnchorActionReplays'] = legacy
    for view, expected in [('individual', 1964), ('overview', 289)]:
        directory = out / view
        directory.mkdir()
        for name in INPUT_FILES:
            shutil.copyfile(base / 'separation-dag-validation2' / view / name, directory / name)
        previous = base / (view + '-anchor-ray1')
        features = np.load(previous / 'joint-input-features.npy')
        positions = np.array(list(pairs(directory / 'positions.tsv').values()))
        sizes = np.array(list(pairs(directory / 'nodes.tsv').values()))
        provider = PerimeterRoutes(directory)
        model = SlotAnchorRayPolicy(features, positions, sizes, 100000., 13471, provider, .3)
        action, _ = model.forward(features)
        np.testing.assert_array_equal(action, 0.)
        provider.set_actions(action[len(positions):],quantized=True,positions=positions)
        full = positions[provider.owner] + provider.offsets
        zero_ports=full[provider.full_edges] + provider.current_offsets
        zero_error=float(np.max(abs(zero_ports-provider.original_ports)))
        np.testing.assert_allclose(zero_ports, provider.original_ports, rtol=0, atol=1e-8)
        output = directory / 'unchanged.tsv'
        p = subprocess.Popen(list(map(str, [binary, '--directory', directory, '--out', output,
            '--overview-only', int(view == 'overview'), '--neural-perimeter-ports', 1])),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            initial = json.loads(p.stdout.readline())
            assert initial['ready'] and initial['visual'] == expected
            def request(command):
                p.stdin.write(command + '\n'); p.stdin.flush()
                line = p.stdout.readline()
                assert line, (p.poll(), p.stderr.read())
                return json.loads(line)
            def measure(action):
                result = request('MEASURE ' + action_text(action)[4:])
                assert result['measureOnly'] and not result['accepted']
                return result
            zero = measure(action)
            assert zero['legal'] and zero['visual'] == expected
            comparisons = []
            maximum_moved = 0
            # These are untrained, noncommitting output fixtures, not candidates.
            for number, std in enumerate([.0003, .001, .003, .01, .03, .1]):
                model.p['wo'] = np.random.default_rng(13481 + number).normal(0, std, model.p['wo'].shape)
                action, _ = model.forward(features)
                nodes, phases = action[:len(positions)], action[len(positions):]
                moved = int(np.any(abs(nodes) > 1e-8, axis=1).sum())
                maximum_moved = max(maximum_moved, moved)
                for begin, end in zip(model.group_bounds[:-1], model.group_bounds[1:]):
                    members = model.slot_order[begin:end]
                    before = np.array(sorted(map(tuple, positions[members])))
                    after = np.array(sorted(map(tuple, (positions + nodes)[members])))
                    np.testing.assert_allclose(after, before, rtol=0, atol=1e-8)
                for group in np.flatnonzero(model.endpoint_counts>1):
                    values=phases.ravel()[model.endpoint_groups==group]
                    np.testing.assert_array_equal(values,values[0])
                checkpoint = directory / f'fixture-{number}.npz'
                model.save(checkpoint, {'validationOnly': True})
                np.testing.assert_array_equal(SlotAnchorRayPolicy.load(checkpoint).forward(features)[0], action)
                np.testing.assert_array_equal(JointPortPolicy.load(checkpoint).forward(features)[0], action)
                comparisons.append({'headStd': std, 'movedCards': moved, 'result': measure(action),
                    'checkpointSha256': digest(checkpoint)})
            assert maximum_moved > 0
            # Verify real native reward probes and Adam replay without accepting outputs.
            model.p['wo'].fill(0.)
            initial_policy = directory / 'reward-initial.npz'
            model.save(initial_policy, {'validationOnly': True})
            with (directory / 'reward-probes.jsonl').open('w') as trace:
                trainer = RewardHeadTrainer(model, features, request, action_text, trace,
                    13511, .01, 2, .001)
                for _ in range(3):
                    trainer.step()
            records = [json.loads(line) for line in (directory / 'reward-probes.jsonl').open()]
            replay = SlotAnchorRayPolicy.load(initial_policy)
            count = verify_trace(replay, features, records, action_text)
            assert count == 12
            np.testing.assert_array_equal(replay.p['wo'], model.p['wo'])
            assert request('SAVE')['saved']
            verify_geometry(directory, output, None, neural_perimeter_ports=True)
            p.stdin.write('QUIT\n'); p.stdin.flush(); assert p.wait(timeout=5) == 0
        finally:
            if p.poll() is None:
                p.kill(); p.wait()
        source = base / (view + '-bounded-trained2') / (
            'candidate.individual.layout.json' if view == 'individual' else 'candidate.layout.json')
        report['views'][view] = {'sourceSha256': digest(source), 'sourceVisual': expected,
            'zeroOutputOriginalEndpointsPreserved': int(provider.full_edges.size),
            'zeroEndpointMaximumAbsoluteError':zero_error,'endpointGeometryTolerance':1e-8,
            'rectangleMultisetFixtures': len(comparisons), 'maximumMovedCardsInUntrainedFixtures': maximum_moved,
            'frozenRoundtripsExact': True, 'noncommittingFixtures': comparisons,
            'nativeRewardProbesReplayed': count, 'adamUpdatesReplayed': 3,
            'sharedEndpointGroups': int(np.count_nonzero(model.endpoint_counts > 1)),
            'trainableHeadParameters': model.p['wo'].size, 'sourceUnchanged': True}
        print(json.dumps({view: report['views'][view]}), flush=True)
    report['allChecksPassed'] = True
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
except Exception:
    (out / 'failure.json').write_text(json.dumps({'partial': report, 'traceback': traceback.format_exc()}, indent=2) + '\n')
    raise
