import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';
const require = createRequire(import.meta.url);
const {createSelectedRelationshipTools} = require('../../out/webview/state/selectedRelationshipEdges.js');
const {createJuneRelationshipOverview} = require('../../out/webview/state/createJuneRelationshipOverview.js');
const {getBrowserControllerScript} = require('../../out/webview/interaction/browserController.js');
const {getBrowserLeafCardSource} = require('../../out/webview/interaction/runtime/browserLeafCardSource.js');
const {getBrowserLayoutSource} = require('../../out/webview/interaction/runtime/browserLayoutSource.js');
const plain = value => JSON.parse(JSON.stringify(value));
const members = edge => edge.memberEdgeIds?.length ? edge.memberEdgeIds : [edge.edgeId];
const tools = createSelectedRelationshipTools();

function fixture() {
  const nodes = [['S', 0, 100], ['A', 500, 100], ['B', 500, 300], ['D', 1100, 100], ['E', 1320, 100]]
    .map(([modelId, x, y]) => ({modelId, x, y, width: 180, height: 120}));
  const edge = (edgeId, sourceModelId, targetModelId, points) => ({edgeId, sourceModelId, targetModelId, points,
    preserveRouteEndpoints: true, markerStartId: '', markerEndId: '', crossingIds: [], provenance: 'declared', cssKind: 'foreign-key'});
  const individual = [edge('sa', 'S', 'A', '180,160 500,160'), edge('as', 'A', 'S', '500,180 180,180'),
    edge('sb', 'S', 'B', '180,210 500,340'), edge('de', 'D', 'E', '1280,160 1320,160'), edge('ab', 'A', 'B', '590,220 590,300')];
  const base = createJuneRelationshipOverview(individual, [{carrierId: 'overview:all', memberEdgeIds: individual.map(e => e.edgeId), points: []}]).edges;
  const cards = [{id: 'leaf-card:test', label: 'S', appLabel: 'app', parentModelId: 'S', memberModelIds: ['A', 'B'], padding: 24}];
  return {nodes, individual, base, cards};
}

test('selecting a model restores real endpoint pairs and removes its IDs from unrelated representatives', () => {
  const {base, individual} = fixture();
  assert.equal(base[0].sourceModelId, 'D'); // The former highlighted route never touched S.
  const focused = tools.focus(base, individual, 'S');
  const direct = focused.filter(e => e.carrierRole === 'direct');
  assert.equal(direct.length, 2);
  assert.ok(direct.every(e => e.physicalEndpointModelIds.includes('S')));
  assert.deepEqual(new Set(direct.flatMap(members)), new Set(['sa', 'as', 'sb']));
  assert.deepEqual(new Set(focused.filter(e => e.carrierRole !== 'direct').flatMap(members)), new Set(['de', 'ab']));
  assert.deepEqual(new Set(focused.flatMap(members)), new Set(individual.map(e => e.edgeId)));
  assert.equal(focused.flatMap(members).length, individual.length);
  assert.equal(direct.find(e => members(e).includes('sa')).memberEdgeIds.length, 2);
  assert.deepEqual(base[0].memberEdgeIds.length, individual.length);
});

test('the remaining overview gets a valid new representative when the selected relationship was shortest', () => {
  const {base, individual} = fixture();
  const focused = tools.focus(base, individual, 'D');
  const remainder = focused.find(e => e.carrierRole === 'overview');
  assert.ok(!remainder.logicalEndpointModelIds.includes('D'));
  assert.ok(!remainder.physicalEndpointModelIds.includes('D'));
  const representative = individual.find(e => e.sourceModelId === remainder.sourceModelId && e.targetModelId === remainder.targetModelId);
  assert.equal(remainder.points, representative.points);
  assert.strictEqual(tools.focus(base, individual, undefined), base);
  assert.strictEqual(tools.focus(base, individual, 'missing'), base);
});

function runtime() {
  const data = fixture(), positions = new Map(data.nodes.map(n => [n.modelId, {x: n.x, y: n.y}])), hidden = new Set();
  const meta = new Map(data.nodes.map(n => [n.modelId, {basePosition: {x: n.x, y: n.y}, width: n.width, height: n.height}]));
  const context = {state: {selectedModelId: 'S'}, tableMetaById: meta, lookupPosition: id => positions.get(id),
    isVisibleModel: id => !hidden.has(id), renderModel: {leafCards: data.cards,
      leafCardOverview: {baseEdges: data.base, individualEdges: data.individual}}};
  const controller = getBrowserControllerScript('test');
  const selection = controller.slice(controller.indexOf('const readEdgeMeta ='), controller.indexOf('const tableMetaList ='));
  const api = vm.runInNewContext(selection + getBrowserLeafCardSource() + getBrowserLayoutSource()
    + '; getCurrentPosition = lookupPosition; ({getActiveEdgeMeta,createLeafCardRecords,projectLeafCardEdges,getStaticOrCatalogEdgePaths})', context);
  const run = (showCards = true) => {
    const current = new Map(data.nodes.filter(n => !hidden.has(n.modelId)).map(n => [n.modelId, {...n, ...positions.get(n.modelId)}]));
    const scene = {tablesById: current, leafBundles: showCards ? api.createLeafCardRecords(data.cards, current, context.state.selectedModelId) : []};
    const entries = api.getActiveEdgeMeta().filter(e => !hidden.has(e.sourceModelId) && !hidden.has(e.targetModelId)).map(edge => ({meta: edge,
      sourceTable: meta.get(edge.sourceModelId), targetTable: meta.get(edge.targetModelId),
      sourcePosition: positions.get(edge.sourceModelId), targetPosition: positions.get(edge.targetModelId)}));
    return {scene, routes: plain(api.projectLeafCardEdges(scene, api.getStaticOrCatalogEdgePaths(entries)))};
  };
  return {...data, context, api, positions, hidden, run};
}

test('actual browser selection and Leaf projection keep one boundary line with live positions and hidden members', () => {
  const {api, context, positions, hidden, run, base} = runtime();
  const selectedRoute = result => result.routes.find(route => members(route.meta).includes('sb'));
  const pointAt = (route, id) => route.points[[route.meta.leafCardEndpointIds?.[0] || route.meta.sourceModelId,
    route.meta.leafCardEndpointIds?.[1] || route.meta.targetModelId].indexOf(id)];
  const first = run(), edge = selectedRoute(first);
  assert.deepEqual(new Set(members(edge.meta)), new Set(['sa', 'as', 'sb']));
  assert.ok(edge.meta.leafCardEndpointIds.includes('leaf-card:test'));
  assert.equal(pointAt(edge, 'S').x, 180); assert.equal(pointAt(edge, 'leaf-card:test').x, 476);
  positions.get('S').x -= 100;
  assert.equal(pointAt(selectedRoute(run()), 'S').x, 80);
  hidden.add('A');
  const visible = selectedRoute(run());
  assert.deepEqual(members(visible.meta), ['sb']);
  assert.equal(pointAt(visible, 'leaf-card:test').x, 476);
  hidden.clear(); positions.get('S').x += 100;
  assert.equal(run(false).routes.filter(r => r.meta.carrierRole === 'direct').length, 2);
  context.state.selectedModelId = undefined;
  assert.deepEqual(plain(api.getActiveEdgeMeta()).map(e => [e.edgeId, e.points, e.memberEdgeIds]), base.map(e => [e.edgeId, e.points, e.memberEdgeIds]));
  context.state.selectedModelId = 'S';
  assert.deepEqual(selectedRoute(run()).points, edge.points);
});

test('selecting a Leaf member keeps external connections at its large card and internal relationships in the group', () => {
  const {context, run, individual} = runtime();
  context.state.selectedModelId = 'A';
  const result = run();
  const edge = result.routes.find(route => members(route.meta).includes('sa'));
  assert.ok(edge.meta.leafCardEndpointIds.includes('leaf-card:test'));
  assert.deepEqual(plain(result.scene.leafCardInternalEdgeIds), ['ab']);
  const coverage = [...result.routes.flatMap(route => members(route.meta)), ...result.scene.leafCardInternalEdgeIds];
  assert.equal(coverage.length, individual.length);
  assert.deepEqual(new Set(coverage), new Set(individual.map(e => e.edgeId)));
});
