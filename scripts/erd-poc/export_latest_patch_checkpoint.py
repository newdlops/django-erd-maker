#!/usr/bin/env python3
"""Export one recorded trained output for review, including neutral/regressed outputs.

Inference uses the frozen checkpoint once. Native only remeasures the output;
no training, action selection, coordinate repair or strict-best fallback occurs.
Run inside run_memory_bounded.py.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

import numpy as np

from compact_feasible_source_port_policy import FeasibleSourcePortPatchActor
from compact_source_port_policy import SourcePortPatchActor
from run_anchor_pair_policy import Native, wire
from run_anchor_pair_walk import WalkDecoder
from run_compact_patch_reward import patch_support
from single_owner_cached_observer_v2 import CachedObserver


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(65536), b''):
            value.update(block)
    return value.hexdigest()


def write_json(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--source-layout', type=Path, required=True)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get('OMP_NUM_THREADS') == '1', 'run under the resource guard'
    assert args.out.resolve().is_relative_to(Path('.tmp').resolve())
    report = json.loads((args.stage / 'report.json').read_text())
    source = Path(report['sourceDirectory'])
    for name, sha in report['sourceInputs'].items():
        assert digest(source / name) == sha
    for name, sha in report['codeSha256'].items():
        assert digest(Path(__file__).parent / name) == sha
    for key in ('environment', 'sourceBinding'):
        assert digest(report[key]) == report[key + 'Sha256']
    assert digest(args.stage / 'actions.jsonl') == report['actionsSha256']
    assert digest(args.stage / 'observations.npz') == report['observationsSha256']
    actions = [json.loads(line) for line in (args.stage / 'actions.jsonl').read_text().splitlines()]
    latest = actions[-1]
    assert latest['step'] == report['updatesExecuted']
    checkpoint = Path(latest['checkpoint'])
    assert digest(checkpoint) == latest['checkpointSha256']
    decoder = WalkDecoder(source)
    with np.load(args.stage / 'observations.npz', allow_pickle=False) as saved:
        nodes, active = saved['nodes'].copy(), saved['active'].copy()
    expected, support = patch_support(decoder, nodes, report['patchSize'], report['rootRank'])
    assert np.array_equal(active, expected) and support == report['support']
    kind = report['kind']
    classes = {
        'compact-source-spacing-original-port-native-reward-learning-v1': SourcePortPatchActor,
        'compact-source-spacing-feasible-original-port-native-reward-learning-v2': FeasibleSourcePortPatchActor,
    }
    model = classes[kind](nodes, decoder, active, report['maxStep'], report['seed'])
    with np.load(checkpoint, allow_pickle=False) as saved:
        for key, value in dict(active=active, embedding=model.embedding, w1=model.w1, w2=model.w2,
                               mean=model.mean, scale=model.scale, **model.buffers).items():
            assert np.array_equal(value, saved[key]), key
        metadata = json.loads(str(saved['metadata']))
        assert metadata['trainedUpdates'] == latest['step']
        for key in model.p:
            assert saved[key].shape == model.p[key].shape
            model.p[key] = saved[key].copy()
    proposed = model.forward(nodes)[0]
    assert hashlib.sha256(wire(proposed).encode()).hexdigest() == latest['wireSha256']
    args.out.mkdir(parents=True, exist_ok=False)
    native = Native(Path(report['environment']), source, args.out / 'unused.tsv', report['view'] == 'overview', False)
    try:
        assert np.array_equal(nodes, np.array([row['features'] for row in native.initial['nodes']]))
        assert native.initial['visual'] == report['initialVisual']
        result = native.request(wire(proposed, 'MEASURE'))
        fields = ('legal', 'reason', 'visual', 'individualVisual', 'spacing', 'hard', 'individualHard')
        assert all(result[key] == latest['result'][key] for key in fields)
        assert result['legal'] and result['spacing'] == result['hard'] == result['individualHard'] == 0
    finally:
        native.close()
    observer = CachedObserver(source, decoder)
    physical, full, physical_lines, full_lines, _ = observer.decode(proposed)
    proposal = args.out / 'learned.tsv'

    def write_rows(path, ids, values, routes=False):
        assert len(ids) == len(values)
        with path.open('x') as stream:
            for key, value in zip(ids, values):
                if routes:
                    text = ' '.join(','.join(format(float(v), '.17g') for v in point) for point in value)
                else:
                    text = '\t'.join(format(float(v), '.17g') for v in value)
                stream.write(key + '\t' + text + '\n')

    ids = lambda name: [line.split('\t')[0] for line in (source / name).read_text().splitlines()]
    write_rows(proposal, ids('nodes.tsv'), physical)
    write_rows(Path(str(proposal) + '.individual'), observer.full_ids, full)
    write_rows(Path(str(proposal) + '.routes.tsv'), ids('groups.tsv'), physical_lines, True)
    write_rows(Path(str(proposal) + '.individual.routes.tsv'), observer.edge_ids, full_lines, True)
    np.save(args.out / 'action.npy', proposed)
    stats = dict(initialVisual=report['initialVisual'], visual=result['visual'],
        initialIndividualVisual=report['initialIndividualVisual'], individualVisual=result['individualVisual'],
        initialHardConditions=0, hardConditions=0, initialIndividualHardConditions=0, individualHardConditions=0,
        overlap=0, spacing=0, heuristicSearchCalls=0, proposalAuthority='external-policy',
        latestCheckpointPreview=True, strictBestFallback=False, nativeMeasureCalls=1)
    policy = dict(modelKind=kind, checkpoint=str(checkpoint), checkpointSha256=digest(checkpoint),
        trainedUpdates=latest['step'], untrainedControl=False, newTrainingUpdates=0, final=stats)
    write_json(Path(str(proposal) + '.stats.json'), stats)
    write_json(Path(str(proposal) + '.policy.json'), policy)
    exported = dict(view=report['view'], sourceFile=str(args.source_layout), sourceSha256=digest(args.source_layout),
        payloadFile=str(args.payload), payloadSha256=digest(args.payload), stage=str(args.stage),
        stageReportSha256=digest(args.stage / 'report.json'), checkpoint=str(checkpoint),
        checkpointSha256=digest(checkpoint), trainedUpdates=latest['step'], modelKind=kind,
        recordedTryWireSha256=latest['wireSha256'], fullNativeMeasurement=result,
        immutableCheckpointAndDecoderArraysMatched=True, sourceObservationsMatched=True,
        latestNetworkForwardExported=True, strictBestFallback=False, newTrainingUpdates=0,
        coordinateSearchOrRepairs=0, exporterSha256=digest(__file__))
    write_json(args.out / 'export.json', exported)
    print(json.dumps(dict(view=report['view'], visual=result['visual'], individualVisual=result['individualVisual'],
        checkpointStep=latest['step'], latestNetworkForwardExported=True, newTrainingUpdates=0)))


if __name__ == '__main__':
    main()
