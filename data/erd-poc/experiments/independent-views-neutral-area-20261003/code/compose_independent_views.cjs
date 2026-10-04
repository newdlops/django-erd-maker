// Package already audited neural geometries. This never proposes coordinates.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict'),vm=require('node:vm');
const [overviewDirectory,individualDirectory,payloadFile,outputDirectory]=process.argv.slice(2);
assert.ok(outputDirectory,'overview-dir individual-dir payload.json fresh-output-dir');
const root=path.resolve(__dirname,'../..'),out=path.resolve(outputDirectory);
assert.ok(out.startsWith(path.join(root,'.tmp')+path.sep),'compose in an isolated .tmp directory');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const overviewFile=path.join(overviewDirectory,'candidate.layout.json');
const individualFile=path.join(individualDirectory,'candidate.individual.layout.json');
const overview=read(overviewFile),individual=read(individualFile),payload=read(payloadFile);
const overviewAudit=read(path.join(overviewDirectory,'product.audit.json'));
const individualAudit=read(path.join(individualDirectory,'individual.audit.json'));
const individualWorkflow=read(path.join(individualDirectory,'workflow.audit.json'));
assert.equal(sha(overviewFile),overviewAudit.candidateSha256);
assert.equal(sha(individualFile),individualAudit.candidateSha256);
assert.equal(sha(payloadFile),overviewAudit.payloadSha256);
assert.equal(sha(payloadFile),individualWorkflow.payloadSha256);
assert.equal(individualWorkflow.candidateSha256,individualAudit.candidateSha256);
assert.equal(individualWorkflow.allChecksPassed,true);
assert.equal(individualAudit.actualProductRendererVerified,true);
assert.equal(overviewAudit.productCoordinatesPreserved,true);
assert.equal(overviewAudit.spacingViolations,0);assert.equal(individualAudit.spacingViolations,0);
fs.mkdirSync(out,{recursive:false});
const candidate=path.join(out,'candidate.layout.json');
overview.individualView={nodes:individual.nodes,routedEdges:individual.routedEdges};
fs.writeFileSync(candidate,JSON.stringify(overview,null,2)+'\n',{flag:'wx'});
Object.assign(process.env,{DJERD_LAYOUT_FROM_FILE:candidate,DJERD_CONSOLIDATE_EDGES:'1',
  DJERD_FINAL_EXPORT_RETOUCH:'0',DJERD_DISABLE_CLUSTER_FALLBACK:'1'});
const {runOgdfLayout}=require(path.join(root,'out/extension/services/layout/runOgdfLayout.js'));
const {createDiagramRenderModel,measureRenderedTableClearance,measureRenderedVisualConflicts}=require(path.join(root,'out/webview/state/createDiagramRenderModel.js'));
function auditRuntime(scene) {
  const load=name=>require(path.join(root,'out/webview/interaction',name+'.js'));
  const controller=load('browserController').getBrowserControllerScript('audit');
  const metadata=controller.slice(controller.indexOf('const readEdgeMeta ='),controller.indexOf('const overlayMeta ='));
  const dom=load('runtime/browserDomSource').getBrowserDomSource();
  const readTable=dom.slice(dom.indexOf('function readTableMeta('),dom.indexOf('function setSidebarSheet('));
  const runtime=vm.runInNewContext(load('runtime/browserLayoutSource').getBrowserLayoutSource()+readTable+metadata+`
    const tableMetaById=new Map(tableMetaList.map(t=>[t.modelId,t]));
    const tableRenderById=new Map(renderModel.tables.map(t=>[t.modelId,t]));
    let state={tableOptions:tableMetaList.map(t=>({modelId:t.modelId})),viewport:{panX:0,panY:0,zoom:1}};
    let drag=null;
  `+load('runtime/browserLeafCardSource').getBrowserLeafCardSource()
   +load('runtime/browserIndependentViewSource').getBrowserIndependentViewSource()+`;({
    setJuneOverviewExpanded,setLeafCardsVisible,
    snapshot(){
      const tables=renderModel.tables.map(t=>({...t,position:getCurrentPosition(t.modelId)}));
      const tablesById=new Map(tables.map(t=>[t.modelId,{modelId:t.modelId,...t.position,width:t.size.width,height:t.size.height}]));
      const entries=getActiveEdgeMeta().map(meta=>({meta,sourceTable:tableMetaById.get(meta.sourceModelId),
        targetTable:tableMetaById.get(meta.targetModelId),sourcePosition:getCurrentPosition(meta.sourceModelId),targetPosition:getCurrentPosition(meta.targetModelId)}));
      let paths=getStaticOrCatalogEdgePaths(entries);
      if(leafCardsVisible)paths=projectLeafCardEdges({tablesById,leafBundles:createLeafCardRecords(renderModel.leafCards,tablesById,'')},paths);
      return {tables,edges:paths.map(p=>({...p.meta,points:p.points.map(q=>q.x+','+q.y).join(' ')}))};
    }
  })`,{renderModel:structuredClone(scene),document:{querySelectorAll:()=>[]},invalidateSceneGraph(){},
    isVisibleModel:()=>true,getTableOptions:(state,id)=>state.tableOptions.find(t=>t.modelId===id)||{}});
  function verify(individual) {
    const snapshot=runtime.snapshot(),expected=individual?scene.individualView.edges:scene.edges;
    assert.equal(snapshot.edges.length,expected.length);
    const expectedById=new Map(expected.map(e=>[e.edgeId,e.points]));
    for(const edge of snapshot.edges)assert.equal(edge.points,expectedById.get(edge.edgeId),edge.edgeId);
    const metrics=measureRenderedVisualConflicts({...scene,...snapshot,
      leafCards:individual?[]:scene.leafCards,leafBundles:individual?[]:scene.leafBundles,
      leafCardOverview:individual?undefined:scene.leafCardOverview});
    assert.equal(metrics.visualCrossings,individual?scene.individualView.visualCrossings:scene.visualCrossings);
  }
  verify(false);runtime.setJuneOverviewExpanded(true);runtime.setLeafCardsVisible(false);verify(true);
  runtime.setJuneOverviewExpanded(false);runtime.setLeafCardsVisible(true);verify(false);
}
async function main() {
  const messages=[];
  const loaded=await runOgdfLayout(root,payload,'fmmm',{info:s=>messages.push(s),warn:s=>messages.push(s)},1,'straight',false,false,false);
  assert.equal(loaded.applied,true,loaded.reason);
  assert.ok(loaded.layout.individualView,'file loading must preserve alternate geometry');
  assert.equal(loaded.layout.individualView.nodes.length,individual.nodes.length);
  assert.equal(loaded.layout.individualView.routedEdges.length,individual.routedEdges.length);
  const inputNodes=new Map(individual.nodes.map(n=>[n.modelId,n]));
  const inputRoutes=new Map(individual.routedEdges.map(e=>[e.edgeId,e]));
  for(const node of loaded.layout.individualView.nodes) {
    assert.deepEqual(node.position,inputNodes.get(node.modelId)?.position,node.modelId);
    assert.deepEqual(node.size,inputNodes.get(node.modelId)?.size,node.modelId);
  }
  for(const edge of loaded.layout.individualView.routedEdges)
    assert.deepEqual(edge.points,inputRoutes.get(edge.edgeId)?.points,edge.edgeId);
  const scene=createDiagramRenderModel({...payload,layout:loaded.layout,view:{...payload.view,tableOptions:[]}});
  assert.equal(scene.visualCrossings,overviewAudit.visualCrossings);
  assert.equal(scene.individualView.visualCrossings,individualAudit.visualCrossings);
  assert.equal(scene.tables.length,1244);assert.equal(scene.individualView.edges.length,1727);
  const individualTables=scene.tables.map(t=>({...t,position:scene.individualView.positions[t.modelId]}));
  const clearance=measureRenderedTableClearance({...scene,tables:individualTables,leafCardOverview:undefined,leafCards:[],leafBundles:[]});
  assert.equal(clearance.spacingViolations,0);assert.ok(clearance.bboxArea<=1.5e9);
  const byId=new Map(individual.routedEdges.map(e=>[e.edgeId,e.points.map(p=>p.x+','+p.y).join(' ')]));
  for(const edge of scene.individualView.edges)assert.equal(edge.points,byId.get(edge.edgeId));
  auditRuntime(scene);
  const report={candidate,candidateSha256:sha(candidate),overviewSource:overviewFile,overviewSourceSha256:sha(overviewFile),
    individualSource:individualFile,individualSourceSha256:sha(individualFile),payloadSha256:sha(payloadFile),
    overviewVisual:scene.visualCrossings,individualVisual:scene.individualView.visualCrossings,
    canonicalRelationships:scene.individualView.edges.length,models:scene.tables.length,
    actualProductFileLoadVerified:true,completeIndividualGeometryPreserved:true,individualSpacingViolations:0,
    browserRouteFunctionParity:true,roundTripViewSwitchVerified:true,
    thresholdsMet:scene.visualCrossings<=300&&scene.individualView.visualCrossings<2000,browserVerified:false,promoted:false};
  fs.writeFileSync(path.join(out,'audit.json'),JSON.stringify(report,null,2)+'\n');
  fs.writeFileSync(path.join(out,'product-load.log'),messages.join('\n')+'\n');
  console.log(JSON.stringify(report));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
