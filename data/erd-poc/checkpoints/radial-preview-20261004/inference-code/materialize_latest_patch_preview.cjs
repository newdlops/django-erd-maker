// Convert the single frozen NN output to the existing overview snapshot format.
// This copies coordinates and routes; it performs no selection or repair.
const fs = require('node:fs'), path = require('node:path');
const assert = require('node:assert/strict'), crypto = require('node:crypto');
const [directory] = process.argv.slice(2);
assert.ok(directory && path.resolve(directory).startsWith(path.resolve(__dirname, '../../.tmp') + path.sep));
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const exported = read(path.join(directory, 'export.json'));
assert.equal(exported.view, 'overview');
assert.equal(sha(exported.sourceFile), exported.sourceSha256);
assert.equal(sha(exported.payloadFile), exported.payloadSha256);
assert.equal(sha(exported.checkpoint), exported.checkpointSha256);
assert.equal(exported.latestNetworkForwardExported, true);
assert.equal(exported.coordinateSearchOrRepairs, 0);
const candidate = read(exported.sourceFile), payload = read(exported.payloadFile);
const product = require(path.resolve(__dirname, '../../out/webview/state/createDiagramRenderModel.js'));
const parse = (file, convert) => new Map(fs.readFileSync(file, 'utf8').trim().split('\n').map(line => {
  const [id, ...values] = line.split('\t'); return [id, convert(values)];
}));
const proposal = path.join(directory, 'learned.tsv');
const physicalCenters = parse(proposal, values => values.map(Number));
const parsePoints = ([text]) => text.split(' ').map(pair => {
  const [x, y] = pair.split(',').map(Number); return {x, y};
});
const centers = parse(proposal + '.individual', values => values.map(Number));
const routes = parse(proposal + '.individual.routes.tsv', parsePoints);
const physicalRoutes = parse(proposal + '.routes.tsv', parsePoints);
assert.deepEqual(new Set(centers.keys()), new Set(candidate.nodes.map(n => n.modelId)));
assert.deepEqual(new Set(routes.keys()), new Set(candidate.routedEdges.map(e => e.edgeId)));
let changedNodes = 0, changedRoutes = 0;
for (const node of candidate.nodes) {
  const [x, y] = centers.get(node.modelId);
  const next = {x: x - node.size.width / 2, y: y - node.size.height / 2};
  if (Math.hypot(next.x - node.position.x, next.y - node.position.y) > 1e-6) changedNodes++;
  node.position = next;
}
for (const edge of candidate.routedEdges) {
  const points = routes.get(edge.edgeId);
  if (JSON.stringify(points) !== JSON.stringify(edge.points)) changedRoutes++;
  edge.points = points;
}
for (const group of candidate.engineMetadata.renderedCarrierRoutes) {
  const selected = group.memberEdgeIds.map(id => ({id, points: routes.get(id)})).sort((a, b) =>
    Math.hypot(a.points[0].x - a.points[1].x, a.points[0].y - a.points[1].y)
      - Math.hypot(b.points[0].x - b.points[1].x, b.points[0].y - b.points[1].y) || a.id.localeCompare(b.id))[0];
  group.points = selected.points;
}
const scene = product.createDiagramRenderModel({...payload, layout: candidate, view: {...payload.view, tableOptions: []}});
const serial = points => points.map(p => `${p.x},${p.y}`).join(' ');
assert.equal(scene.edges.length, physicalRoutes.size);
for (const edge of scene.edges) assert.equal(edge.points, serial(physicalRoutes.get(edge.edgeId)), edge.edgeId);
for (const card of product.getRenderedConnectionTables(scene)) {
  const [x, y] = physicalCenters.get(card.modelId);
  assert.ok(Math.hypot(card.position.x + card.size.width / 2 - x,
    card.position.y + card.size.height / 2 - y) < 1e-6, card.modelId);
}
const metrics = product.measureRenderedVisualConflicts(scene), clearance = product.measureRenderedTableClearance(scene);
assert.equal(metrics.visualCrossings, exported.fullNativeMeasurement.visual);
assert.equal(clearance.spacingViolations, 0);
assert.ok(clearance.bboxArea <= 1.5e9);
const individual = read(path.join(directory, '../individual/export.json'));
assert.equal(individual.view, 'individual');
assert.equal(individual.latestNetworkForwardExported, true);
candidate.engineMetadata = {...candidate.engineMetadata, ...metrics, boundingBoxArea: clearance.bboxArea,
  actualAlgorithm: exported.modelKind, researchCandidateStatus: 'latest-trained-checkpoint-preview',
  latestCheckpointPreview: {
    overview: {checkpointSha256: exported.checkpointSha256, trainedUpdates: exported.trainedUpdates, modelKind: exported.modelKind},
    individual: {checkpointSha256: individual.checkpointSha256, trainedUpdates: individual.trainedUpdates, modelKind: individual.modelKind},
  }};
fs.writeFileSync(path.join(directory, 'candidate.layout.json'), JSON.stringify(candidate) + '\n', {flag: 'wx'});
console.log(JSON.stringify({visual: metrics.visualCrossings, changedNodes, changedRoutes, latestNetworkForwardExported: true}));
