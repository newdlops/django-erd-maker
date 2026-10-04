const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const {createDiagramRenderModel, measureRenderedVisualConflicts, measureRenderedTableClearance} = require('../../out/webview/state/createDiagramRenderModel.js');
const {getBrowserControllerScript} = require('../../out/webview/interaction/browserController.js');
const {getBrowserLeafCardSource} = require('../../out/webview/interaction/runtime/browserLeafCardSource.js');
const {getBrowserLayoutSource} = require('../../out/webview/interaction/runtime/browserLayoutSource.js');
const {getBrowserCanvasDrawSource} = require('../../out/webview/interaction/runtime/browserCanvasDrawSource.js');
const [payloadFile, layoutFile, outputFile] = process.argv.slice(2);
assert.ok(outputFile, 'payload.json layout.json output.json');
const payload = JSON.parse(fs.readFileSync(payloadFile, 'utf8'));
const scene = createDiagramRenderModel({...payload, layout: JSON.parse(fs.readFileSync(layoutFile, 'utf8'))});
const models = new Map(scene.inspector.models.map(model => [model.modelId, model]));
const tables = new Map(scene.tables.map(n => [n.modelId, {modelId: n.modelId, ...n.position, ...n.size}]));
const metas = new Map(scene.tables.map(n => [n.modelId, {basePosition: n.position, width: n.size.width, height: n.size.height}]));
const relationships = new Map(scene.inspector.models.flatMap(model => model.relationships).map(r => [r.edgeId, r]));
const individual = scene.leafCardOverview?.individualEdges || scene.relationshipOverview?.individualEdges || scene.edges;
const canonicalIds = new Set(individual.map(edge => edge.edgeId));
const members = edge => edge.memberEdgeIds?.length ? edge.memberEdgeIds : [edge.edgeId];
const context = {performance, state: {}, renderModel: scene, tableMetaById: metas, relationshipByEdgeId: relationships,
  lookupPosition: id => metas.get(id).basePosition, isVisibleModel: id => metas.has(id)};
const controller = getBrowserControllerScript('audit');
const selection = controller.slice(controller.indexOf('const readEdgeMeta ='), controller.indexOf('const tableMetaList ='));
const api = vm.runInNewContext(selection + getBrowserLeafCardSource() + getBrowserLayoutSource() + getBrowserCanvasDrawSource()
  + ';getCurrentPosition=lookupPosition;({getActiveEdgeMeta,createLeafCardRecords,projectLeafCardEdges,getStaticOrCatalogEdgePaths,edgeRelationshipKinds})', context);
const render = modelId => {
  context.state.selectedModelId = modelId;
  const graph = {tablesById: tables, leafBundles: api.createLeafCardRecords(scene.leafCards || [], tables, modelId)};
  const entries = api.getActiveEdgeMeta().filter(e => metas.has(e.sourceModelId) && metas.has(e.targetModelId)).map(meta => ({meta,
    sourceTable: metas.get(meta.sourceModelId), targetTable: metas.get(meta.targetModelId),
    sourcePosition: metas.get(meta.sourceModelId).basePosition, targetPosition: metas.get(meta.targetModelId).basePosition}));
  return {graph, routes: api.projectLeafCardEdges(graph, api.getStaticOrCatalogEdgePaths(entries))};
};
const baseline = render(undefined);
const snapshot = routes => JSON.stringify(routes.map(r => [r.edgeId, r.points, r.meta.memberEdgeIds]));
const original = snapshot(baseline.routes);
const owner = new Map(baseline.graph.leafBundles.flatMap(card => card.memberModelIds.map(id => [id, card.id])));
const physical = id => owner.get(id) || id;
const pair = ids => [...ids].sort().join('|');
const boxes = new Map([...tables, ...baseline.graph.leafBundles.map(card => [card.id, card])]);
const endpoints = edge => [edge.leafCardEndpointIds?.[0] || edge.sourceModelId, edge.leafCardEndpointIds?.[1] || edge.targetModelId];
const report = {modelsChecked: 0, directPairsChecked: 0, boundaryEndpointsChecked: 0,
  canonicalRelationships: canonicalIds.size, canonicalCoverageExactlyOnce: true, examples: [], unavailableRelationships: 0};
for (const model of models.values()) {
  const {graph, routes} = render(model.modelId), root = physical(model.modelId);
  const expected = new Set();
  for (const r of model.relationships) {
    if (!tables.has(r.otherModelId)) {report.unavailableRelationships++; continue;}
    if (physical(r.otherModelId) !== root) expected.add(pair([root, physical(r.otherModelId)]));
  }
  const highlighted = routes.filter(r => api.edgeRelationshipKinds(r.meta, model.modelId).length);
  const actual = new Set();
  for (const route of highlighted) {
    const ends = endpoints(route.meta);
    assert.ok(ends.includes(root), model.modelId + ' detached ' + route.edgeId);
    const key = pair(ends);
    assert.equal(actual.has(key), false, model.modelId + ' duplicate physical pair ' + key);
    actual.add(key);
    assert.equal(route.points.length, 2, route.edgeId);
    for (let end = 0; end < 2; end++) {
      const box = boxes.get(ends[end]), point = route.points[end];
      assert.ok(box, ends[end]);
      const left = box.x, right = left + box.width, top = box.y, bottom = top + box.height;
      assert.ok(point.x >= left - .03 && point.x <= right + .03 && point.y >= top - .03 && point.y <= bottom + .03, route.edgeId);
      assert.ok(Math.min(Math.abs(point.x-left), Math.abs(point.x-right), Math.abs(point.y-top), Math.abs(point.y-bottom)) < .03, route.edgeId);
      report.boundaryEndpointsChecked++;
    }
  }
  assert.deepEqual(actual, expected, model.modelId + ' physical neighbor coverage');
  const coverage = [...routes.flatMap(r => members(r.meta)), ...(graph.leafCardInternalEdgeIds || [])];
  assert.equal(coverage.length, canonicalIds.size, model.modelId + ' canonical count');
  assert.deepEqual(new Set(coverage), canonicalIds, model.modelId + ' canonical coverage');
  report.modelsChecked++; report.directPairsChecked += expected.size;
  if (['db.Company', 'db.Address', 'db.MeetingDocument', 'db.MeetingDocumentCache'].includes(model.modelId)) report.examples.push({modelId: model.modelId,
    relationships: model.relationships.length, expectedPeerCards: expected.size, directConnections: actual.size,
    missingConnections: 0, detachedHighlightedLines: 0});
}
assert.equal(snapshot(render(undefined).routes), original, 'clearing selection restores all overview routes');
report.overviewRestoredExactly = true;
report.overviewVisualCrossings = measureRenderedVisualConflicts(scene).visualCrossings;
report.overviewAreaB = measureRenderedTableClearance(scene).bboxArea / 1e9;
report.overviewLines = baseline.routes.length;
fs.writeFileSync(outputFile, JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report));
