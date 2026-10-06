import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';

const require = createRequire(import.meta.url);
const {createCircularDiagramTools} = require('../../out/webview/state/circularDiagram.js');
const {getBrowserCircularDiagramSource} = require('../../out/webview/interaction/runtime/browserCircularDiagramSource.js');
const {getBrowserStateSource} = require('../../out/webview/interaction/runtime/browserStateSource.js');
const {getBrowserRenderSource} = require('../../out/webview/interaction/runtime/browserRenderSource.js');
const {getBrowserControllerScript} = require('../../out/webview/interaction/browserController.js');
const tools = createCircularDiagramTools();
const model = (id, relationships = []) => ({modelId: id, modelName: id.split('.').at(-1), appLabel: id.split('.')[0], databaseTableName: id.replace('.', '_'), relationships});
const relation = (id, source, target) => ({edgeId: id, sourceModelId: source, targetModelId: target, fieldName: id, kind: 'foreign_key', direction: source === target ? 'self' : 'outgoing', otherModelId: target});

test('the entire catalog retains isolated/hidden models and every independent declared connection', () => {
  const edges = [relation('first', 'app.A', 'app.B'), relation('parallel', 'app.A', 'app.B'),
    relation('reverse', 'app.B', 'app.A'), relation('self', 'app.A', 'app.A'), relation('missing', 'app.A', 'external.Missing')];
  const models = [model('app.A', edges), model('app.B', edges.slice(0, 3)), {...model('other.Isolated'), hidden: true}];
  const before = structuredClone(models), graph = tools.build(models);
  assert.deepEqual(models, before);
  assert.deepEqual(graph.nodes.map(n => n.modelId), ['app.A', 'app.B', 'other.Isolated']);
  assert.equal(graph.relationshipCount, 5); assert.equal(graph.edges.length, 4); assert.equal(graph.unavailableCount, 1);
  assert.equal(graph.nodes[2].degree, 0);
  assert.equal(new Set(graph.edges.slice(0, 3).map(e => JSON.stringify(e.points))).size, 3);
  const self = graph.edges.find(e => e.edgeId === 'self');
  assert.deepEqual(self.points[0], self.points[3]);
  assert.notDeepEqual(self.points[0], self.points[1]);
});

test('1,247 models have stable unique finite positions with complete app membership', () => {
  const models = Array.from({length: 1247}, (_, i) => model('app' + i % 17 + '.Model' + i));
  const graph = tools.build(models), reversed = tools.build(models.slice().reverse());
  assert.deepEqual(graph, reversed);
  assert.equal(graph.nodes.length, 1247);
  assert.equal(new Set(graph.nodes.map(n => n.x + ',' + n.y)).size, 1247);
  assert.equal(graph.apps.reduce((sum, a) => sum + a.modelCount, 0), 1247);
  for (const node of graph.nodes) assert.ok(Number.isFinite(node.x) && Number.isFinite(node.y));
  assert.deepEqual(tools.build([]).nodes, []);
});

test('curve hit testing resolves a declared field and ignores distant clicks', () => {
  const edge = tools.build([model('app.A', [relation('field', 'app.A', 'app.B')]), model('app.B')]).edges[0];
  assert.equal(tools.hitEdge([edge], tools.point(edge.points, 0.25), 1)?.edgeId, 'field');
  assert.equal(tools.hitEdge([edge], {x: 100000, y: -100000}, 7), undefined);
});

test('circular search, selection, connection preview and exit preserve the original ERD geometry', () => {
  const edge = relation('field', 'app.A', 'app.B');
  const models = new Map([model('app.A', [edge]), model('app.B')].map(m => [m.modelId, m]));
  const initial = {selectedModelId: 'app.A', layoutMode: 'fmmm', settings: {}, viewport: {zoom: 0.23, panX: -3456, panY: 789},
    tableOptions: [{modelId: 'app.A', manualPosition: {x: 3000, y: 5000}}]};
  const picker = {innerHTML: ''}, host = {querySelector: () => picker};
  const sandbox = {state: structuredClone(initial), inspectorModelById: models, relationshipByEdgeId: new Map([[edge.edgeId, edge]]),
    revealedRelationshipEdgeId: '', connectionOverviewViewport: null,
    document: {querySelector: selector => selector === '[data-circular-diagram]' ? host : null, querySelectorAll: () => []},
    window: {cancelAnimationFrame() {}}, setSidebarSheet() {}, invalidateSceneGraph() {},
    isRelatedDiagramOpen: () => false, relationshipExplorer: {escape: value => value}};
  const api = vm.runInNewContext(getBrowserStateSource() + getBrowserCircularDiagramSource() + getBrowserRenderSource() + `
    applyState = () => {}; scheduleViewportRender = () => {};
    ({openCircularDiagram, closeCircularDiagram, selectCircularModel, selectCircularEdge, dispatch,
      snapshot: () => ({state, circularOpen, revealedRelationshipEdgeId})})`, sandbox);
  api.openCircularDiagram(); assert.equal(api.snapshot().circularOpen, true);
  api.dispatch({type: 'focus-model', modelId: 'app.B', zoom: 1});
  assert.equal(api.snapshot().state.selectedModelId, 'app.B');
  api.selectCircularEdge('field'); assert.equal(api.snapshot().revealedRelationshipEdgeId, 'field');
  api.closeCircularDiagram(); assert.equal(api.snapshot().circularOpen, false);
  assert.deepEqual(JSON.parse(JSON.stringify(api.snapshot().state.viewport)), initial.viewport);
  assert.deepEqual(JSON.parse(JSON.stringify(api.snapshot().state.tableOptions)), initial.tableOptions);
});

test('the complete embedded browser controller is valid JavaScript', () => {
  const script = getBrowserControllerScript('test');
  new vm.Script(script.slice(script.indexOf('>') + 1, script.lastIndexOf('</script>')));
});
