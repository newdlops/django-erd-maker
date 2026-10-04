import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';
import vm from 'node:vm';
const require=createRequire(import.meta.url);
const {loadPhaseOneSample}=require('../../out/extension/services/loadPhaseOneSample.js');
const {decodeLayoutSnapshot}=require('../../out/shared/protocol/decodeDiagramBootstrap.js');
const {createDiagramRenderModel}=require('../../out/webview/state/createDiagramRenderModel.js');
const {getBrowserControllerScript}=require('../../out/webview/interaction/browserController.js');
const {getBrowserIndependentViewSource}=require('../../out/webview/interaction/runtime/browserIndependentViewSource.js');
const {getBrowserLeafCardSource}=require('../../out/webview/interaction/runtime/browserLeafCardSource.js');
const {getBrowserLayoutSource}=require('../../out/webview/interaction/runtime/browserLayoutSource.js');
const plain=value=>JSON.parse(JSON.stringify(value));

function fixture() {
  const payload=loadPhaseOneSample(), baseline=createDiagramRenderModel(payload);
  const originalNodes=new Map(payload.layout.nodes.map(n=>[n.modelId,n]));
  payload.layout.nodes=baseline.tables.map(t=>({...originalNodes.get(t.modelId),position:t.position,size:t.size}));
  const routes=new Map(baseline.edges.map(e=>[e.edgeId,e.points.split(' ').map(p=>{
    const [x,y]=p.split(',').map(Number);return {x,y};
  })]));
  payload.layout.routedEdges=payload.layout.routedEdges.map(e=>({...e,points:routes.get(e.edgeId)}));
  payload.layout.engineMetadata={...payload.layout.engineMetadata,relationshipPresentation:'june-bundled',
    renderedCarrierRoutes:[{carrierId:'test-overview',memberEdgeIds:baseline.edges.map(e=>e.edgeId),points:routes.get(baseline.edges[0].edgeId)}]};
  payload.layout.individualView={
    nodes:payload.layout.nodes.map(n=>({...structuredClone(n),position:{x:n.position.x+10000,y:n.position.y+5000}})),
    routedEdges:payload.layout.routedEdges.map(e=>({...e,points:e.points.map(p=>({x:p.x+10000,y:p.y+5000}))})),
  };
  return payload;
}

test('independent geometry survives decoding and preserves complete boundary routes',()=>{
  const payload=fixture();payload.layout=decodeLayoutSnapshot(payload.layout);
  const scene=createDiagramRenderModel(payload), baseline=structuredClone(payload);
  delete baseline.layout.individualView;
  const original=createDiagramRenderModel(baseline);
  assert.deepEqual(scene.tables,original.tables);
  assert.deepEqual(scene.edges,original.edges);
  assert.equal(scene.individualView.edges.length,original.relationshipOverview.individualEdges.length);
  assert.equal(scene.individualView.visualCrossings,original.relationshipOverview.individualVisualCrossings);
  for (const table of scene.tables) assert.deepEqual(scene.individualView.positions[table.modelId],
    {x:table.position.x+10000,y:table.position.y+5000});
});

test('incomplete, duplicate, resized and detached alternate geometry fails explicitly',()=>{
  for (const mutate of [
    view=>view.nodes.pop(),
    view=>view.nodes[1]=structuredClone(view.nodes[0]),
    view=>view.nodes[0].size.width+=1,
    view=>view.routedEdges.pop(),
    view=>view.routedEdges[1]=structuredClone(view.routedEdges[0]),
    view=>view.routedEdges[0].points.push({x:0,y:0}),
  ]) {
    const payload=fixture();mutate(payload.layout.individualView);
    assert.throws(()=>decodeLayoutSnapshot(payload.layout),/individualView/);
  }
  const payload=fixture();payload.layout.individualView.routedEdges[0].points[0]={x:0,y:0};
  payload.layout=decodeLayoutSnapshot(payload.layout);
  assert.throws(()=>createDiagramRenderModel(payload),/card boundaries/);
});

test('both controls switch positions and ports together and retain separate edits and viewports',()=>{
  const scene=createDiagramRenderModel(fixture());
  // This runtime test needs an enabled Leaf switch; geometry projection is
  // covered by leaf-card-render.test and the complete Captain file-load audit.
  scene.leafCards=[{id:'test-leaf',memberModelIds:scene.tables.slice(0,2).map(t=>t.modelId)}];
  const controller=getBrowserControllerScript('test');
  const metadata=controller.slice(controller.indexOf('const readEdgeMeta ='),controller.indexOf('const overlayMeta ='));
  const visual={};
  const context={renderModel:structuredClone(scene),document:{querySelectorAll(selector){
    return selector.includes('visual-crossing')?[visual]:[];
  }},readTableMeta:t=>({modelId:t.modelId,basePosition:{...t.position},width:t.size.width,height:t.size.height}),
  invalidateSceneGraph(){},getTableOptions:(s,id)=>s.tableOptions.find(o=>o.modelId===id)||{}};
  const runtime=vm.runInNewContext(getBrowserLayoutSource()+metadata+`
    const tableMetaById=new Map(tableMetaList.map(t=>[t.modelId,t]));
    const tableRenderById=new Map(renderModel.tables.map(t=>[t.modelId,t]));
    let state={tableOptions:tableMetaList.map(t=>({modelId:t.modelId})),viewport:{panX:0,panY:0,zoom:1}};
    let drag=null;
  `+getBrowserLeafCardSource()+getBrowserIndependentViewSource()+`;({
    setJuneOverviewExpanded,setLeafCardsVisible,getActiveEdgeMeta,getCurrentPosition,getStaticEdgePath,getOverviewRefreshState,
    state:()=>state,
    move(id,p){state.tableOptions.find(t=>t.modelId===id).manualPosition=p;},
    pan(x){state.viewport.panX=x;},
    removeLeafCards(){renderModel.leafCards=[];},
    path(meta){return getStaticEdgePath({meta,sourceTable:tableMetaById.get(meta.sourceModelId),
      targetTable:tableMetaById.get(meta.targetModelId),sourcePosition:getCurrentPosition(meta.sourceModelId),
      targetPosition:getCurrentPosition(meta.targetModelId)});},
  })`,context);
  const id=scene.tables[0].modelId,initial=plain(runtime.getCurrentPosition(id));
  const edited={x:initial.x+10,y:initial.y+5};runtime.move(id,edited);runtime.pan(20);
  runtime.setJuneOverviewExpanded(true);
  assert.deepEqual(plain(runtime.getCurrentPosition(id)),edited);
  runtime.setLeafCardsVisible(false);
  assert.deepEqual(plain(runtime.getCurrentPosition(id)),scene.individualView.positions[id]);
  assert.equal(runtime.getActiveEdgeMeta().length,scene.individualView.edges.length);
  for(const edge of runtime.getActiveEdgeMeta()) assert.equal(runtime.path(edge).map(p=>p.x+','+p.y).join(' '),edge.points);
  assert.match(visual.textContent,new RegExp(String(scene.individualView.visualCrossings)));
  const altEdit={x:initial.x+10020,y:initial.y+5005};runtime.move(id,altEdit);runtime.pan(75);
  const refresh=runtime.getOverviewRefreshState(runtime.state());
  assert.deepEqual(plain(refresh.tableOptions.find(t=>t.modelId===id).manualPosition),edited);
  assert.equal(refresh.viewport.panX,20);
  runtime.setJuneOverviewExpanded(false);
  assert.deepEqual(plain(runtime.getCurrentPosition(id)),edited);assert.equal(runtime.state().viewport.panX,20);
  runtime.setJuneOverviewExpanded(true);
  assert.deepEqual(plain(runtime.getCurrentPosition(id)),altEdit);assert.equal(runtime.state().viewport.panX,75);
  runtime.setLeafCardsVisible(true);
  assert.deepEqual(plain(runtime.getCurrentPosition(id)),edited);
  runtime.removeLeafCards();runtime.setJuneOverviewExpanded(true);
  assert.deepEqual(plain(runtime.getCurrentPosition(id)),altEdit,'a disabled, empty Leaf switch must not prevent the individual view');
});
