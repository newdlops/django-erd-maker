import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';

const require = createRequire(import.meta.url);
const {createRelationshipExplorerTools} = require('../../out/webview/state/relationshipExplorer.js');
const {createLeafCards} = require('../../out/webview/state/createLeafCards.js');
const {getBrowserRelationshipSource} = require('../../out/webview/interaction/runtime/browserRelationshipSource.js');
const {getBrowserStateSource} = require('../../out/webview/interaction/runtime/browserStateSource.js');
const {getBrowserLeafCardSource} = require('../../out/webview/interaction/runtime/browserLeafCardSource.js');
const {getBrowserLayoutSource} = require('../../out/webview/interaction/runtime/browserLayoutSource.js');
const {getBrowserRenderSource} = require('../../out/webview/interaction/runtime/browserRenderSource.js');
const tools = createRelationshipExplorerTools();
const rel = (edgeId, sourceModelId, targetModelId, fieldName = edgeId, kind = 'foreign_key') =>
  ({edgeId, sourceModelId, targetModelId, fieldName, kind, direction: 'outgoing', otherModelId: targetModelId});

test('connection filters keep incoming, outgoing, inheritance and self references distinct', () => {
  const model = {modelId: 'a.Parent', modelName: 'Parent', relationships: [
    rel('out', 'a.Parent', 'b.Author', 'author'),
    {...rel('in', 'b.Post', 'a.Parent', 'owner'), direction: 'incoming', otherModelId: 'b.Post'},
    rel('base', 'a.Parent', 'b.Base', 'Base', 'inheritance'),
    {...rel('self', 'a.Parent', 'a.Parent', 'parent'), direction: 'self'},
  ]};
  assert.deepEqual(tools.results(model, {filter: 'outgoing'}).map(r => r.edgeId), ['out']);
  assert.deepEqual(tools.results(model, {filter: 'incoming'}).map(r => r.edgeId), ['in']);
  assert.deepEqual(tools.results(model, {filter: 'inheritance'}).map(r => r.edgeId), ['base']);
  assert.equal(tools.results(model).length, 4);
  assert.equal(tools.results(model, {query: ' OWNER '})[0].otherModelId, 'b.Post');
  assert.equal(tools.results(model, {query: 'b.author'})[0].edgeId, 'out');
  assert.match(tools.render(model), /Self-reference/);
});

test('dense lists paginate without losing duplicate field relationships or accepting HTML', () => {
  const relationships = Array.from({length: 93}, (_, index) => rel('edge-' + index, 'a.Parent', 'b.Child', 'field_' + index));
  relationships[0].fieldName = '<img src=x onerror="boom">';
  const model = {modelId: 'a.Parent', modelName: 'Parent', relationships};
  const html = tools.render(model, {query: '', revealedEdgeId: 'edge-0'});
  assert.equal((html.match(/data-preview-relationship/g) || []).length, 40);
  assert.match(html, /53 remaining/);
  assert.match(html, /&lt;img src=x onerror=&quot;boom&quot;&gt;/);
  assert.doesNotMatch(html, /<img/);
  assert.equal((tools.renderResults(model, {limit: 120}).match(/data-preview-relationship/g) || []).length, 93);
  assert.match(tools.renderResults(model, {query: 'missing'}), /No matching connections/);
  assert.match(tools.renderResults({...model, relationships: []}), /no declared relationships/);
});

function runtime() {
  const positions = {'a.Parent': {x: 0, y: 100}, 'b.A': {x: 500, y: 100}, 'b.B': {x: 500, y: 300}};
  const tables = Object.entries(positions).map(([modelId, position]) =>
    ({modelId, modelName: modelId, appLabel: modelId.split('.')[0], position, size: {width: 180, height: 120}}));
  const metas = new Map(tables.map(t => [t.modelId, {...t, basePosition: t.position, width: 180, height: 120}]));
  const relationships = [rel('primary', 'a.Parent', 'b.A'), rel('duplicate', 'a.Parent', 'b.A', 'secondary_owner'),
    rel('reverse', 'b.A', 'a.Parent'), rel('internal', 'b.A', 'b.B'), rel('self', 'a.Parent', 'a.Parent'),
    rel('missing', 'a.Parent', 'absent.Model')];
  const edges = [
    {edgeId: 'primary', sourceModelId: 'a.Parent', targetModelId: 'b.A', points: '180,160 500,160'},
    {edgeId: 'internal', sourceModelId: 'b.A', targetModelId: 'b.B', points: '590,220 590,300'},
  ];
  const cards = createLeafCards([{parentModelId: 'a.Parent', leafModelIds: ['b.A', 'b.B']}], tables);
  const scene = {tablesById: new Map(tables.map(t => [t.modelId, {modelId: t.modelId, ...t.position, width: 180, height: 120}]))};
  const initialState = {layoutMode: 'fmmm', tableOptions: [], collapseClusters: false, settings: {},
    selectedModelId: 'a.Parent', viewport: {zoom: 0.2, panX: 25, panY: 40}};
  const sandbox = {state: structuredClone(initialState), initialState, tableMetaById: metas,
    relationshipByEdgeId: new Map(relationships.map(r => [r.edgeId, r])), individualEdgeMeta: edges, edgeMeta: edges,
    revealedRelationshipEdgeId: '', juneOverviewExpanded: false, leafCardsVisible: true, drag: null,
    document: {querySelectorAll: () => [], querySelector: () => null},
    setSidebarSheet: () => {}, invalidateSceneGraph: () => {},
    layoutVariants: {fmmm: positions}, ensureSceneGraph: () => scene, cards, scene,
    isVisibleModel: id => !sandbox.state.tableOptions.find(o => o.modelId === id)?.hidden,
  };
  const api = vm.runInNewContext(getBrowserStateSource() + getBrowserLeafCardSource() + getBrowserLayoutSource()
    + getBrowserRelationshipSource() + getBrowserRenderSource() + `
      getViewportScreenRect = () => ({width: 1000, height: 600});
      applyState = () => { scene.relationshipPreview = undefined; buildConnectionPreviewRoutes(scene); };
      scheduleViewportRender = () => {};
      scene.leafBundles = createLeafCardRecords(cards, scene.tablesById, '');
      ({previewRelationship, clearConnectionPreview, goBackToModel, buildConnectionPreviewRoutes, dispatch,
        options: connectionOptions, setQuery: value => connectionQuery = value,
        setFilter: value => connectionFilter = value,
        snapshot: () => ({state, revealedRelationshipEdgeId, juneOverviewExpanded, leafCardsVisible, history: modelNavigationHistory}),
        routes: () => buildConnectionPreviewRoutes(scene)})`, sandbox);
  return {api, scene, sandbox, initialState};
}
const plain = value => JSON.parse(JSON.stringify(value));

test('preview resolves an unrendered duplicate to one straight line at the large card boundary', () => {
  const {api, scene, initialState} = runtime();
  assert.equal(api.routes(), undefined, 'overview routing must not be replaced outside preview');
  api.previewRelationship('duplicate');
  const routes = plain(api.routes()), snapshot = api.snapshot();
  assert.equal(routes.length, 1);
  assert.deepEqual(routes[0].meta.memberEdgeIds, ['duplicate']);
  assert.deepEqual(routes[0].points, [{x: 180, y: 160}, {x: 476, y: 160}]);
  assert.equal(snapshot.state.selectedModelId, initialState.selectedModelId);
  assert.equal(snapshot.juneOverviewExpanded, false);
  assert.equal(snapshot.leafCardsVisible, true);
  assert.equal(scene.relationshipPreview.grouped, true);
  api.clearConnectionPreview();
  assert.equal(api.routes(), undefined);
  assert.deepEqual(plain(api.snapshot().state.viewport), initialState.viewport);
});

test('reverse direction keeps the chosen source and target without fanning out leaf connections', () => {
  const {api} = runtime();
  api.previewRelationship('reverse');
  const [route] = plain(api.routes());
  assert.deepEqual(route.points, [{x: 476, y: 160}, {x: 180, y: 160}]);
  assert.equal(route.meta.sourceModelId, 'b.A');
  assert.equal(route.meta.targetModelId, 'a.Parent');
});

test('explicit navigation and Back restore the origin, preview, filters, and viewport', () => {
  const {api, initialState} = runtime();
  api.setQuery('secondary'); api.setFilter('outgoing');
  api.previewRelationship('duplicate');
  const before = plain(api.snapshot());
  api.dispatch({type: 'focus-model', modelId: 'b.A', zoom: 1});
  assert.equal(api.snapshot().state.selectedModelId, 'b.A');
  assert.equal(api.snapshot().revealedRelationshipEdgeId, '');
  assert.equal(api.options().query, '');
  api.goBackToModel();
  assert.deepEqual(plain(api.snapshot().state.viewport), before.state.viewport);
  assert.equal(api.snapshot().state.selectedModelId, 'a.Parent');
  assert.equal(api.snapshot().revealedRelationshipEdgeId, 'duplicate');
  assert.equal(api.options().query, 'secondary');
  assert.equal(api.options().filter, 'outgoing');
  api.clearConnectionPreview();
  assert.deepEqual(plain(api.snapshot().state.viewport), initialState.viewport);
});

test('internal, self, hidden and missing endpoints have explicit states and no misleading external line', () => {
  const {api, scene, sandbox} = runtime();
  for (const id of ['internal', 'self', 'missing']) {
    api.previewRelationship(id);
    assert.equal(api.routes().length, 0);
    assert.equal(scene.relationshipPreview.status, id);
  }
  sandbox.state.tableOptions.push({modelId: 'b.A', hidden: true});
  api.previewRelationship('primary');
  assert.equal(api.routes().length, 0);
  assert.equal(scene.relationshipPreview.status, 'hidden');
});

test('clearing selection restores the overview viewport and reset drops transient navigation', () => {
  const {api, initialState} = runtime();
  api.previewRelationship('duplicate');
  api.dispatch({type: 'clear-selection'});
  assert.equal(api.routes(), undefined);
  assert.deepEqual(plain(api.snapshot().state.viewport), initialState.viewport);
  api.dispatch({type: 'focus-model', modelId: 'b.A', zoom: 1});
  api.dispatch({type: 'focus-model', modelId: 'a.Parent', zoom: 1});
  assert.ok(api.snapshot().history.length);
  api.dispatch({type: 'reset-view', initialState});
  assert.equal(api.snapshot().history.length, 0);
  assert.equal(api.options().query, '');
});
