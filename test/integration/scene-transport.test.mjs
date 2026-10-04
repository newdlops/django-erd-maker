import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import vm from 'node:vm';
import test from 'node:test';

const require = createRequire(import.meta.url);
const {createSceneTransportTools} = require('../../out/webview/state/sceneTransport.js');

test('compact scenes restore every crossing ID and shared leaf-card relationship set', () => {
  const edges = Array.from({length: 80}, (_, i) => ({edgeId: `edge-${i}`, points: `${i},0 20,30`,
    crossingIds: Array.from({length: 150}, (_, j) => `crossing-${j}-edge-contact`)}));
  const scene = {edges, inspectorModels: [{modelId: 'test.A', fieldRows: ['한글<&>']}],
    leafCardOverview: {baseEdges: edges, individualEdges: edges, visualCrossings: 21},
    relationshipOverview: {individualEdges: edges}, individualView: {edges}};
  const baseline = JSON.stringify(scene);
  const transport = createSceneTransportTools();
  const prepared = transport.prepare(scene);
  const encoded = JSON.stringify(prepared.scene, prepared.replacer);
  assert.ok(encoded.length < baseline.length / 3);
  assert.equal(JSON.stringify(scene), baseline, 'encoding must not mutate the render model');
  // Exercise the exact dependency-free function embedded into the webview.
  const browserTools = vm.runInNewContext(`(${createSceneTransportTools.toString()})()`);
  const restored = browserTools.hydrate(JSON.parse(encoded));
  assert.deepEqual(JSON.parse(JSON.stringify(restored)), JSON.parse(baseline));
  assert.equal(restored.leafCardOverview.individualEdges, restored.leafCardOverview.baseEdges);
});

test('old and small scenes retain their existing JSON and distinct relationship arrays', () => {
  const scene = {edges: [{edgeId: 'a', crossingIds: ['c1']}], leafCardOverview: {
    baseEdges: [{edgeId: 'b', crossingIds: []}], individualEdges: [{edgeId: 'c', crossingIds: []}]}};
  const tools = createSceneTransportTools();
  const prepared = tools.prepare(scene);
  const encoded = JSON.stringify(prepared.scene, prepared.replacer);
  assert.equal(encoded, JSON.stringify(scene));
  assert.deepEqual(tools.hydrate(JSON.parse(encoded)), scene);
});

test('invalid compact crossing references fail before rendering', () => {
  const tools = createSceneTransportTools();
  assert.throws(() => tools.hydrate({edges: [{crossingIds: [2]}],
    crossingIdCatalog: ['valid']}), /invalid crossing ID reference/);
  assert.throws(() => tools.hydrate({edges: [-1], edgeCatalog: [{crossingIds: []}],
    crossingIdCatalog: ['valid']}), /invalid edge reference/);
});
