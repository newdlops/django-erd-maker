"""Verify that fixed-conflict witnesses stayed fixed in both learned rollouts."""
import hashlib
import json
import sys
from pathlib import Path
sys.path.insert(0, 'scripts/erd-poc')
from learned_global_replay import pairs, routes

base = Path(__file__).parent
coverage = base / 'pair-context-coverage1'
report = json.loads((coverage / 'report.json').read_text())
result = {'views': {}, 'positionsProposed': False}
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
for view in ['individual', 'overview']:
    initial = base / 'pair-context-validation2' / view
    completed = base / (view + '-pair-context1')
    witness_path = Path(report['views'][view]['witnessFile'])
    assert digest(witness_path) == report['views'][view]['witnessSha256']
    witnesses = json.loads(witness_path.read_text())
    assert digest(initial / 'branches.tsv') == witnesses['branchMapSha256']
    assert digest(completed / 'branches.tsv') == witnesses['branchMapSha256']
    before_routes, after_routes = routes(initial / 'routes.tsv'), routes(completed / 'learned.tsv.routes.tsv')
    before_nodes, after_nodes = pairs(initial / 'positions.tsv'), pairs(completed / 'learned.tsv')
    edge_ids = {edge for row in witnesses['fixedCrossings'] for edge in row}
    edge_ids.update(edge for edge, _ in witnesses['fixedCardHits'])
    card_ids = {node for _, node in witnesses['fixedCardHits']}
    for edge in edge_ids:
        assert all(abs(before_routes[edge][a][b] - after_routes[edge][a][b]) < 1e-8
                   for a in range(2) for b in range(2)), edge
    for node in card_ids:
        assert all(abs(before_nodes[node][k] - after_nodes[node][k]) < 1e-8 for k in range(2)), node
    edges = {row[0]: row[1:] for row in (line.split('\t') for line in (initial / 'edges.tsv').read_text().splitlines())}
    support = card_ids | {node for edge in edge_ids for node in edges[edge]}
    result['views'][view] = {'witnessSha256': digest(witness_path),
        'fixedPhysicalRoutesChecked': len(edge_ids), 'fixedHitCardsChecked': len(card_ids),
        'fixedConflictSupportCards': len(support), 'learnedActions': json.loads(
            (completed / 'learned.tsv.stats.json').read_text())['policyActionsEvaluated'],
        'witnessGeometryUnchanged': True, 'sourceSha256': witnesses['sourceSha256']}
with (coverage / 'rollout-witness-verification.json').open('x') as stream:
    stream.write(json.dumps(result, indent=2) + '\n')
print(json.dumps(result), flush=True)
