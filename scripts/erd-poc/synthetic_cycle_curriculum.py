#!/usr/bin/env python3
"""Model-generated coordinated outcomes on independently seeded small graphs.

Fixture geometry is created before any scoring. The frozen critic selects
directed cycles and amplitudes; native geometry only measures its supplied
actions and saves the best legal model output. No source repair or search.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import time
import numpy as np
from fractional_cycle_model import load, action_features
from joint_neural_ports import PerimeterRoutes
from learned_global_replay import INPUT_FILES, port, rounded
from run_anchor_pair_policy import Native, wire, digest
from run_anchor_pair_walk import WalkDecoder, verify_saved
from run_directed_cycle_policy import coordinated_cycles
from run_fraction_conditioned_policy import rank, sample_order, FRACTIONS, code_hashes as policy_hashes
from run_fractional_cycle_transfer import fractional_action
from train_anchor_pair_outcomes import outcome_target


def code_hashes():
    return policy_hashes() | {'synthetic_cycle_curriculum.py': digest(__file__)}


def fixture(directory, seed):
    directory.mkdir(parents=True, exist_ok=False)
    rng = np.random.default_rng(seed); count = 28+seed%9
    width, height = 160+20*(seed%4), 100+20*(seed%3)
    slots = rng.permutation(count); physical_ids = [f'c{i:03}' for i in range(count)]
    positions = []; sizes = []; members = []; full_positions = []; full_ids = []
    leaf = [i for i in range(count) if i%8 == 7]; ordinary = [i for i in range(count) if i not in leaf]
    for i, slot in enumerate(slots):
        p = np.array([1400.+1400*(int(slot)%6), 900.+1000*(int(slot)//6)])
        p += np.array([rounded(v) for v in rng.uniform(-90, 90, 2)])
        number = (2 if seed%2 else 4) if i in leaf else 1
        columns = 2 if number>1 else 1; rows = (number+columns-1)//columns
        size = np.array([columns*width+(columns-1)*56+(48 if number>1 else 0),
            rows*height+(rows-1)*42+(48 if number>1 else 0)])
        positions.append(p); sizes.append(size); owner_members = []
        for k in range(number):
            key = f'n{len(full_ids):03}'; full_ids.append(key); owner_members.append(key)
            full_positions.append(p-size/2+(24 if number>1 else 0)+np.array([width/2+(k%columns)*(width+56),
                height/2+(k//columns)*(height+42)]))
        members.append(owner_members)
    edges = set(); order = rng.permutation(ordinary)
    for a, b in zip(order, np.roll(order, -1)):
        edges.add(tuple(sorted((int(a), int(b)))))
    while len(edges) < int(len(ordinary)*2.1):
        edges.add(tuple(sorted(map(int, rng.choice(ordinary, 2, replace=False)))))
    if seed%3 == 0:
        edges.update(tuple(sorted((ordinary[0], i))) for i in ordinary[1::2])
    for i in leaf:
        parent = ordinary[0] if seed%2 else int(rng.choice(ordinary))
        edges.add(tuple(sorted((i, parent))))
    edges = sorted(edges); full_index = {key: i for i, key in enumerate(full_ids)}
    full_edges = []; full_routes = []; groups = []
    for a, b in edges:
        group = []
        for source in members[a]:
            for target in members[b]:
                edge = f'e{len(full_edges):04}'; group.append(edge); full_edges.append((edge, source, target))
                p, q = full_positions[full_index[source]], full_positions[full_index[target]]
                full_routes.append([port(p, [width, height], q), port(q, [width, height], p)])
        groups.append(group)
    def rows(name, values):
        with (directory/name).open('x') as stream:
            for row in values:
                stream.write('\t'.join(map(str, row))+'\n')
    def route_rows(keys, routes):
        return [(key, ' '.join(','.join(f'{float(v):.2f}' for v in point) for point in points))
            for key, points in zip(keys, routes)]
    physical_edges = [(f'g{i:04}', physical_ids[a], physical_ids[b]) for i, (a, b) in enumerate(edges)]
    rows('nodes.tsv', [(key, *size) for key, size in zip(physical_ids, sizes)])
    rows('positions.tsv', [(key, *p) for key, p in zip(physical_ids, positions)])
    rows('edges.tsv', physical_edges)
    rows('individual.nodes.tsv', [(key, width, height) for key in full_ids])
    rows('individual.positions.tsv', [(key, *p) for key, p in zip(full_ids, full_positions)])
    rows('individual.edges.tsv', full_edges)
    rows('individual.routes.tsv', route_rows([row[0] for row in full_edges], full_routes))
    rows('components.tsv', [(key, *ids) for key, ids in zip(physical_ids, members)])
    rows('groups.tsv', [(row[0], *ids) for row, ids in zip(physical_edges, groups)])
    # This is the existing fixed product projection, not an optimizer.
    provider = PerimeterRoutes(directory)
    projected, _, endpoints = provider.forward(np.array(positions), np.array(sizes))
    assert np.array_equal(endpoints, np.array(edges))
    projected = np.copysign(np.floor(abs(projected)*100+.5), projected)/100
    rows('routes.tsv', route_rows([row[0] for row in physical_edges], projected))
    low = (np.array(positions)-np.array(sizes)/2).min(0)
    high = (np.array(positions)+np.array(sizes)/2).max(0)
    report = {'seed': seed, 'physicalNodes': count, 'fullNodes': len(full_ids), 'physicalEdges': len(edges),
        'fullEdges': len(full_edges), 'leafCards': len(leaf), 'frameArea': float(np.prod(high-low)),
        'randomTopologyAndFixedGridBeforeScores': True, 'coordinatesOptimized': False,
        'sourceInputs': {name: digest(directory/name) for name in INPUT_FILES}}
    assert report['frameArea'] <= 1.5e9
    (directory/'fixture.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def flatten_fixture(source, directory):
    """Represent every full card and relationship once, without moving them."""
    directory.mkdir(parents=True, exist_ok=False)
    for prefix in ['', 'individual.']:
        for name in ['nodes.tsv', 'positions.tsv', 'edges.tsv', 'routes.tsv']:
            shutil.copyfile(source/('individual.'+name), directory/(prefix+name))
    nodes = [line.split('\t')[0] for line in (directory/'nodes.tsv').read_text().splitlines()]
    edges = [line.split('\t')[0] for line in (directory/'edges.tsv').read_text().splitlines()]
    for name, ids in [('components.tsv', nodes), ('groups.tsv', edges)]:
        (directory/name).write_text(''.join(key+'\t'+key+'\n' for key in ids))
    report = {'groupedFixture': str(source), 'groupedFixtureReportSha256': digest(source/'fixture.json'),
        'allFullCardsAndRelationshipsRepresentedIndividually': True,
        'coordinatesAndFullRoutesCopiedWithoutChanges': True,
        'sourceInputs': {name: digest(directory/name) for name in INPUT_FILES}}
    (directory/'fixture.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def collect_stage(root, graph_id, source, view, checkpoint, environment, budget=64, stage_name=None):
    stage = root/f'graph-{graph_id:02}'/(stage_name or view); stage.mkdir(parents=True, exist_ok=False)
    model, metadata = load(checkpoint); decoder = WalkDecoder(source)
    native = Native(environment, source, stage/'learned.tsv', view=='overview', True)
    best = None; attempts = improving = accepted = 0; reasons = Counter(); started = time.monotonic()
    best_visual = native.initial['visual']; features = []; targets = []; cycles_seen = []; fractions_seen = []; gain_seen = []
    try:
        baseline = native.request(wire(decoder.decode(np.arange(len(decoder.positions), dtype=np.int32)), 'MEASURE'))
        assert baseline['legal'] and baseline['hard'] == baseline['individualHard'] == baseline['spacing'] == 0
        assert baseline['visual'] == native.initial['visual']
        nodes = np.array([row['features'] for row in native.initial['nodes']])
        vocabulary_seed = 86069+graph_id; sampling_seed = 86171+graph_id
        vocabulary, support = coordinated_cycles(decoder, nodes, np.empty((0, 3), dtype=np.int32), vocabulary_seed, 128)
        scores, hashes, error = rank(model, decoder, nodes, vocabulary, view=='overview')
        order, noise, policy = sample_order(scores, sampling_seed, 4.)
        np.savez_compressed(stage/'observations.npz', node_features=nodes, cycles=vocabulary, scores=scores, gumbel=noise)
        with (stage/'actions.jsonl').open('x') as stream:
            for index in order[:budget]:
                ci, fi = map(int, np.unravel_index(index, scores.shape)); cycle = vocabulary[ci]; fraction = float(FRACTIONS[fi])
                action = fractional_action(decoder, cycle, fraction); command = wire(action); result = native.request(command)
                feature, _ = action_features(nodes, decoder, cycle[None], np.array([fraction]), view=='overview')
                gain = float(native.initial['visual']-result['visual']) if result['legal'] else np.nan
                features.append(feature[0]); targets.append(outcome_target(result, native.initial['visual']))
                cycles_seen.append(cycle.copy()); fractions_seen.append(fraction); gain_seen.append(gain)
                stream.write(json.dumps({'rank': attempts, 'cycleIndex': ci, 'fractionIndex': fi, 'cycle': cycle.tolist(),
                    'fraction': fraction, 'score': float(scores[ci, fi]), 'gumbel': float(noise[ci, fi]),
                    'wireSha256': hashlib.sha256(command.encode()).hexdigest(), 'result': result})+'\n')
                attempts += 1; reasons[result['reason']] += 1; improving += int(gain>0)
                if result['accepted']:
                    accepted += 1; best_visual = result['visual']; best = action.copy()
        assert native.request('SAVE')['saved']
    finally:
        native.close()
    verify_saved(source, stage, decoder, best)
    np.savez_compressed(stage/'labels.npz', x=np.array(features), y=np.array(targets), cycles=np.array(cycles_seen),
        fractions=np.array(fractions_seen), actual_gain=np.array(gain_seen))
    stats = json.loads((stage/'learned.tsv.stats.json').read_text())
    assert stats['visual'] == best_visual and stats['policyActionsEvaluated'] == attempts
    assert stats['acceptedActions'] == accepted and stats['hardConditions'] == stats['individualHardConditions'] == 0
    report = {'kind': 'frozen-critic-synthetic-cycle-outcomes-v1', 'graph': graph_id, 'view': view,
        'sourceDirectory': str(source), 'sourceInputs': {name: digest(source/name) for name in INPUT_FILES},
        'checkpoint': str(checkpoint), 'checkpointSha256': digest(checkpoint),
        'environment': str(environment), 'environmentSha256': digest(environment),
        'initialVisual': native.initial['visual'], 'initialIndividualVisual': native.initial['individualVisual'],
        'initialHard': baseline['hard'], 'initialIndividualHard': baseline['individualHard'],
        'final': stats, 'attempts': attempts, 'improvingLegalActions': improving, 'accepted': accepted,
        'vocabularySeed': vocabulary_seed, 'samplingSeed': sampling_seed, 'triples': 128, 'support': support,
        'policy': policy, 'featureChunkHashes': hashes, 'scaleFeatureMaxError': error,
        'reasons': dict(reasons), 'wallSeconds': time.monotonic()-started, 'newTrainingUpdates': 0,
        'positionsOptimizedOrRepaired': False, 'nativeSearchCalls': 0, 'codeSha256': code_hashes(),
        'observationsSha256': digest(stage/'observations.npz'), 'actionsSha256': digest(stage/'actions.jsonl'),
        'labelsSha256': digest(stage/'labels.npz'), 'sourceScoresUnchangedAcrossActions': True}
    (stage/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'graph': graph_id, 'view': view, 'actions': attempts, 'improvingLegalActions': improving,
        'initial': native.initial['visual'], 'final': best_visual}), flush=True)
    return report


def collect(args):
    assert 0<=args.start<64 and 1<=args.graphs<=32 and args.start+args.graphs<=64
    args.root.mkdir(parents=True, exist_ok=True)
    assert digest(args.checkpoint) == 'bd4d0612bd9abfdfdb0ccf4e4cbb1192098d212c4691d0096220afcbf8649891'
    assert digest(args.environment) == 'b5c355b2675d225114279549051ef201d84d6e56dbe8658810cc9705004f5514'
    reports = []; started = time.monotonic(); reused = 0
    for graph in range(args.start, args.start+args.graphs):
        directory = args.root/f'graph-{graph:02}'/'input'
        if not directory.exists():
            fixture(directory, 86243+graph)
        else:
            info = json.loads((directory/'fixture.json').read_text())
            assert info['seed'] == 86243+graph
            assert all(digest(directory/name) == value for name, value in info['sourceInputs'].items())
        existing = args.root/f'graph-{graph:02}'/'overview'/'report.json'
        if existing.exists():
            report = json.loads(existing.read_text())
            assert report['checkpointSha256'] == digest(args.checkpoint)
            assert report['sourceInputs'] == json.loads((directory/'fixture.json').read_text())['sourceInputs']
            reports.append(report); reused += 1
        else:
            reports.append(collect_stage(args.root, graph, directory, 'overview', args.checkpoint, args.environment))
        full_directory = directory.parent/'input-individual'
        flatten_fixture(directory, full_directory)
        reports.append(collect_stage(args.root, graph, full_directory, 'individual', args.checkpoint, args.environment,
            stage_name='individual-flat'))
    summary = {'graphs': args.graphs, 'start': args.start, 'stages': len(reports),
        'actions': sum(row['attempts'] for row in reports),
        'improvingLegalActions': sum(row['improvingLegalActions'] for row in reports),
        'graphsWithImprovingActions': len({row['graph'] for row in reports if row['improvingLegalActions']}),
        'reusedOverviewStagesWithoutNativeRerun': reused, 'originalGroupedIndividualProbeStagesExcluded': True,
        'individualFullGeometryMatchesActualView': True,
        'wallSeconds': time.monotonic()-started, 'codeSha256': code_hashes()}
    path = args.root/f'collection-flat-{args.start:02}-{args.graphs:02}.json'; assert not path.exists()
    path.write_text(json.dumps(summary, indent=2)+'\n'); print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, required=True); parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--start', type=int, default=0); parser.add_argument('--graphs', type=int, default=4)
    collect(parser.parse_args())
