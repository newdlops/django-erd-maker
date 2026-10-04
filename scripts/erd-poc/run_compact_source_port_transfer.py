"""Run predetermined trained heads with original card-relative endpoints.

Checkpoint order is fixed before Native outcomes. Native performs only source
observation, decoding and strict legal acceptance. No new learning occurs.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np

from compact_source_port_policy import SourcePortPatchActor
from geometry_world_model import digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_anchor_pair_policy import Native, wire
from learned_global_replay import INPUT_FILES


def run(args):
    args.out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    source = json.loads(args.source_binding.read_text())['viewSources'][args.view]
    directory = Path(source['directory'])
    inputs = {name: digest(directory / name) for name in INPUT_FILES}
    assert inputs == source['inputs']
    assert digest(args.candidate) == '4e741de39ca655c7ff042422b9407206427f89e02ecd56b7f4a9f36f6a5cd02e'
    decoder = WalkDecoder(directory)
    stages = [args.parent / (args.view + suffix) for suffix in ('-field1', '-tied1')]
    proposals = []
    bindings = []
    # Only the first and last trained heads of each stage are declared here.
    # No metric, legality or geometry from future proposals chooses the heads.
    for stage in stages:
        report = json.loads((stage / 'report.json').read_text())
        assert report['sourceDirectory'] == str(directory) and report['sourceInputs'] == inputs
        assert digest(stage / 'observations.npz') == report['observationsSha256']
        with np.load(stage / 'observations.npz', allow_pickle=False) as saved:
            nodes, active = saved['nodes'].copy(), saved['active'].copy()
        model = SourcePortPatchActor(nodes, decoder, active, report['maxStep'], report['seed'])
        records = [json.loads(line) for line in (stage / 'actions.jsonl').read_text().splitlines()]
        assert digest(stage / 'actions.jsonl') == report['actionsSha256']
        bindings.append(dict(stage=str(stage), reportSha256=digest(stage / 'report.json'),
                             observationsSha256=report['observationsSha256'], selectedSteps=[records[0]['step'], records[-1]['step']]))
        for row in (records[0], records[-1]):
            assert digest(row['checkpoint']) == row['checkpointSha256']
            with np.load(row['checkpoint'], allow_pickle=False) as saved:
                for key, value in dict(active=active, embedding=model.embedding, w1=model.w1, w2=model.w2,
                                       mean=model.mean, scale=model.scale, **model.buffers).items():
                    assert np.array_equal(value, saved[key])
                model.p = {key: saved[key].copy() for key in model.p}
            proposed = model.forward(nodes)[0]
            proposals.append((stage, row, proposed, nodes.copy(), active.copy()))
    native = Native(args.environment, directory, args.out / 'learned.tsv', args.view == 'overview', False)
    best = None
    attempts = accepted = skipped = 0
    best_visual = native.initial['visual']
    reasons = Counter()
    zero = np.zeros_like(proposals[0][2])
    seen = {hashlib.sha256(wire(zero).encode()).hexdigest()}
    try:
        assert best_visual == source['expectedVisual']
        observed_nodes = np.array([node['features'] for node in native.initial['nodes']])
        for _, _, _, nodes, _ in proposals:
            assert np.array_equal(nodes, observed_nodes)
        baseline = native.request(wire(zero, 'MEASURE'))
        assert baseline['legal'] and baseline['visual'] == best_visual
        assert baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        with (args.out / 'actions.jsonl').open('x') as stream:
            for stage, row, proposed, _, _ in proposals:
                command = wire(proposed)
                sha = hashlib.sha256(command.encode()).hexdigest()
                record = dict(sourceStage=str(stage), trainedStep=row['step'],
                    checkpoint=row['checkpoint'], checkpointSha256=row['checkpointSha256'], wireSha256=sha)
                if sha in seen:
                    skipped += 1
                    stream.write(json.dumps(record | dict(skipped='duplicate')) + '\n')
                    continue
                seen.add(sha)
                result = native.request(command)
                attempts += 1
                reasons[result['reason']] += 1
                stream.write(json.dumps(record | dict(result=result)) + '\n')
                if result['accepted']:
                    accepted += 1
                    best = proposed.copy()
                    best_visual = result['visual']
                    (args.out / 'best-model-source.json').write_text(json.dumps(record, indent=2) + '\n')
            assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out / 'best-action.npy', best)
    stats = json.loads((args.out / 'learned.tsv.stats.json').read_text())
    assert stats['visual'] == best_visual and stats['acceptedActions'] == accepted
    assert stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    report = dict(kind='trained-compact-head-original-port-transfer-v1', view=args.view,
        sourceDirectory=str(directory), sourceBinding=str(args.source_binding), sourceBindingSha256=digest(args.source_binding),
        sourceInputs=inputs, candidate=str(args.candidate), candidateSha256=digest(args.candidate),
        environment=str(args.environment), environmentSha256=digest(args.environment),
        declaredCheckpointSelection='First and last trained head of each previous compact stage; fixed before measurement',
        parentBindings=bindings, proposals=len(proposals), attempts=attempts, duplicateOutputsSkipped=skipped,
        accepted=accepted, initialVisual=source['expectedVisual'], final=stats, reasons=dict(reasons),
        newTrainingUpdates=0, nativeCoordinateSearchOrRepairs=0, endpointOffsetsAlwaysZero=True,
        actionsSha256=digest(args.out / 'actions.jsonl'), wallSeconds=time.monotonic() - started,
        codeSha256={name: digest(Path(__file__).parent / name) for name in (
            'run_compact_source_port_transfer.py', 'compact_source_port_policy.py', 'compact_patch_neural_policy.py',
            'joint_separation_policy.py', 'run_anchor_pair_walk.py', 'run_anchor_pair_policy.py')})
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('view', 'proposals', 'attempts', 'accepted', 'reasons', 'wallSeconds')} |
                     dict(initialVisual=source['expectedVisual'], finalVisual=best_visual)), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--view', choices=['individual', 'overview'], required=True)
    p.add_argument('--out', type=Path, required=True)
    run(p.parse_args())
