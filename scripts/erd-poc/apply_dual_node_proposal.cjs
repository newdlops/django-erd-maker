// Validate bounded ordinary-card translations using both actual product views.
// Execute inside run_memory_bounded.py; the source candidate remains immutable.
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const crypto = require('node:crypto');
const [sourceFile, payloadFile, directory, proposalFile] = process.argv.slice(2);
assert.ok(proposalFile, 'source.layout.json payload.json experiment-directory proposal.tsv');
const product = require(path.resolve(__dirname, '../../out/webview/state/createDiagramRenderModel.js'));
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const source = read(sourceFile), payload = read(payloadFile), exported = read(path.join(directory, 'export.json'));
assert.equal(digest(sourceFile), exported.sourceSha256);
assert.equal(digest(payloadFile), exported.payloadSha256);
assert.equal(exported.resourcePolicy.withIndividual, true);
const render = layout => product.createDiagramRenderModel({...payload, layout, view:{...payload.view, tableOptions:[]}});
const individual = model => ({...model, edges:model.leafCardOverview.individualEdges, leafCardOverview:undefined});
const scene = render(source), full = individual(scene);
assert.ok(scene.relationshipOverview && scene.leafCardOverview);
const tables = product.getRenderedConnectionTables(scene);
const parsePoints = text => text.trim().split(/\s+/).map(pair => {
  const xy = pair.split(',').map(Number);assert.equal(xy.length, 2);assert.ok(xy.every(Number.isFinite));
  return {x:xy[0], y:xy[1]};
});
const serialize = points => points.map(p => `${p.x},${p.y}`).join(' ');
function readTsv(file, convert) {
  const result = new Map();
  for (const line of fs.readFileSync(file, 'utf8').trim().split('\n')) {
    const [id, ...fields] = line.split('\t');assert.ok(!result.has(id));result.set(id, convert(fields));
  }
  return result;
}
const readPositions = file => readTsv(file, fields => {
  assert.equal(fields.length, 2);const [x,y] = fields.map(Number);assert.ok([x,y].every(Number.isFinite));return {x,y};
});
const readRoutes = file => readTsv(file, fields => {
  assert.equal(fields.length, 1);const points = parsePoints(fields[0]);assert.equal(points.length, 2);return points;
});
const positions = readPositions(proposalFile), routes = readRoutes(proposalFile + '.routes.tsv');
const individualPositions = readPositions(proposalFile + '.individual');
const individualRoutes = readRoutes(proposalFile + '.individual.routes.tsv');
const sameKeys = (map, ids) => assert.deepEqual(new Set(map.keys()), new Set(ids));
sameKeys(positions, tables.map(n => n.modelId));sameKeys(routes, scene.edges.map(e => e.edgeId));
sameKeys(individualPositions, scene.tables.map(n => n.modelId));sameKeys(individualRoutes, full.edges.map(e => e.edgeId));
const singleton = new Set(scene.edges.filter(e => !e.leafCardEndpointIds
  && e.memberEdgeIds?.length === 1 && e.memberEdgeIds[0] === e.edgeId).map(e => e.edgeId));
const leafMembers = new Set(scene.leafCards.flatMap(c => c.memberModelIds));
const candidate = structuredClone(source), nodes = new Map(candidate.nodes.map(n => [n.modelId,n]));
const changedNodes = [], changedRoutes = [], stats = read(proposalFile + '.stats.json');
let learnedPolicy;
if (stats.proposalAuthority === 'external-policy') {
  learnedPolicy = read(proposalFile + '.policy.json');
  assert.equal(learnedPolicy.untrainedControl, false, 'an untrained control cannot become a learned candidate');
  assert.equal(learnedPolicy.checkpointSha256, digest(learnedPolicy.checkpoint));
  assert.deepEqual(learnedPolicy.final, stats);
  assert.equal(stats.heuristicSearchCalls, 0);
}
assert.ok(stats.maxDisplacement > 0 && stats.maxDisplacement <= 512);
for (const table of tables) {
  const p = positions.get(table.modelId), center = {x:table.position.x + table.size.width/2, y:table.position.y + table.size.height/2};
  if (Math.hypot(p.x-center.x, p.y-center.y) < 1e-6) continue;
  assert.ok(nodes.has(table.modelId) && !leafMembers.has(table.modelId), 'only ordinary cards may move');
  const incident = scene.edges.filter(e => e.sourceModelId === table.modelId || e.targetModelId === table.modelId);
  assert.ok(incident.every(e => singleton.has(e.edgeId)), 'every attached overview line must be a singleton');
  assert.deepEqual(new Set(incident.map(e => e.edgeId)), new Set(full.edges
    .filter(e => e.sourceModelId === table.modelId || e.targetModelId === table.modelId).map(e => e.edgeId)));
  const distance = Math.hypot(p.x-center.x, p.y-center.y);assert.ok(distance <= stats.maxDisplacement + 1e-6);
  const node = nodes.get(table.modelId);assert.deepEqual(node.size, table.size);
  node.position = {x:p.x-node.size.width/2, y:p.y-node.size.height/2};
  changedNodes.push({modelId:table.modelId, before:table.position, after:node.position, distance});
}
const canonical = new Map(candidate.routedEdges.map(e => [e.edgeId,e]));
for (const edge of scene.edges) {
  const points = routes.get(edge.edgeId);
  if (serialize(points) === serialize(parsePoints(edge.points))) continue;
  assert.ok(singleton.has(edge.edgeId), 'group route changed');
  assert.ok(changedNodes.some(n => n.modelId === edge.sourceModelId || n.modelId === edge.targetModelId));
  canonical.get(edge.edgeId).points = points;
  const group = candidate.engineMetadata.renderedCarrierRoutes.find(g => g.memberEdgeIds.includes(edge.edgeId));
  assert.equal(group.memberEdgeIds.length, 1);group.points = points;changedRoutes.push(edge.edgeId);
}
for (let i=0;i<candidate.nodes.length;i++) {
  const {position, ...rest} = candidate.nodes[i], {position:oldPosition, ...oldRest} = source.nodes[i];
  assert.deepEqual(rest, oldRest);
  const p = individualPositions.get(candidate.nodes[i].modelId);
  assert.ok(Math.abs(position.x + candidate.nodes[i].size.width/2 - p.x) < 1e-6);
  assert.ok(Math.abs(position.y + candidate.nodes[i].size.height/2 - p.y) < 1e-6);
}
assert.deepEqual(candidate.engineMetadata.leafBundles, source.engineMetadata.leafBundles);
assert.deepEqual(candidate.engineMetadata.renderedCarrierRoutes.map(g => [g.carrierId,g.memberEdgeIds]),
  source.engineMetadata.renderedCarrierRoutes.map(g => [g.carrierId,g.memberEdgeIds]));
const actual = render(candidate), actualIndividual = individual(actual);
assert.deepEqual(actual.leafCards, scene.leafCards);
assert.deepEqual(actual.edges.map(e => [e.edgeId,e.sourceModelId,e.targetModelId,e.memberEdgeIds,e.leafCardEndpointIds]),
  scene.edges.map(e => [e.edgeId,e.sourceModelId,e.targetModelId,e.memberEdgeIds,e.leafCardEndpointIds]));
for (const [model, expected] of [[actual,routes],[actualIndividual,individualRoutes]])
  for (const edge of model.edges) assert.equal(serialize(parsePoints(edge.points)), serialize(expected.get(edge.edgeId)), edge.edgeId);
const baseline = product.measureRenderedVisualConflicts(scene), metrics = product.measureRenderedVisualConflicts(actual);
const originalIndividual = product.measureRenderedVisualConflicts(full), candidateIndividual = product.measureRenderedVisualConflicts(actualIndividual);
const baselineClearance = product.measureRenderedTableClearance(scene), clearance = product.measureRenderedTableClearance(actual);
const memberClearance = product.measureRenderedTableClearance({...actual,leafCardOverview:undefined});
assert.equal(stats.initialVisual, baseline.visualCrossings);assert.equal(stats.visual, metrics.visualCrossings);
assert.equal(stats.initialIndividualVisual, originalIndividual.visualCrossings);assert.equal(stats.individualVisual, candidateIndividual.visualCrossings);
assert.ok(metrics.visualCrossings < baseline.visualCrossings && candidateIndividual.visualCrossings <= originalIndividual.visualCrossings);
assert.ok(stats.hardConditions <= stats.initialHardConditions && stats.individualHardConditions <= stats.initialIndividualHardConditions);
assert.equal(stats.overlap, 0);assert.equal(stats.spacing, 0);
assert.equal(clearance.spacingViolations, 0);assert.equal(memberClearance.spacingViolations, 0);
assert.ok(clearance.bboxArea <= baselineClearance.bboxArea + .01);
candidate.engineMetadata = {...candidate.engineMetadata, ...metrics, boundingBoxArea:clearance.bboxArea,
  strategy:learnedPolicy ? 'learned-leaf-card-policy' : 'preserved-leaf-groups-local-card-polish',
  actualAlgorithm:learnedPolicy ? 'ConditionalMixtureDensityPolicy(geometry validation only)' : 'DualViewAttachedPortNodeSearch(low-resource research)'};
const candidateFile = path.join(directory, 'candidate.layout.json');
fs.writeFileSync(candidateFile, JSON.stringify(candidate) + '\n');
const report = {sourceFile,candidateFile,sourceSha256:digest(sourceFile),candidateSha256:digest(candidateFile),
  baseline,metrics,originalIndividual,candidateIndividual,bboxB:clearance.bboxArea/1e9,changedNodes,changedRoutes,
  sizesPreserved:true,membershipsPreserved:true,leafCardGeometryPreserved:true,productRouteParity:true,
  nativeProposal:stats,learnedPolicy,resourcePolicy:{...exported.resourcePolicy,
    ...(learnedPolicy ? {maxPolicyProposals:learnedPolicy.budget} : {maxSelectedNodes:256}),
    maxNodeDisplacement:stats.maxDisplacement},browserVerified:false};
fs.writeFileSync(path.join(directory,'proposal.audit.json'), JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({...report,changedNodes:changedNodes.length,changedRoutes:changedRoutes.length}));
