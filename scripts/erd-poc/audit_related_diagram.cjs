const fs = require('node:fs');
const assert = require('node:assert/strict');
const {createDiagramRenderModel} = require('../../out/webview/state/createDiagramRenderModel.js');
const {createRelatedDiagramTools} = require('../../out/webview/state/relatedDiagram.js');
const [payloadFile, layoutFile, outputFile] = process.argv.slice(2);
assert.ok(outputFile, 'payload.json layout.json output.json');
const payload = JSON.parse(fs.readFileSync(payloadFile, 'utf8'));
const layout = JSON.parse(fs.readFileSync(layoutFile, 'utf8'));
const scene = createDiagramRenderModel({...payload, layout});
const models = new Map(scene.inspector.models.map(model => [model.modelId, model]));
const tools = createRelatedDiagramTools();
const report = {models: models.size, relationshipsChecked: 0, examples: []};
for (const model of models.values()) {
  const graph = tools.build(model, model.relationships, models, scene.leafCards, {pageSize: 8});
  const expected = new Set(model.relationships.map(r => r.edgeId));
  const actual = [...graph.self, ...graph.peers.flatMap(p => p.relationships)];
  assert.equal(actual.length, expected.size, model.modelId);
  assert.deepEqual(new Set(actual.map(r => r.edgeId)), expected, model.modelId);
  const related = new Set(model.relationships.map(r => r.otherModelId));
  for (const peer of graph.peers) for (const id of peer.modelIds) assert.ok(related.has(id), model.modelId + ' unrelated sibling ' + id);
  const pageKeys = [];
  for (let page = 0; page < graph.pageCount; page++) pageKeys.push(...tools.build(model, model.relationships, models, scene.leafCards, {page, pageSize: 8}).visiblePeers.map(p => p.key));
  assert.equal(pageKeys.length, graph.peers.length);
  assert.equal(new Set(pageKeys).size, pageKeys.length);
  report.relationshipsChecked += actual.length;
  if (['db.MeetingDocument', 'db.Company', 'db.Address'].includes(model.modelId)) report.examples.push({model: model.modelId,
    models: graph.modelCount, relationships: graph.relationshipCount, peerCards: graph.peers.length, pages: graph.pageCount,
    leafGroups: graph.peers.filter(p => p.grouped).map(p => ({models: p.modelIds.length, connections: p.relationships.length}))});
}
report.coverage = 'Every root relationship exactly once; only direct peers; every page reachable';
fs.writeFileSync(outputFile, JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report));
