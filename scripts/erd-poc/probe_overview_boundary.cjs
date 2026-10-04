// Export the actual overview to the bounded native boundary sweep, then audit
// a proposal with the product renderer. Run inside run_memory_bounded.py.
// Only singleton, non-Leaf routes can move; every card and group stays fixed.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const product = require(path.join(root, 'out/webview/state/createDiagramRenderModel.js'));
const [operation, sourceFile, payloadFile, directory, proposalFile] = process.argv.slice(2);
assert.ok(['export', 'apply'].includes(operation) && directory,
  'export|apply source.layout.json payload.json directory [proposal.tsv] [--max-routes 80] [--with-individual]');
const maxRoutesIndex = process.argv.indexOf('--max-routes');
const maxRoutes = maxRoutesIndex < 0 ? 80 : Number(process.argv[maxRoutesIndex + 1]);
assert.ok(Number.isInteger(maxRoutes) && maxRoutes >= 1 && maxRoutes <= 256,
  '--max-routes must be an integer between 1 and 256');
assert.ok(maxRoutesIndex < 0 || operation === 'export', '--max-routes applies to export');
const withIndividual = process.argv.includes('--with-individual');
assert.ok(!withIndividual || operation === 'export', '--with-individual applies to export');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const write = (name, value) => fs.writeFileSync(path.join(directory, name), JSON.stringify(value, null, 2) + '\n');
const source = read(sourceFile), payload = read(payloadFile);
const render = layout => product.createDiagramRenderModel({
  ...payload, layout, view: {...payload.view, tableOptions: []},
});
const scene = render(source);
assert.ok(scene.relationshipOverview && scene.leafCardOverview, 'requires an intact grouped overview');
const tables = product.getRenderedConnectionTables(scene);
const points = edge => edge.points.split(/\s+/).map(pair => {
  const xy = pair.split(',').map(Number);
  assert.equal(xy.length, 2); assert.ok(xy.every(Number.isFinite));
  return {x: xy[0], y: xy[1]};
});
const physicalIds = edge => [edge.leafCardEndpointIds?.[0] || edge.sourceModelId,
  edge.leafCardEndpointIds?.[1] || edge.targetModelId];
const serialPoints = ps => ps.map(p => `${p.x},${p.y}`).join(' ');
const orientation = (a, b, c) => (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
const crosses = ([p, q], [r, s]) => orientation(p, q, r) * orientation(p, q, s) < -1e-9
  && orientation(r, s, p) * orientation(r, s, q) < -1e-9;
const individualModel = model => ({...model, edges: model.leafCardOverview.individualEdges,
  leafCardOverview: undefined});
const edgeCost = (model, edge) => product.measureRenderedEdgeNodeIntersections({...model, edges: [edge]}).count
  + model.edges.reduce((sum, other) => sum + (other.edgeId !== edge.edgeId && crosses(points(edge), points(other)) ? 1 : 0), 0);
const eligible = scene.edges.filter(edge => !edge.leafCardEndpointIds
  && edge.memberEdgeIds?.length === 1 && edge.memberEdgeIds[0] === edge.edgeId);
const baseline = product.measureRenderedVisualConflicts(scene);
const baselineClearance = product.measureRenderedTableClearance(scene);
fs.mkdirSync(directory, {recursive: true});

if (operation === 'export') {
  const tableIds = new Set(tables.map(table => table.modelId));
  const ids = [...tableIds, ...scene.edges.map(edge => edge.edgeId)];
  assert.ok(ids.every(id => !/[\t\r\n]/.test(id)), 'TSV identifiers must be single fields');
  fs.writeFileSync(path.join(directory, 'nodes.tsv'), tables.map(table =>
    [table.modelId, table.size.width, table.size.height].join('\t')).join('\n') + '\n');
  fs.writeFileSync(path.join(directory, 'positions.tsv'), tables.map(table =>
    [table.modelId, table.position.x + table.size.width / 2,
      table.position.y + table.size.height / 2].join('\t')).join('\n') + '\n');
  fs.writeFileSync(path.join(directory, 'edges.tsv'), scene.edges.map(edge => {
    const endpoints = physicalIds(edge);
    assert.ok(endpoints.every(id => tableIds.has(id)));
    assert.equal(points(edge).length, 2);
    return [edge.edgeId, ...endpoints].join('\t');
  }).join('\n') + '\n');
  fs.writeFileSync(path.join(directory, 'routes.tsv'), scene.edges.map(edge =>
    edge.edgeId + '\t' + edge.points).join('\n') + '\n');
  let individualBaseline;
  if (withIndividual) {
    const individual = individualModel(scene), nodes = product.getRenderedConnectionTables(individual);
    const fullIds = new Set(nodes.map(table => table.modelId));
    assert.ok([...fullIds, ...individual.edges.map(edge => edge.edgeId)].every(id => !/[\t\r\n]/.test(id)));
    fs.writeFileSync(path.join(directory, 'individual.nodes.tsv'), nodes.map(table =>
      [table.modelId, table.size.width, table.size.height].join('\t')).join('\n') + '\n');
    fs.writeFileSync(path.join(directory, 'individual.positions.tsv'), nodes.map(table =>
      [table.modelId, table.position.x + table.size.width / 2,
        table.position.y + table.size.height / 2].join('\t')).join('\n') + '\n');
    fs.writeFileSync(path.join(directory, 'individual.edges.tsv'), individual.edges.map(edge => {
      assert.ok(edge.preserveRouteEndpoints && points(edge).length === 2);
      assert.ok([edge.sourceModelId, edge.targetModelId].every(id => fullIds.has(id)));
      return [edge.edgeId, edge.sourceModelId, edge.targetModelId].join('\t');
    }).join('\n') + '\n');
    fs.writeFileSync(path.join(directory, 'individual.routes.tsv'), individual.edges.map(edge =>
      edge.edgeId + '\t' + edge.points).join('\n') + '\n');
    individualBaseline = product.measureRenderedVisualConflicts(individual);
  }
  const pressure = new Map(scene.edges.map(edge => [edge.edgeId, 0]));
  const add = id => pressure.set(id, pressure.get(id) + 1);
  for (const hit of product.measureRenderedEdgeNodeIntersections(scene).hits) add(hit.edgeId);
  const routes = scene.edges.map(points);
  for (let a = 0; a < routes.length; a++) for (let b = a + 1; b < routes.length; b++) {
    if (crosses(routes[a], routes[b])) {
      add(scene.edges[a].edgeId); add(scene.edges[b].edgeId);
    }
  }
  const selected = eligible.filter(edge => pressure.get(edge.edgeId) > 0)
    .sort((a, b) => pressure.get(b.edgeId) - pressure.get(a.edgeId)
      || a.edgeId.localeCompare(b.edgeId)).slice(0, maxRoutes);
  fs.writeFileSync(path.join(directory, 'selection.txt'), selected.map(edge => edge.edgeId).join('\n') + '\n');
  fs.writeFileSync(path.join(directory, 'eligible-singletons.txt'), eligible.map(edge => edge.edgeId).join('\n') + '\n');
  const report = {sourceFile, payloadFile, sourceSha256: digest(sourceFile), payloadSha256: digest(payloadFile),
    baseline, individualBaseline, bboxB: baselineClearance.bboxArea / 1e9,
    physicalCards: tables.length, canonicalRoutes: source.routedEdges.length,
    eligibleRoutes: eligible.length, selectedRoutes: selected.length,
    selection: selected.map(edge => ({edgeId: edge.edgeId, conflicts: pressure.get(edge.edgeId)})),
    resourcePolicy: {threads: 1, rssLimitMiB: 256, maxSearchSeconds: 20, maxSelectedRoutes: maxRoutes, withIndividual}, browserVerified: false};
  write('export.json', report);
  console.log(JSON.stringify({...report, selection: undefined}));
} else {
  assert.ok(proposalFile, 'apply requires the native proposal TSV');
  const exported = read(path.join(directory, 'export.json'));
  assert.equal(digest(sourceFile), exported.sourceSha256, 'source changed since export');
  assert.equal(digest(payloadFile), exported.payloadSha256, 'payload changed since export');
  const selected = new Set(exported.selection.map(edge => edge.edgeId));
  const proposals = new Map();
  for (const line of fs.readFileSync(proposalFile + '.routes.tsv', 'utf8').trim().split('\n')) {
    const [id, text, extra] = line.split('\t');
    assert.equal(extra, undefined); assert.ok(!proposals.has(id));
    const ps = points({points: text}); assert.equal(ps.length, 2);
    proposals.set(id, ps);
  }
  assert.deepEqual(new Set(proposals.keys()), new Set(scene.edges.map(edge => edge.edgeId)));
  let candidate = structuredClone(source);
  let canonical = new Map(candidate.routedEdges.map(edge => [edge.edgeId, edge]));
  let changed = [];
  for (const edge of scene.edges) {
    const ps = proposals.get(edge.edgeId);
    if (serialPoints(ps) === serialPoints(points(edge))) continue;
    assert.ok(selected.has(edge.edgeId), 'unselected route changed: ' + edge.edgeId);
    assert.ok(eligible.some(other => other.edgeId === edge.edgeId));
    canonical.get(edge.edgeId).points = ps;
    changed.push(edge.edgeId);
    const group = candidate.engineMetadata.renderedCarrierRoutes.find(g => g.memberEdgeIds.includes(edge.edgeId));
    assert.equal(group.memberEdgeIds.length, 1);
    group.points = ps;
  }
  let actual = render(candidate);
  assert.deepEqual(candidate.nodes, source.nodes);
  assert.deepEqual(candidate.engineMetadata.renderedCarrierRoutes.map(g => [g.carrierId, g.memberEdgeIds]),
    source.engineMetadata.renderedCarrierRoutes.map(g => [g.carrierId, g.memberEdgeIds]));
  assert.deepEqual(actual.leafCards, scene.leafCards);
  assert.deepEqual(actual.edges.map(edge => [edge.edgeId, physicalIds(edge), edge.memberEdgeIds]),
    scene.edges.map(edge => [edge.edgeId, physicalIds(edge), edge.memberEdgeIds]));
  for (const edge of actual.edges) assert.equal(serialPoints(points(edge)), serialPoints(proposals.get(edge.edgeId)),
    'product route disagrees with native proposal: ' + edge.edgeId);
  const proposedMetrics = product.measureRenderedVisualConflicts(actual);
  const originalIndividual = product.measureRenderedVisualConflicts(individualModel(scene));
  const proposedIndividual = product.measureRenderedVisualConflicts(individualModel(actual));
  const stats = read(proposalFile + '.stats.json');
  assert.equal(stats.initialVisual, baseline.visualCrossings);
  assert.equal(stats.visual, proposedMetrics.visualCrossings);
  assert.ok(stats.hardConditions <= stats.initialHardConditions);
  if (exported.resourcePolicy.withIndividual) {
    assert.equal(stats.initialIndividualVisual, originalIndividual.visualCrossings);
    assert.equal(stats.individualVisual, proposedIndividual.visualCrossings);
    assert.ok(stats.individualHardConditions <= stats.initialIndividualHardConditions);
    assert.ok(proposedIndividual.visualCrossings <= originalIndividual.visualCrossings);
  }
  const rejected = [];
  if (proposedIndividual.visualCrossings > originalIndividual.visualCrossings) {
    candidate = structuredClone(source);
    canonical = new Map(candidate.routedEdges.map(edge => [edge.edgeId, edge]));
    const overview = {...scene, edges: scene.edges.map(edge => ({...edge}))};
    const individual = {...individualModel(scene), edges: scene.leafCardOverview.individualEdges.map(edge => ({...edge}))};
    const proposedChanges = new Set(changed);
    changed = [];
    for (const {edgeId: id} of exported.selection) {
      if (!proposedChanges.has(id)) continue;
      const overviewIndex = overview.edges.findIndex(edge => edge.edgeId === id);
      const individualIndex = individual.edges.findIndex(edge => edge.edgeId === id);
      assert.ok(overviewIndex >= 0 && individualIndex >= 0);
      const overviewEdge = overview.edges[overviewIndex], individualEdge = individual.edges[individualIndex];
      assert.ok(overviewEdge.preserveRouteEndpoints && individualEdge.preserveRouteEndpoints);
      const text = serialPoints(proposals.get(id));
      const nextOverview = {...overviewEdge, points: text}, nextIndividual = {...individualEdge, points: text};
      const overviewDelta = edgeCost(overview, nextOverview) - edgeCost(overview, overviewEdge);
      const individualDelta = edgeCost(individual, nextIndividual) - edgeCost(individual, individualEdge);
      if (overviewDelta > 0 || individualDelta > 0) {
        rejected.push({edgeId: id, overviewDelta, individualDelta}); continue;
      }
      overview.edges[overviewIndex] = nextOverview;
      individual.edges[individualIndex] = nextIndividual;
      canonical.get(id).points = proposals.get(id);
      candidate.engineMetadata.renderedCarrierRoutes.find(g => g.memberEdgeIds.includes(id)).points = proposals.get(id);
      changed.push(id);
    }
    actual = render(candidate);
    assert.deepEqual(actual.edges.map(edge => [edge.edgeId, edge.points]), overview.edges.map(edge => [edge.edgeId, edge.points]));
  }
  const metrics = product.measureRenderedVisualConflicts(actual);
  const clearance = product.measureRenderedTableClearance(actual);
  const candidateIndividual = product.measureRenderedVisualConflicts(individualModel(actual));
  assert.deepEqual(clearance, baselineClearance);
  assert.ok(metrics.visualCrossings < baseline.visualCrossings, 'requires a strict product improvement');
  assert.ok(candidateIndividual.visualCrossings <= originalIndividual.visualCrossings, 'individual view regressed');
  candidate.engineMetadata = {...candidate.engineMetadata, ...metrics, boundingBoxArea: clearance.bboxArea,
    strategy: 'preserved-leaf-layout-boundary-polish', actualAlgorithm: exported.resourcePolicy.withIndividual
      ? 'BoundaryIntervalSweep(two views, low-resource research)' : 'BoundaryIntervalSweep(low-resource research)'};
  const candidateFile = path.join(directory, 'candidate.layout.json');
  fs.writeFileSync(candidateFile, JSON.stringify(candidate) + '\n');
  fs.writeFileSync(path.join(directory, 'accepted.routes.tsv'), actual.edges.map(edge =>
    edge.edgeId + '\t' + edge.points).join('\n') + '\n');
  const report = {sourceFile, candidateFile, sourceSha256: digest(sourceFile), candidateSha256: digest(candidateFile),
    baseline, metrics, originalIndividual, candidateIndividual, bboxB: clearance.bboxArea / 1e9,
    changedRoutes: changed, rejectedRoutes: rejected, proposedMetrics, proposedIndividual,
    nodesAndSizesPreserved: true, membershipsPreserved: true,
    productRouteParity: true, nativeProposal: stats, resourcePolicy: exported.resourcePolicy, browserVerified: false};
  write('proposal.audit.json', report);
  console.log(JSON.stringify({...report, changedRoutes: changed.length}));
}
