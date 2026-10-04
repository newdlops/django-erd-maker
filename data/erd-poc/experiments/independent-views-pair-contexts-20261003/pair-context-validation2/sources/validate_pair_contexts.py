"""Validate context indexing, graph parity, native gates and frozen replay."""
import hashlib
import json
import shutil
import subprocess
import sys
import traceback
from pathlib import Path
sys.path.insert(0, 'scripts/erd-poc')
import numpy as np
from learned_pair_contexts import self_test
from learned_branch_map import prepare_branch_map
from learned_bounded_replay import BoundedMovingRayReplay
from learned_global_replay import INPUT_FILES

base = Path(__file__).parent
out = base / 'pair-context-validation2'
out.mkdir(exist_ok=False)
binary = base / 'component-environment-v27-o0'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sources = out / 'sources'
sources.mkdir()
names = ['ml_component_environment.cpp', 'learned_pair_contexts.h', 'learned_pair_contexts.py',
         'constrained_dual_node_geometry.h', 'constrained_scene.h', 'constrained_boundary_sweep.h',
         'learned_branch_map.py', 'learned_global_replay.py', 'learned_bounded_replay.py',
         'learn_card_policy.py', 'run_individual_policy_experiment.py', 'run_learned_leaf_layout.py',
         'run_memory_bounded.py']
hashes = {}
for name in names:
    path = Path('scripts/erd-poc') / name
    shutil.copyfile(path, sources / name)
    hashes[name] = digest(path)
shutil.copyfile(Path(__file__), sources / Path(__file__).name)
report = {'nativeBinarySha256': digest(binary), 'implementationHashes': hashes,
          'compilerFlags': ['-std=c++17', '-O0', '-ffp-contract=off'], 'views': {}}


def run(name, command):
    with (out / (name + '.stdout')).open('w') as stdout, (out / (name + '.stderr')).open('w') as stderr:
        result = subprocess.run(list(map(str, command)), stdout=stdout, stderr=stderr)
    assert result.returncode == 0, (name, result.returncode)
    return (out / (name + '.stdout')).read_text()


try:
    prior_path = base / 'pair-context-validation1/failure.json'
    prior_checks = json.loads(prior_path.read_text())['partial']
    assert prior_checks['implementationHashes'] == hashes
    assert prior_checks['nativeBinarySha256'] == digest(binary)
    for field in ['pythonFixtures', 'nativeTests', 'legacyReplay']:
        report[field] = prior_checks[field]
    report['unchangedCompletedChecksReusedFrom'] = str(prior_path)
    report['unchangedCompletedChecksReportSha256'] = digest(prior_path)
    for view, expected_visual in [('individual', 1964), ('overview', 289)]:
        directory = out / view
        directory.mkdir()
        previous = base / 'separation-dag-validation2' / view
        for name in INPUT_FILES:
            shutil.copyfile(previous / name, directory / name)
        source = base / (view + '-bounded-trained2') / (
            'candidate.individual.layout.json' if view == 'individual' else 'candidate.layout.json')
        branch = prepare_branch_map(directory, digest(source), 'pair-cut')
        replay = BoundedMovingRayReplay(directory, 1.5e9, True)
        prior = json.loads((base / 'pair-separator-analysis1' / (view + '-contexts.json')).read_text())
        assert prior['sourceSha256'] == digest(source)
        assert {frozenset(row) for row in replay.branches.values()} == {
            frozenset(row['nodeIds']) for row in prior['contexts']}
        command = [binary, '--directory', directory, '--decoder', 'bounded-moving-ray',
                   '--branches', '1', '--branch-mode', 'pair-cut', '--out', directory / 'zero.tsv']
        if view == 'overview':
            command += ['--overview-only', '1']
        p = subprocess.Popen(list(map(str, command)), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
        try:
            ready = json.loads(p.stdout.readline())
            assert ready['visual'] == expected_visual and ready['eligible'] == len(replay.context_keys)
            def request(line):
                p.stdin.write(line + '\n'); p.stdin.flush()
                reply = p.stdout.readline()
                assert reply, (p.poll(), p.stderr.read())
                return json.loads(reply)
            for n in range(len(replay.context_keys)):
                np.testing.assert_allclose(request(f'BOX {n}'), replay.box(n), rtol=0, atol=1e-8)
            observation = request('OBS 64')
            assert observation['nodes']
            assert all(0 <= row['id'] < len(replay.context_keys) and len(row['features']) == 64
                       and row['actionScale'] == 1 for row in observation['nodes'])
            assert request('SAVE')['saved']
            replay.verify(directory / 'zero.tsv')
            p.stdin.write('QUIT\n'); p.stdin.flush(); assert p.wait(timeout=5) == 0
        finally:
            if p.poll() is None:
                p.kill(); p.wait()
        # Native topology verification must reject a validly shaped wrong map.
        corrupt = out / (view + '-corrupt')
        corrupt.mkdir()
        for name in INPUT_FILES:
            shutil.copyfile(directory / name, corrupt / name)
        rows = (directory / 'branches.tsv').read_text().splitlines()
        rows[0], rows[1] = ('context:0\t' + rows[1].split('\t', 1)[1],
                            'context:1\t' + rows[0].split('\t', 1)[1])
        (corrupt / 'branches.tsv').write_text('\n'.join(rows) + '\n')
        rejected = subprocess.run(list(map(str, [binary, '--directory', corrupt, '--decoder',
            'bounded-moving-ray', '--branches', '1', '--branch-mode', 'pair-cut', '--out', corrupt / 'unused'])),
            input='QUIT\n', capture_output=True, text=True)
        assert rejected.returncode != 0 and 'branch map differs from source graph' in rejected.stderr
        (corrupt / 'rejection.stderr').write_text(rejected.stderr)
        report['views'][view] = {'sourceSha256': digest(source), 'visual': expected_visual,
            'branchMap': branch, 'boxesCompared': len(replay.context_keys),
            'observedContexts': len(observation['nodes']), 'sourceGeometryPreserved': True,
            'priorAnalysisMembershipMatches': True, 'wrongContextMapRejected': True}
        print(json.dumps({view: report['views'][view]}), flush=True)
    report['allChecksPassed'] = True
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
except Exception:
    (out / 'failure.json').write_text(json.dumps({'partial': report, 'traceback': traceback.format_exc()}, indent=2) + '\n')
    raise
