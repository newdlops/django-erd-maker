import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';
const require=createRequire(import.meta.url);
const {createJuneRelationshipOverview}=require('../../out/webview/state/createJuneRelationshipOverview.js');
const {createDiagramRenderModel,measureRenderedVisualConflicts}=require('../../out/webview/state/createDiagramRenderModel.js');
const {loadPhaseOneSample}=require('../../out/extension/services/loadPhaseOneSample.js');
const {decodeLayoutSnapshot}=require('../../out/shared/protocol/decodeDiagramBootstrap.js');
const {synchronizeLayoutRenderedVisualMetrics}=require('../../out/extension/services/layout/runOgdfLayout.js');
const {getBrowserControllerScript}=require('../../out/webview/interaction/browserController.js');
const {getBrowserEventSource}=require('../../out/webview/interaction/runtime/browserEventSource.js');
const {renderDiagramDocument}=require('../../out/webview/app/renderDiagramDocument.js');
const plain=value=>JSON.parse(JSON.stringify(value));
function example() {
 const payload=loadPhaseOneSample();
 const baseline=createDiagramRenderModel(payload);
 assert.ok(baseline.edges.length>=2);
 payload.layout.engineMetadata={...payload.layout.engineMetadata,relationshipPresentation:'june-bundled',
  renderedCarrierRoutes:[{carrierId:'june-group:test',memberEdgeIds:baseline.edges.map(edge=>edge.edgeId),
   points:[{x:-999,y:-999},{x:-998,y:-998}]}]};
 return {payload,baseline};
}
test('overview keeps all original relationships and uses a real member straight route',()=>{
 const {payload,baseline}=example(), scene=createDiagramRenderModel(payload);
 assert.equal(scene.edges.length,1);
 assert.deepEqual(scene.tables,baseline.tables);
 assert.deepEqual(scene.relationshipOverview.individualEdges,baseline.edges);
 assert.deepEqual(new Set(scene.edges[0].memberEdgeIds),new Set(baseline.edges.map(edge=>edge.edgeId)));
 assert.ok(baseline.edges.some(edge=>edge.points===scene.edges[0].points));
 assert.equal(scene.edges[0].carrierRole,'overview');
 assert.equal(scene.edges[0].markerEndId,'');
 assert.equal(scene.visualCrossings,measureRenderedVisualConflicts(scene).visualCrossings);
 assert.ok(!scene.edges[0].points.includes('-999'));
});
test('missing, duplicate, unknown, empty, and colliding memberships fall back to full geometry',()=>{
 const {baseline}=example(), ids=baseline.edges.map(edge=>edge.edgeId);
 const group=(id,members)=>({carrierId:id,memberEdgeIds:members,points:[]});
 for(const groups of [[],[group('g',ids.slice(1))],[group('g',[...ids,ids[0]])],
  [group('g',[...ids,'missing'])],[group('g',[])],
  [group(ids[0],ids.slice(1)),group('h',[ids[0]])]]) {
  assert.equal(createJuneRelationshipOverview(baseline.edges,groups),undefined);
 }
 const {payload}=example();
 payload.layout.engineMetadata.renderedCarrierRoutes[0].memberEdgeIds.pop();
 payload.layout.engineMetadata.visualCrossings=0;
 const scene=createDiagramRenderModel(payload);
 assert.equal(scene.relationshipOverview,undefined);
 assert.deepEqual(scene.edges,baseline.edges);
 assert.equal(scene.visualCrossings,measureRenderedVisualConflicts(baseline).visualCrossings);
});
test('historical grouped metadata needs the explicit presentation flag',()=>{
 const {payload,baseline}=example();
 delete payload.layout.engineMetadata.relationshipPresentation;
 const scene=createDiagramRenderModel(payload);
 assert.deepEqual(scene.edges,baseline.edges);
 assert.equal(scene.relationshipOverview,undefined);
});
test('decoding and product scoring retain a distinct overview scope',()=>{
 const {payload}=example();
 payload.layout=decodeLayoutSnapshot(payload.layout);
 assert.equal(payload.layout.engineMetadata.relationshipPresentation,'june-bundled');
 const m=synchronizeLayoutRenderedVisualMetrics(payload,payload.layout);
 assert.equal(payload.layout.engineMetadata.visualCrossingsScope,'rendered-june-bundled-overview-v1');
 assert.equal(m.edgeCount,1);
});
test('the actual toggle handler expands every route and restores the overview and readouts',()=>{
 const {payload}=example(), scene=createDiagramRenderModel(payload);
 const html=renderDiagramDocument(payload);
 assert.match(html,/data-june-bundles-toggle aria-pressed="true"/);
 assert.match(html,/June bundle overview/);
 const controller=getBrowserControllerScript('test');
 const source=controller.slice(controller.indexOf('const readEdgeMeta ='),controller.indexOf('const tableMetaList ='));
 let click,invalidations=0,renders=0;
 const button={attributes:{},classList:{toggle(){}},setAttribute(k,v){this.attributes[k]=v;},addEventListener(_,fn){click=fn;}};
 const visual={},count={};
 const context={renderModel:scene,state:{},document:{querySelectorAll(selector){
  if (selector.includes('leaf-card')) return [];
  return selector.includes('toggle')?[button]:selector.includes('visual-crossing')?[visual]:[count];
 }},invalidateSceneGraph(){invalidations++;},applyState(){renders++;}};
 const event=getBrowserEventSource(),start=event.indexOf('for (const button of document.querySelectorAll("[data-june-bundles-toggle]"))');
 const handler=event.slice(start,event.indexOf('for (const button of document.querySelectorAll("[data-cluster-collapse-toggle]"))',start));
 const api=vm.runInNewContext(source+handler+';({getActiveEdgeMeta,setJuneOverviewExpanded})',context);
 assert.equal(api.getActiveEdgeMeta().length,1);
 click();
 assert.deepEqual(plain(api.getActiveEdgeMeta()).map(edge=>edge.edgeId),scene.relationshipOverview.individualEdges.map(edge=>edge.edgeId));
 assert.equal(button.attributes['aria-pressed'],'false');
 assert.match(visual.textContent,/Individual relationships/);
 click();
 assert.equal(api.getActiveEdgeMeta().length,1);
 assert.equal(button.attributes['aria-pressed'],'true');
 assert.match(count.textContent,/bundles/);
 assert.equal(renders,2);
 assert.equal(invalidations,2);
});
