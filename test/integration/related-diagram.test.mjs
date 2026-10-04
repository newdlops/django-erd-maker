import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';

const require = createRequire(import.meta.url);
const {createRelatedDiagramTools} = require('../../out/webview/state/relatedDiagram.js');
const {createRelationshipExplorerTools} = require('../../out/webview/state/relationshipExplorer.js');
const {getBrowserRelatedDiagramSource} = require('../../out/webview/interaction/runtime/browserRelatedDiagramSource.js');
const {getBrowserRelationshipSource} = require('../../out/webview/interaction/runtime/browserRelationshipSource.js');
const {getBrowserRenderSource} = require('../../out/webview/interaction/runtime/browserRenderSource.js');
const {getBrowserStateSource} = require('../../out/webview/interaction/runtime/browserStateSource.js');
const compact = createRelatedDiagramTools(), explorer = createRelationshipExplorerTools();
const relationship = (edgeId, otherModelId, direction = 'outgoing', kind = 'foreign_key') => ({edgeId, otherModelId, direction, kind,
  fieldName: edgeId, sourceModelId: direction === 'incoming' ? otherModelId : 'app.Root', targetModelId: direction === 'incoming' ? 'app.Root' : otherModelId});
function fixture() {
  const relationships = [relationship('a', 'app.A'), relationship('b', 'app.B'), relationship('second-a', 'app.A'),
    relationship('back-a', 'app.A', 'incoming'), relationship('self', 'app.Root', 'self'), relationship('missing', 'external.Missing')];
  const root = {modelId: 'app.Root', modelName: 'Root', relationships};
  const models = new Map([root, ...['A', 'B', 'Unrelated'].map(name => ({modelId: 'app.' + name, modelName: name, relationships: []}))].map(model => [model.modelId, model]));
  return {root, models, relationships, cards: [{id: 'leaves', label: 'Root', memberModelIds: ['app.A', 'app.B', 'app.Unrelated']}]};
}

test('a local diagram includes only direct peers, retaining every field in one line per leaf card', () => {
  const {root, models, relationships, cards} = fixture();
  const graph = compact.build(root, relationships, models, cards);
  assert.equal(graph.peers.length, 2);
  assert.equal(graph.self.length, 1);
  assert.equal(graph.relationshipCount, 6);
  assert.equal(graph.modelCount, 4);
  const group = graph.peers.find(peer => peer.grouped);
  assert.deepEqual(group.modelIds, ['app.A', 'app.B']);
  assert.equal(group.relationships.length, 4);
  assert.equal(compact.direction(group), 'Both directions');
  assert.equal(graph.peers.find(peer => !peer.grouped).available, false);
  assert.ok(!graph.peers.flatMap(peer => peer.modelIds).includes('app.Unrelated'));
  assert.equal(new Set([...graph.self, ...graph.peers.flatMap(peer => peer.relationships)].map(r => r.edgeId)).size, 6);
});

test('opposite directions and duplicate fields between ordinary models share one line', () => {
  const {root, models, relationships} = fixture();
  const graph = compact.build(root, relationships, models, []);
  const peer = graph.peers.find(p => p.modelIds.includes('app.A'));
  assert.equal(peer.relationships.length, 3);
  assert.equal(compact.direction(peer), 'Both directions');
  const links = compact.links([peer], new Map([[peer.key, {x: 400, y: 50, width: 200, height: 120}]]), {x: 100, y: 50, width: 200, height: 120});
  assert.equal(links.length, 1);
  assert.equal(links[0].towardCenter, true); assert.equal(links[0].towardPeer, true);
  assert.deepEqual(links[0].from, {x: 300, y: 110});
  assert.deepEqual(links[0].to, {x: 400, y: 110});
});

test('paging covers all peers once and a chosen relationship opens its page', () => {
  const {root, models} = fixture();
  const relationships = Array.from({length: 57}, (_, i) => relationship('edge-' + i, 'app.Peer' + String(i).padStart(2, '0')));
  const all = [];
  for (let page = 0; page < 8; page++) all.push(...compact.build(root, relationships, models, [], {page, pageSize: 8}).visiblePeers);
  assert.equal(all.length, 57);
  assert.equal(new Set(all.map(peer => peer.key)).size, 57);
  const selected = compact.build(root, relationships, models, [], {page: 0, pageSize: 8, edgeId: 'edge-56'});
  assert.equal(selected.page, 7);
  assert.ok(selected.visiblePeers[0].relationships.some(r => r.edgeId === 'edge-56'));
});

test('a mixed leaf group retains both inheritance and references in its direction label', () => {
  const {root, models, cards} = fixture();
  const relationships = [relationship('extends', 'app.A', 'incoming', 'inheritance'), relationship('root_id', 'app.B', 'incoming')];
  const graph = compact.build(root, relationships, models, cards);
  assert.equal(graph.peers.length, 1);
  assert.equal(compact.direction(graph.peers[0]), 'Referenced / inherited by');
  assert.equal(compact.color(graph.peers[0]), '#b6e7d9');
  assert.deepEqual(graph.peers[0].relationships, relationships);
});

test('filtering stays exact and does not silently add siblings from a leaf group', () => {
  const {root, models, cards} = fixture();
  const filtered = explorer.results(root, {filter: 'incoming'});
  const graph = compact.build(root, filtered, models, cards);
  assert.equal(graph.relationshipCount, 1);
  assert.deepEqual(graph.peers[0].modelIds, ['app.A']);
  assert.equal(explorer.results(root, {modelIds: ['app.B']}).length, 1);
  assert.equal(compact.build(root, [], models, cards).peers.length, 0);
  assert.equal(compact.build(root, [], models, cards).pageCount, 1);
});

test('overlapping leaf memberships do not merge distinct models ambiguously', () => {
  const {root, models, cards} = fixture();
  const graph = compact.build(root, root.relationships, models, [...cards, {id: 'other', label: 'Other', memberModelIds: ['app.A']}]);
  assert.ok(graph.peers.some(peer => peer.key === 'model:app.A'));
});

test('straight routes use ordered boundary ports and stay inside the empty column gaps', () => {
  const {root, models} = fixture();
  const peers = compact.build(root, Array.from({length: 8}, (_, i) => relationship('e' + i, 'app.P' + i)), models, []).peers;
  const boxes = new Map(peers.map((p, i) => [p.key, {x: i % 2 ? 680 : 20, y: Math.floor(i / 2) * 140 + 20, width: 220, height: 120}]));
  const center = {x: 350, y: 230, width: 220, height: 160};
  const links = compact.links(peers, boxes, center);
  assert.equal(links.length, 8);
  for (const link of links) {
    const peer = boxes.get(link.key);
    assert.ok([center.x, center.x + center.width].includes(link.from.x));
    assert.ok(link.from.y > center.y && link.from.y < center.y + center.height);
    assert.equal(link.to.y, peer.y + peer.height / 2);
    assert.ok(Math.abs(link.to.x - link.from.x) <= 110);
  }
});

function navigationRuntime() {
  const {root, models, cards} = fixture();
  const sidebar = {scrollTop: 45};
  const initial = {selectedModelId: root.modelId, viewport: {zoom: 0.2, panX: -3200, panY: 800},
    tableOptions: [{modelId: 'app.A', manualPosition: {x: 10000, y: 5000}}], layoutMode: 'fmmm', collapseClusters: false, settings: {}};
  const sandbox = {state: structuredClone(initial), inspectorModelById: models, tableMetaById: models,
    relationshipByEdgeId: new Map(root.relationships.map(r => [r.edgeId, r])), revealedRelationshipEdgeId: '',
    document: {querySelector: selector => selector.includes('data-sidebar-sheet') ? sidebar : null, querySelectorAll: () => []},
    setSidebarSheet: () => {}, invalidateSceneGraph: () => {}, renderModel: {leafCards: cards}};
  const api = vm.runInNewContext(getBrowserStateSource() + getBrowserRelationshipSource() + getBrowserRelatedDiagramSource() + getBrowserRenderSource() + `
    applyState = () => {}; scheduleViewportRender = () => {};
    ({openRelatedDiagram, closeRelatedDiagram, navigateRelatedDiagram, goBackToModel, dispatch, isRelatedDiagramOpen,
      setLocal: () => {connectionQuery = 'a'; connectionFilter = 'incoming'; relatedDiagramPage = 1; relatedDiagramPeerKey = 'group:leaves';},
      snapshot: () => ({state, query: connectionQuery, filter: connectionFilter, local: relatedDiagramNavigation(), history: modelNavigationHistory, revealedRelationshipEdgeId})})`, sandbox);
  return {api, initial, sidebar};
}
const plain = value => JSON.parse(JSON.stringify(value));

test('local navigation and Back preserve filters and return to the original overview without moving models', () => {
  const {api, initial, sidebar} = navigationRuntime();
  api.openRelatedDiagram('a'); api.setLocal();
  const before = plain(api.snapshot());
  api.navigateRelatedDiagram('app.B');
  assert.equal(api.snapshot().state.selectedModelId, 'app.B');
  assert.deepEqual(plain(api.snapshot().state.viewport), initial.viewport);
  assert.equal(sidebar.scrollTop, 0);
  api.goBackToModel();
  assert.equal(api.isRelatedDiagramOpen(), true);
  assert.equal(api.snapshot().state.selectedModelId, 'app.Root');
  assert.deepEqual(plain(api.snapshot().local), before.local);
  assert.equal(api.snapshot().query, 'a');
  assert.equal(sidebar.scrollTop, 45);
  api.goBackToModel();
  assert.equal(api.isRelatedDiagramOpen(), false);
  assert.deepEqual(plain(api.snapshot().state), initial);
  assert.equal(api.snapshot().query, '');
  assert.equal(api.snapshot().history.length, 0);
});

test('search inside the local diagram changes the center without adopting distant saved coordinates', () => {
  const {api, initial} = navigationRuntime();
  api.openRelatedDiagram();
  api.dispatch({type: 'focus-model', modelId: 'app.A', zoom: 1});
  assert.equal(api.snapshot().state.selectedModelId, 'app.A');
  assert.deepEqual(plain(api.snapshot().state.viewport), initial.viewport);
  api.closeRelatedDiagram();
  assert.deepEqual(plain(api.snapshot().state.tableOptions), initial.tableOptions);
  assert.equal(api.snapshot().state.selectedModelId, 'app.Root');
});
