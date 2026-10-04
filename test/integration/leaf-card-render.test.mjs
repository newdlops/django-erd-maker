import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';

const require = createRequire(import.meta.url);
const {createLeafCards} = require('../../out/webview/state/createLeafCards.js');
const {createLeafCardConnectionTools} = require('../../out/webview/state/leafCardConnections.js');
const {createDiagramRenderModel, measureRenderedVisualConflicts} = require('../../out/webview/state/createDiagramRenderModel.js');
const {renderCanvasScene} = require('../../out/webview/render/renderCanvasScene.js');
const {getBrowserLeafCardSource} = require('../../out/webview/interaction/runtime/browserLeafCardSource.js');
const {getBrowserCanvasDrawSource} = require('../../out/webview/interaction/runtime/browserCanvasDrawSource.js');
const {getBrowserLayoutSource} = require('../../out/webview/interaction/runtime/browserLayoutSource.js');
const {getBrowserEventSource} = require('../../out/webview/interaction/runtime/browserEventSource.js');
const {createDiagramInteractionState, reduceDiagramInteractionState} = require('../../out/webview/state/diagramInteractionState.js');

function fixture() {
  const node = (id, x, y) => ({modelId: id, modelName: id, appLabel: 'test', position: {x, y}, size: {width: 180, height: 120}});
  const tables = [node('Parent', 0, 100), node('A', 500, 100), node('B', 500, 300), node('C', 760, 100), node('Extra', 1200, 100)];
  const bundle = {parentModelId: 'Parent', leafModelIds: ['A', 'B', 'C'], sharedRootModelIds: ['Parent', 'Extra'],
    anchor: {x: 180, y: 160}, bbox: {x: -9999, y: -9999, width: 90000, height: 90000}};
  return {tables, bundles: [bundle]};
}

function payloadFixture() {
  const {tables, bundles} = fixture();
  const edges = [['pa', 'Parent', 'A'], ['pb', 'Parent', 'B'], ['pc', 'Parent', 'C'], ['eb', 'Extra', 'B']]
    .map(([id, sourceModelId, targetModelId]) => ({id, sourceModelId, targetModelId, kind: 'foreign_key', provenance: 'declared'}));
  return {
    analyzer: {diagnostics: [], models: tables.map(n => ({declaredBaseClasses: [], fields: [], methods: [], properties: [],
      identity: {id: n.modelId, modelName: n.modelName, appLabel: n.appLabel}})), summary: {}},
    contractVersion: 'test', graph: {diagnostics: [], methodAssociations: [], nodes: tables, structuralEdges: edges},
    layout: {mode: 'fmmm', crossings: [], nodes: tables, engineMetadata: {leafBundles: bundles},
      routedEdges: edges.map(e => ({edgeId: e.id, crossingIds: [], points: [{x: 10, y: 10}, {x: 600, y: 200}]}))},
    layoutExecution: {appliedMode: 'fmmm', engine: 'ogdf', requestedMode: 'fmmm', status: 'applied'},
    view: {layoutMode: 'fmmm', tableOptions: []},
  };
}

function records(tables) {
  return tables.map(node => ({modelId: node.modelId, x: node.position.x, y: node.position.y,
    width: node.size.width, height: node.size.height, meta: {modelId: node.modelId, clusterId: ['A', 'B', 'C'].includes(node.modelId) ? 'leaves' : ''}}));
}

test('leaf cards preserve every model and relationship while sharing their external endpoints', () => {
  const payload = payloadFixture();
  const before = structuredClone(payload);
  before.layout.engineMetadata.leafBundles = [];
  const normal = createDiagramRenderModel(before), grouped = createDiagramRenderModel(payload);
  assert.equal(grouped.leafCards.length, 1);
  assert.deepEqual(grouped.leafCards[0].memberModelIds, ['A', 'B', 'C']);
  assert.deepEqual(grouped.tables, normal.tables);
  assert.deepEqual(grouped.leafCardOverview.individualEdges, normal.edges);
  assert.equal(grouped.edges.length, 2);
  assert.deepEqual(new Set(grouped.edges.flatMap(edge => edge.memberEdgeIds)), new Set(normal.edges.map(edge => edge.edgeId)));
  assert.ok(grouped.edges.every(edge => edge.leafCardEndpointIds.includes('leaf-card:0')));
  assert.deepEqual(grouped.bundleLeavesByFakeId, {});
  const html = renderCanvasScene(grouped, 'test');
  assert.match(html, /data-leaf-cards-toggle aria-pressed="true"/);
  assert.match(html, /Leaf cards \(1\)/);
  assert.match(html, /aria-label="Find a leaf card"/);
  assert.match(html, /Parent · 3 leaves/);
});

function straightEdges() {
  return [
    ['pa', 'Parent', 'A', '180,160 500,160'],
    ['pb', 'Parent', 'B', '180,220 500,360'],
    ['pc', 'C', 'Parent', '760,160 180,160'],
    ['eb', 'Extra', 'B', '1200,160 680,360'],
    ['ab', 'A', 'B', '590,220 590,300'],
  ].map(([edgeId, sourceModelId, targetModelId, points]) => ({edgeId, sourceModelId, targetModelId, points,
    markerStartId: '', markerEndId: '', crossingIds: [], cssKind: 'foreign-key', provenance: 'declared', preserveRouteEndpoints: true}));
}

test('one straight line reaches the group boundary per peer, with opposite directions and internal relations retained', () => {
  const {tables, bundles} = fixture(), tools = createLeafCardConnectionTools(), edges = straightEdges();
  const bounds = tools.bounds(createLeafCards(bundles, tables), new Map(records(tables).map(n => [n.modelId, n])));
  const projection = tools.project(edges, edges, bounds);
  assert.equal(projection.edges.length, 2);
  assert.deepEqual(projection.internalEdgeIds, ['ab']);
  const parent = projection.edges.find(edge => edge.logicalEndpointModelIds.includes('Parent'));
  assert.deepEqual(parent.memberEdgeIds, ['pa', 'pb', 'pc']);
  assert.equal(parent.points, '180,160 476,160');
  assert.deepEqual(parent.physicalEndpointModelIds, ['Parent']);
  assert.deepEqual(parent.leafCardEndpointIds, [null, 'leaf-card:0']);
  assert.equal(projection.edges.flatMap(edge => edge.memberEdgeIds).length + projection.internalEdgeIds.length, edges.length);
  assert.deepEqual(tools.project(edges, edges, []).edges, edges);
});

test('projecting a leaf card preserves the existing June relation grouping', () => {
  const {tables, bundles} = fixture(), tools = createLeafCardConnectionTools(), individual = straightEdges();
  const bounds = tools.bounds(createLeafCards(bundles, tables), new Map(records(tables).map(n => [n.modelId, n])));
  const existingGroup = {...individual[0], edgeId: 'saved-june-group', memberEdgeIds: ['pa', 'pb', 'pc', 'eb'],
    logicalEndpointModelIds: ['Parent', 'A', 'B', 'C', 'Extra']};
  const projected = tools.project([existingGroup, individual[4]], individual, bounds);
  assert.equal(projected.edges.length, 1);
  assert.deepEqual(projected.edges[0].memberEdgeIds, existingGroup.memberEdgeIds);
  assert.ok(projected.edges[0].logicalEndpointModelIds.includes('Extra'));
  assert.deepEqual(projected.internalEdgeIds, ['ab']);
});

test('server and GPU collision audits count the whole large card, including gaps between its members', () => {
  const payload = payloadFixture();
  for (const node of payload.layout.nodes) if (['Parent', 'Extra'].includes(node.modelId)) node.position.y = 200;
  const scene = createDiagramRenderModel(payload);
  const probe = {...straightEdges()[0], edgeId: 'outside', targetModelId: 'Extra', points: '180,260 1200,260'};
  const grouped = measureRenderedVisualConflicts({...scene, edges: [probe]});
  const ungrouped = measureRenderedVisualConflicts({...scene, edges: [probe], leafCardOverview: undefined});
  assert.equal(grouped.bundleEdgeIntersections, 1);
  assert.equal(grouped.edgeNodeIntersections, 0);
  assert.equal(ungrouped.visualCrossings, 0);
  const tables = records(payload.layout.nodes), tablesById = new Map(tables.map(table => [table.modelId, table]));
  const runtime = vm.runInNewContext(getBrowserLeafCardSource() + getBrowserCanvasDrawSource()
    + ';({createLeafCardRecords,addToBuckets,collectEdgePathCollisions})', {performance, state: {viewport: {zoom: 1}}});
  const gpuScene = {tables, tablesById, tableBuckets: new Map(),
    leafBundles: runtime.createLeafCardRecords(scene.leafCards, tablesById, ''), leafCardMemberIds: new Set(['A', 'B', 'C'])};
  for (const table of tables) runtime.addToBuckets(gpuScene.tableBuckets,
    {left: table.x, top: table.y, right: table.x + table.width, bottom: table.y + table.height}, table.modelId);
  const collisions = runtime.collectEdgePathCollisions([{x: 180, y: 260}, {x: 1200, y: 260}], probe, gpuScene);
  assert.equal(collisions.length, 1);
  assert.equal(collisions[0].table.modelId, 'leaf-card:0');
});

test('GPU projection keeps one connection when the group moves or its representative leaf is hidden', () => {
  const {tables, bundles} = fixture(), individual = straightEdges();
  const metas = new Map(tables.map(table => [table.modelId, {basePosition: table.position, width: table.size.width, height: table.size.height}]));
  const positions = new Map(tables.map(table => [table.modelId, {...table.position}]));
  const hidden = new Set();
  const context = {individualEdgeMeta: individual, edgeMeta: individual, getActiveEdgeMeta: () => individual,
    tableMetaById: metas, isVisibleModel: id => !hidden.has(id), lookupPosition: id => positions.get(id)};
  const runtime = vm.runInNewContext(getBrowserLeafCardSource() + getBrowserLayoutSource()
    + ';getCurrentPosition=lookupPosition;({createLeafCardRecords,projectLeafCardEdges,getStaticOrCatalogEdgePaths})', context);
  const run = () => {
    const current = new Map(records(tables).filter(table => !hidden.has(table.modelId)).map(table => [table.modelId, {...table, ...positions.get(table.modelId)}]));
    const scene = {leafBundles: runtime.createLeafCardRecords(createLeafCards(bundles, tables), current, ''), tablesById: current};
    const entries = individual.filter(edge => !hidden.has(edge.sourceModelId) && !hidden.has(edge.targetModelId)).map(meta => ({meta,
      sourceTable: metas.get(meta.sourceModelId), targetTable: metas.get(meta.targetModelId),
      sourcePosition: positions.get(meta.sourceModelId), targetPosition: positions.get(meta.targetModelId)}));
    return runtime.projectLeafCardEdges(scene, runtime.getStaticOrCatalogEdgePaths(entries));
  };
  assert.equal(run().length, 2);
  for (const id of ['A', 'B', 'C']) positions.get(id).x += 100;
  const moved = run();
  assert.equal(moved.length, 2);
  const parent = moved.find(route => route.meta.logicalEndpointModelIds.includes('Parent'));
  assert.equal(parent.points[1].x, 576);
  hidden.add('A');
  const filtered = run();
  assert.equal(filtered.length, 2);
  assert.ok(filtered.flatMap(route => route.meta.memberEdgeIds).includes('pb'));
  assert.ok(!filtered.flatMap(route => route.meta.memberEdgeIds).includes('pa'));
});

test('invalid, ambiguous, dispersed and occluding memberships do not create huge cards', () => {
  const {tables, bundles} = fixture();
  assert.equal(createLeafCards(bundles, tables).length, 1); // Stale metadata bbox is ignored.
  assert.equal(createLeafCards([...bundles, structuredClone(bundles[0])], tables).length, 0);
  assert.equal(createLeafCards([{...bundles[0], leafModelIds: ['A', 'missing']}], tables).length, 0);
  assert.equal(createLeafCards([{...bundles[0], leafModelIds: ['A', 'Parent']}], tables).length, 0);
  const dispersed = structuredClone(tables);
  dispersed.find(n => n.modelId === 'B').position.y = 50000;
  assert.equal(createLeafCards(bundles, dispersed).length, 0);
  const occluding = structuredClone(tables);
  occluding.find(n => n.modelId === 'Extra').position = {x: 740, y: 300};
  assert.equal(createLeafCards(bundles, occluding).length, 0);
  assert.match(renderCanvasScene(createDiagramRenderModel({...payloadFixture(), layout: {...payloadFixture().layout, engineMetadata: {}}}), 'test'), /data-leaf-cards-toggle aria-pressed="false" disabled/);
});

test('runtime cards follow current member positions, hidden members and selection', () => {
  const {tables, bundles} = fixture();
  const cards = createLeafCards(bundles, tables);
  const runtime = vm.runInNewContext(getBrowserLeafCardSource() + ';({createLeafCardRecords})');
  const source = records(tables), byId = new Map(source.map(n => [n.modelId, n]));
  const original = runtime.createLeafCardRecords(cards, byId, 'A')[0];
  assert.equal(original.x, 476);
  assert.equal(original.y, 76);
  assert.equal(original.width, 488);
  assert.equal(original.height, 368);
  assert.equal(original.selected, true);
  for (const id of cards[0].memberModelIds) {byId.get(id).x += 200; byId.get(id).y += 100;}
  const moved = runtime.createLeafCardRecords(cards, byId, '')[0];
  assert.equal(moved.x - original.x, 200);
  assert.equal(moved.y - original.y, 100);
  assert.equal(moved.width, original.width);
  byId.get('B').y += 50000;
  assert.equal(runtime.createLeafCardRecords(cards, byId, '').length, 0);
  byId.get('B').y -= 50000;
  byId.delete('B');
  assert.equal(runtime.createLeafCardRecords(cards, byId, '')[0].leafCount, 2);
  byId.delete('A'); byId.delete('C');
  assert.equal(runtime.createLeafCardRecords(cards, byId, '').length, 0);
});

function leafHighlightRuntime() {
  const {tables, bundles} = fixture();
  const members = records(tables);
  for (const member of members) {
    member.meta.appLabel = 'test';
    if (!member.meta.clusterId) member.meta.clusterId = 'core';
  }
  const byId = new Map(members.map(member => [member.modelId, member]));
  const context = {performance, MIN_VIEWPORT_ZOOM: 0.04, state: {viewport: {zoom: 1}},
    tableMetaById: new Map(members.map(member => [member.modelId, member.meta])),
    relationshipsByModelId: new Map(), drag: null, isMethodTarget: () => false};
  const api = vm.runInNewContext(getBrowserLeafCardSource() + getBrowserCanvasDrawSource()
    + ';({createLeafCardRecords, leafBundleColors, tableColors, appendLeafBundleLabels, fillLeafBundleInstanceData, hover: id => hoveredLeafCardId = id})', context);
  const cards = createLeafCards(bundles, tables);
  const card = () => api.createLeafCardRecords(cards, byId, context.state.selectedModelId)[0];
  const labels = record => {const result = []; api.appendLeafBundleLabels(result, [record]); return result;};
  return {api, context, card, byId, labels};
}

test('unrelated leaf card surfaces, borders and titles dim with their members and recover when selection clears', () => {
  const {api, context, card, byId, labels} = leafHighlightRuntime();
  const original = card(), normal = api.leafBundleColors(original), normalTitle = labels(original)[0].color;
  context.state.selectedModelId = 'Parent';
  const dimmed = api.leafBundleColors(card()), member = api.tableColors(byId.get('A'));
  assert.deepEqual(dimmed.fill, member.fill);
  assert.equal(dimmed.stroke[3], member.stroke[3]);
  assert.ok(dimmed.stroke[3] < normal.stroke[3]);
  assert.notEqual(labels(card())[0].color, normalTitle);
  api.hover(original.id);
  assert.deepEqual(api.leafBundleColors(card()).fill, dimmed.fill);
  assert.deepEqual(api.leafBundleColors(card()).stroke, dimmed.stroke);
  const data = new Float32Array(14);
  api.fillLeafBundleInstanceData(data, [card()]);
  assert.deepEqual(Array.from(data.slice(0, 4)), [original.x, original.y, original.width, original.height]);
  assert.ok(Math.abs(data[7] - member.fill[3]) < 1e-6);
  assert.ok(Math.abs(data[11] - member.stroke[3]) < 1e-6);
  context.state.selectedModelId = undefined; api.hover('');
  assert.deepEqual(api.leafBundleColors(card()), normal);
  assert.equal(labels(card())[0].color, normalTitle);
});

test('leaf card emphasis follows any member relationship, selected cluster and preview endpoints', () => {
  const {api, context, card, byId, labels} = leafHighlightRuntime();
  const normalTitle = labels(card())[0].color;
  context.state.selectedModelId = 'Parent';
  context.relationshipsByModelId.set('Parent', [{otherModelId: 'C', kind: 'foreign_key'}]);
  assert.deepEqual(api.leafBundleColors(card()).stroke, api.tableColors(byId.get('C')).stroke);
  assert.ok(api.leafBundleColors(card()).stroke[3] > 0.9);
  context.state.selectedModelId = 'A';
  assert.deepEqual(api.leafBundleColors(card()).fill, api.tableColors(byId.get('A')).fill);
  assert.deepEqual(api.leafBundleColors(card()).stroke, api.tableColors(byId.get('A')).stroke);
  assert.equal(labels(card())[0].color, normalTitle);
  context.state.selectedModelId = 'Extra'; context.tableMetaById.get('Extra').clusterId = 'leaves';
  assert.deepEqual(api.leafBundleColors(card()).fill, api.tableColors(byId.get('B')).fill);
  assert.equal(labels(card())[0].color, normalTitle);
  context.state.selectedModelId = 'Parent';
  context.getPreviewRelationship = () => ({edgeId: 'pc'});
  context.isConnectionEndpoint = id => id === 'C' || id === 'Parent';
  assert.equal(api.leafBundleColors(card()).fill[3], 0.96);
  assert.equal(labels(card())[0].color, normalTitle);
  context.isConnectionEndpoint = id => id === 'Parent';
  assert.equal(api.leafBundleColors(card()).fill[3], 0.18);
});

test('leaf containers and the selected cluster have distinct spatial bucket entries', () => {
  const {tables, bundles} = fixture();
  const tableRecords = records(tables);
  const scene = {tables: tableRecords, tablesById: new Map(tableRecords.map(n => [n.modelId, n]))};
  const context = {performance, renderModel: {leafCards: createLeafCards(bundles, tables), clusterOutlines: []},
    state: {collapseClusters: false, selectedModelId: 'A'}};
  const runtime = vm.runInNewContext(getBrowserLeafCardSource() + getBrowserCanvasDrawSource() + ';({buildClusterOutlineRecords})', context);
  runtime.buildClusterOutlineRecords(scene);
  assert.equal(scene.leafBundles.length, 2);
  assert.equal(scene.leafBundles[0].kind, 'leaf-card');
  assert.equal(scene.leafBundles[1].kind, 'cluster-outline');
  assert.deepEqual(new Set([...scene.leafBundleBuckets.values()].flat()), new Set([0, 1]));
});

test('live group dragging translates every member and preserves other positions', () => {
  const context = {drag: {kind: 'leaf-card', currentPosition: {x: 300, y: 250}, startPosition: {x: 100, y: 100},
    memberPositions: {A: {x: 500, y: 100}, B: {x: 500, y: 300}}},
    state: {layoutMode: 'fmmm'}, getTableOptions: () => ({}), layoutVariants: {hierarchical: {Parent: {x: 0, y: 100}}}};
  const runtime = vm.runInNewContext(getBrowserLayoutSource() + ';({getCurrentPosition})', context);
  assert.equal(runtime.getCurrentPosition('A').x, 700);
  assert.equal(runtime.getCurrentPosition('B').y, 450);
  assert.equal(runtime.getCurrentPosition('Parent').x, 0);
  context.drag = null;
  context.layoutVariants.hierarchical.A = {x: 500, y: 100};
  assert.equal(runtime.getCurrentPosition('A').x, 500);
});

test('leaf controls toggle, reveal a collapsed group and move its members together by keyboard', () => {
  const {tables, bundles} = fixture();
  const leafCards = createLeafCards(bundles, tables);
  const byId = new Map(records(tables).map(node => [node.modelId, node]));
  const toggleEvents = {}, selectEvents = {}, attributes = {}, status = {};
  let renders = 0, collapseClassRemoved = false;
  const toggle = {classList: {toggle() {}}, setAttribute(key, value) {attributes[key] = value;},
    addEventListener(name, handler) {toggleEvents[name] = handler;}};
  const select = {value: leafCards[0].id, addEventListener(name, handler) {selectEvents[name] = handler;}};
  const collapse = {classList: {remove() {collapseClassRemoved = true;}}};
  const context = {
    state: createDiagramInteractionState({layoutMode: 'fmmm', collapseClusters: true, tableOptions: [{modelId: 'B', hidden: true}]}),
    renderModel: {leafCards}, reduceState: reduceDiagramInteractionState,
    document: {
      querySelectorAll(selector) {return selector === '[data-leaf-cards-toggle]' ? [toggle]
        : selector === '[data-leaf-card-select]' ? [select] : selector === '[data-cluster-collapse-toggle]' ? [collapse] : [];},
      querySelector() {return status;},
    },
    invalidateSceneGraph() {}, applyState() {renders++;},
    round2: value => Math.round(value * 100) / 100, clampZoom: value => value,
    getViewportScreenRect: () => ({width: 800, height: 600}),
    getCurrentPosition(id) {return context.state.tableOptions.find(option => option.modelId === id)?.manualPosition
      || tables.find(node => node.modelId === id).position;},
    dispatch(action) {context.state = reduceDiagramInteractionState(context.state, action);},
  };
  let runtime;
  context.ensureSceneGraph = () => ({leafBundles: context.state.collapseClusters ? []
    : runtime.createLeafCardRecords(leafCards, byId, context.state.selectedModelId)});
  const eventSource = getBrowserEventSource();
  const start = eventSource.indexOf('for (const button of document.querySelectorAll("[data-leaf-cards-toggle]"))');
  const handlers = eventSource.slice(start, eventSource.indexOf('for (const button of document.querySelectorAll("[data-cluster-collapse-toggle]"))', start));
  runtime = vm.runInNewContext(getBrowserLeafCardSource() + handlers + ';({createLeafCardRecords})', context);
  toggleEvents.click();
  assert.equal(attributes['aria-pressed'], 'false');
  selectEvents.change();
  assert.equal(attributes['aria-pressed'], 'true');
  assert.equal(context.state.collapseClusters, false);
  assert.equal(collapseClassRemoved, true);
  assert.equal(context.state.selectedModelId, 'Parent');
  const frame = runtime.createLeafCardRecords(leafCards, byId, 'Parent')[0];
  const viewport = context.state.viewport;
  assert.equal(viewport.panX + (frame.x + frame.width / 2) * viewport.zoom, 400);
  assert.equal(viewport.panY + (frame.y + frame.height / 2) * viewport.zoom, 300);
  assert.match(status.textContent, /Parent: 3 leaf models/);
  const beforeRenders = renders;
  let prevented = false;
  selectEvents.keydown({shiftKey: true, key: 'ArrowRight', preventDefault() {prevented = true;}, stopPropagation() {}});
  assert.equal(prevented, true);
  assert.equal(renders - beforeRenders, 1);
  for (const id of ['A', 'B', 'C']) assert.equal(context.getCurrentPosition(id).x, tables.find(node => node.modelId === id).position.x + 10);
  assert.equal(context.getCurrentPosition('Parent').x, 0);
  assert.equal(context.state.tableOptions.find(option => option.modelId === 'B').hidden, true);
});
