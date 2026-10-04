"""Evaluate fixed trained heads with whole-scene NN positions and common anchors."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import time
import numpy as np
from compact_global_anchor_policy import GlobalAnchorPolicy
from geometry_world_model import digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_anchor_pair_policy import Native, wire
from learned_global_replay import INPUT_FILES


def run(args):
    args.out.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()
    spec = json.loads(args.source_binding.read_text())['viewSources']['individual']
    directory = Path(spec['directory'])
    inputs = {name: digest(directory / name) for name in INPUT_FILES}
    assert inputs == spec['inputs']
    decoder = WalkDecoder(directory)
    model = None
    proposals = []
    bindings = []
    for suffix in ('-field1', '-tied1'):
        stage = args.parent / ('individual' + suffix)
        report = json.loads((stage/'report.json').read_text())
        assert report['sourceInputs'] == inputs and report['sourceDirectory'] == str(directory)
        assert digest(stage/'observations.npz') == report['observationsSha256']
        with np.load(stage/'observations.npz', allow_pickle=False) as saved:
            nodes, training_active = saved['nodes'].copy(), saved['active'].copy()
        if model is None:
            model = GlobalAnchorPolicy(nodes, decoder, training_active, report['maxStep'], report['seed'])
            model.save(args.out/'initial-model.npz', dict(kind='global-anchor-initial', trainedUpdates=0))
            np.savez_compressed(args.out/'observations.npz', nodes=nodes, training_active=training_active)
            initial_control = model.forward(nodes)[0]
        else:
            assert report['seed'] == initial_seed and np.array_equal(nodes, initial_nodes)
        initial_seed = report['seed']
        initial_nodes = nodes.copy()
        rows = [json.loads(line) for line in (stage/'actions.jsonl').read_text().splitlines()]
        assert digest(stage/'actions.jsonl') == report['actionsSha256']
        bindings.append(dict(stage=str(stage), reportSha256=digest(stage/'report.json'), selectedSteps=[rows[0]['step'], rows[-1]['step']]))
        for row in (rows[0], rows[-1]):
            assert digest(row['checkpoint']) == row['checkpointSha256']
            with np.load(row['checkpoint'], allow_pickle=False) as saved:
                for key, value in dict(w1=model.w1, w2=model.w2, mean=model.mean, scale=model.scale).items():
                    assert np.array_equal(value, saved[key])
                assert np.array_equal(model.anchor_embedding[training_active], saved['embedding'])
                model.p = {key: saved[key].copy() for key in model.p}
            proposals.append((str(stage), row, model.forward(nodes)[0]))
    native = Native(args.environment, directory, args.out/'learned.tsv', False, False)
    best = None
    best_visual = native.initial['visual']
    accepted = 0
    reasons = Counter()
    try:
        assert best_visual == spec['expectedVisual'] and np.array_equal(initial_nodes, np.array([node['features'] for node in native.initial['nodes']]))
        zero_head = native.request(wire(initial_control, 'MEASURE'))
        with (args.out/'actions.jsonl').open('x') as stream:
            for stage, row, proposed in proposals:
                result = native.request(wire(proposed))
                reasons[result['reason']] += 1
                record = dict(sourceStage=stage, trainedStep=row['step'], checkpoint=row['checkpoint'],
                    checkpointSha256=row['checkpointSha256'], wireSha256=hashlib.sha256(wire(proposed).encode()).hexdigest(), result=result)
                stream.write(json.dumps(record)+'\n')
                if result['accepted']:
                    accepted += 1
                    best = proposed.copy()
                    best_visual = result['visual']
                    (args.out/'best-model-source.json').write_text(json.dumps(record, indent=2)+'\n')
            assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(directory, args.out, decoder, best)
    if best is not None:
        np.save(args.out/'best-action.npy', best)
    stats = json.loads((args.out/'learned.tsv.stats.json').read_text())
    assert stats['visual'] == best_visual and stats['hardConditions'] == stats['individualHardConditions'] == stats['spacing'] == stats['overlap'] == 0
    report = dict(kind='trained-head-whole-scene-shared-anchor-transfer-v1', view='individual',
        sourceDirectory=str(directory), sourceBinding=str(args.source_binding), sourceBindingSha256=digest(args.source_binding),
        sourceInputs=inputs, environment=str(args.environment), environmentSha256=digest(args.environment), parentBindings=bindings,
        seed=initial_seed, originalTrainingActive=training_active.tolist(), wholeSceneOwners=len(decoder.positions),
        maxStep=2048., trainableParameters=18, newTrainingUpdates=0, attempts=len(proposals), accepted=accepted,
        initialVisual=spec['expectedVisual'], zeroHeadControl=zero_head, final=stats, reasons=dict(reasons),
        sourceFrameAndOriginalCardSizesRetained=True, nativeCoordinateSearchOrRepairs=0,
        actionsSha256=digest(args.out/'actions.jsonl'), initialModelSha256=digest(args.out/'initial-model.npz'),
        observationsSha256=digest(args.out/'observations.npz'), wallSeconds=time.monotonic()-start,
        codeSha256={name: digest(Path(__file__).parent/name) for name in ('run_global_anchor_transfer.py',
            'compact_global_anchor_policy.py', 'compact_shared_anchor_policy.py', 'compact_source_port_policy.py',
            'compact_patch_neural_policy.py', 'joint_separation_policy.py', 'joint_neural_ports.py',
            'run_anchor_pair_walk.py', 'run_anchor_pair_policy.py')})
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(initialVisual=spec['expectedVisual'], zeroHeadControl=zero_head,
        owners=len(decoder.positions), attempts=len(proposals), accepted=accepted, finalVisual=best_visual,
        reasons=dict(reasons), wallSeconds=report['wallSeconds'])), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--source-binding', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    run(p.parse_args())
