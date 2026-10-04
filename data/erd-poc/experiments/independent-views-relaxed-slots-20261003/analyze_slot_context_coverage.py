"""Conservative fixed-conflict bound for same-size slots and pooled anchor rays."""
import hashlib
import json
import sys
from pathlib import Path
sys.path.insert(0, 'scripts/erd-poc')
import numpy as np
from learned_global_replay import pairs, routes
from joint_slot_anchor_policy import SlotAnchorRayPolicy
from joint_neural_ports import PerimeterRoutes

base = Path(__file__).parent
out = base / 'slot-context-coverage1'
out.mkdir(exist_ok=False)
report = {'scope': 'arbitrary same-size slot permutations with pooled source interior-anchor endpoint phases',
          'positionsProposed': False, 'globalTargetImpossibleProved': False,
          'independentPortOrOtherNodePoliciesCovered': False, 'views': {}}
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
for view, goal in [('individual', 750), ('overview', 150)]:
    directory = base / (view + '-slot-cached1')
    workflow = json.loads((directory / ('workflow.audit.json' if view == 'individual' else 'workflow.json')).read_text())
    assert workflow['allChecksPassed']
    model = SlotAnchorRayPolicy.load(directory / 'reward-initial-policy.npz')
    provider = PerimeterRoutes(directory)
    movable = np.zeros(len(provider.physical_ids), dtype=bool)
    for begin, end in zip(model.group_bounds[:-1], model.group_bounds[1:]):
        if end-begin > 1: movable[model.slot_order[begin:end]] = True
    moved = {key for key, flag in zip(provider.physical_ids, movable) if flag}
    components = {row[0]: row[1:] for row in (line.split('\t') for line in (directory / 'components.tsv').read_text().splitlines())}
    moved_full = {member for node in moved for member in components[node]}
    full_edges = [line.split('\t') for line in (directory / 'individual.edges.tsv').read_text().splitlines()]
    edge_move = movable[model.owner_edges].any(1)
    # Mean pooling may change a fixed edge's endpoint if another edge in its
    # source endpoint group has a movable owner. Include that indirect support.
    phase_groups = np.zeros(len(model.endpoint_counts), dtype=bool)
    np.maximum.at(phase_groups, model.endpoint_groups, np.repeat(edge_move, 2))
    pooled_move = phase_groups[model.endpoint_groups].reshape(-1, 2).any(1)
    changing_full_edges = {row[0] for row, flag in zip(full_edges, edge_move | pooled_move) if flag}
    groups = {row[0]: row[1:] for row in (line.split('\t') for line in (directory / 'groups.tsv').read_text().splitlines())}
    changing_groups = {key for key, members in groups.items() if set(members) & changing_full_edges}
    positions, dimensions = pairs(directory / 'positions.tsv'), pairs(directory / 'nodes.tsv')
    ids = list(positions)
    index = {node: n for n, node in enumerate(ids)}
    p = np.array(list(positions.values()))
    size = np.array([dimensions[node] for node in ids])
    edge_rows = [line.split('\t') for line in (directory / 'edges.tsv').read_text().splitlines()]
    edges = np.array([[index[a], index[b]] for _, a, b in edge_rows])
    edge_ids = [row[0] for row in edge_rows]
    route_map = routes(directory / 'routes.tsv')
    ports = np.array([route_map[key] for key in edge_ids])
    low, high, direction = ports.min(1), ports.max(1), ports[:, 1] - ports[:, 0]
    box_low, box_high = p - size / 2 - 10, p + size / 2 + 10
    crossing_witnesses, hit_witnesses = [], []
    cross_total = hit_total = 0
    cross = lambda a, b: a[..., 0] * b[..., 1] - a[..., 1] * b[..., 0]
    for i, key in enumerate(edge_ids):
        candidates = np.flatnonzero((np.arange(len(edges)) > i) & (low[i] <= high).all(1) & (low <= high[i]).all(1))
        a, b = ports[i]
        other, d = ports[candidates], direction[candidates]
        good = ((cross(b - a, other[:, 0] - a) * cross(b - a, other[:, 1] - a) < -1e-9) &
                (cross(d, a - other[:, 0]) * cross(d, b - other[:, 0]) < -1e-9))
        for j in candidates[good]:
            cross_total += 1
            if key not in changing_groups and edge_ids[j] not in changing_groups:
                crossing_witnesses.append([key, edge_ids[j]])
        candidates = np.flatnonzero((high[i] > box_low).all(1) & (low[i] < box_high).all(1) &
            (np.arange(len(p)) != edges[i, 0]) & (np.arange(len(p)) != edges[i, 1]))
        lo, hi, good = np.zeros(len(candidates)), np.ones(len(candidates)), np.ones(len(candidates), dtype=bool)
        for axis in range(2):
            if abs(direction[i, axis]) < 1e-9:
                good &= (a[axis] > box_low[candidates, axis]) & (a[axis] < box_high[candidates, axis])
            else:
                first = (box_low[candidates, axis] - a[axis]) / direction[i, axis]
                second = (box_high[candidates, axis] - a[axis]) / direction[i, axis]
                lo, hi = np.maximum(lo, np.minimum(first, second)), np.minimum(hi, np.maximum(first, second))
                good &= hi - lo > 1e-9
        good &= (hi > 1e-9) & (lo < 1 - 1e-9)
        for n in candidates[good]:
            hit_total += 1
            if key not in changing_groups and ids[n] not in moved:
                hit_witnesses.append([key, ids[n]])
    expected = 1964 if view == 'individual' else 289
    assert cross_total + hit_total == expected
    fixed = len(crossing_witnesses) + len(hit_witnesses)
    witnesses = out / (view + '-fixed-conflicts.json')
    witnesses.write_text(json.dumps({'sourceSha256': workflow['sourceSha256'],
        'initialCheckpointSha256': digest(directory / 'reward-initial-policy.npz'), 'fixedCrossings': crossing_witnesses,
        'fixedCardHits': hit_witnesses}, indent=2) + '\n')
    report['views'][view] = {'sourceSha256': workflow['sourceSha256'], 'sourceVisual': expected,
        'target': goal, 'physicalCardsMovable': len(moved), 'fullCardsMovable': len(moved_full),
        'potentiallyChangingPhysicalRoutes': len(changing_groups),
        'fixedPhysicalCards': len(positions)-len(moved),
        'indirectlyAffectedFullRoutesFromPooling': int(np.count_nonzero(pooled_move & ~edge_move)),
        'fixedCrossings': len(crossing_witnesses), 'fixedCardHits': len(hit_witnesses),
        'fixedVisualConflicts': fixed, 'targetUnreachableWithThisActionSpaceAlone': fixed > goal,
        'maximumRemovableSourceConflicts': expected - fixed,
        'witnessFile': str(witnesses), 'witnessSha256': digest(witnesses),
        'initialCountMatchesNative': True}
(out / 'analysis-driver.py').write_text(Path(__file__).read_text())
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report), flush=True)
