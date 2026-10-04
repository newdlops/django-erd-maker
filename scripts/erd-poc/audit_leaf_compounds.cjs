// Validate the expanded real-card scene with the product scorer, independently
// from the optimizer's deliberately separate compound-graph metric.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const [layoutFile, payloadFile, sourceFile, sceneFile] = process.argv.slice(2);
assert.notEqual(path.resolve(layoutFile), path.resolve(sourceFile), 'audit must not overwrite the preserved source');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const layout = read(layoutFile), payload = read(payloadFile), source = read(sourceFile);
const product = require(path.join(root, 'out/webview/state/createDiagramRenderModel.js'));
const scene = product.createDiagramRenderModel({...payload, layout, view: {...payload.view, tableOptions: []}});
const sourceNodes = new Map(source.nodes.map(n => [n.modelId, n]));
assert.equal(scene.tables.length, source.nodes.length);
assert.deepEqual(new Set(layout.nodes.map(n => n.modelId)), new Set(sourceNodes.keys()));
for (const node of layout.nodes) assert.deepEqual(node.size, sourceNodes.get(node.modelId).size);
for (const node of scene.tables) assert.deepEqual(node.size, sourceNodes.get(node.modelId).size);
const routes = new Map(source.routedEdges.map(route => [route.edgeId, route]));
assert.deepEqual(new Set(layout.routedEdges.map(e => e.edgeId)), new Set(routes.keys()));
for (const edge of layout.routedEdges) {
  assert.equal(edge.sourceModelId, routes.get(edge.edgeId).sourceModelId);
  assert.equal(edge.targetModelId, routes.get(edge.edgeId).targetModelId);
  assert.equal(edge.points.length, 2);
}
const coverage = scene.edges.flatMap(e => e.memberEdgeIds ?? [e.edgeId]);
assert.equal(coverage.length, routes.size);
assert.deepEqual(new Set(coverage), new Set(routes.keys()));
assert.deepEqual(layout.engineMetadata.renderedCarrierRoutes.map(g => [g.carrierId, [...g.memberEdgeIds].sort()]),
  source.engineMetadata.renderedCarrierRoutes.map(g => [g.carrierId, [...g.memberEdgeIds].sort()]));
const metrics = product.measureRenderedVisualConflicts(scene), clearance = product.measureRenderedTableClearance(scene);
const fullMetrics = product.measureRenderedVisualConflicts({...scene, edges: scene.relationshipOverview.individualEdges});
const report = {file: layoutFile, cards: scene.tables.length, lines: scene.edges.length, relationships: routes.size,
  movedCards: layout.nodes.filter(n => JSON.stringify(n.position) !== JSON.stringify(sourceNodes.get(n.modelId).position)).length,
  ...metrics, individual: fullMetrics, bboxB: clearance.bboxArea / 1e9, spacingViolations: clearance.spacingViolations,
  coverageExactlyOnce: true, unchangedMemberships: true, unchangedSizes: true,
  straight: scene.edges.every(e => e.points.split(/\s+/).length === 2), browserVerified: false};
layout.engineMetadata = {...layout.engineMetadata, ...metrics, boundingBoxArea: clearance.bboxArea,
  visualCrossingsScope: 'rendered-june-bundled-overview-v1'};
fs.writeFileSync(layoutFile, JSON.stringify(layout));
report.sha256 = crypto.createHash('sha256').update(fs.readFileSync(layoutFile)).digest('hex');
fs.writeFileSync(sceneFile ?? layoutFile.replace(/\.layout\.json$/, '.scene.json'), JSON.stringify(scene));
fs.writeFileSync(layoutFile.replace(/\.layout\.json$/, '.audit.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report));
