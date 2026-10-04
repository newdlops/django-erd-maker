// Audit an actual product file load, including current graph coverage and the
// browser's route projection. Execute under run_memory_bounded.py.
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const assert = require('node:assert/strict'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const [candidate, payloadFile, outputStem] = process.argv.slice(2);
assert.ok(outputStem, 'candidate.layout.json payload.json output-stem');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const payload = read(payloadFile), original = read(candidate);
const product = require(path.join(root, 'out/webview/state/createDiagramRenderModel.js'));
const {runOgdfLayout} = require(path.join(root, 'out/extension/services/layout/runOgdfLayout.js'));
const {consolidateEdges} = require(path.join(root, 'out/extension/services/layout/consolidateEdges.js'));
const {getBrowserLeafCardSource} = require(path.join(root, 'out/webview/interaction/runtime/browserLeafCardSource.js'));
const {getBrowserLayoutSource} = require(path.join(root, 'out/webview/interaction/runtime/browserLayoutSource.js'));
Object.assign(process.env, {DJERD_LAYOUT_FROM_FILE:path.resolve(candidate), DJERD_CONSOLIDATE_EDGES:'1',
  DJERD_FINAL_EXPORT_RETOUCH:'0', DJERD_DISABLE_CLUSTER_FALLBACK:'1'});
async function main() {
  const messages=[];
  const result=await runOgdfLayout(root,payload,'fmmm',{info(s){messages.push(s);},warn(s){messages.push('WARN '+s);}},1,'straight',false,false,false);
  assert.equal(result.applied,true,[result.reason,...messages.slice(-3)].join('\n'));
  assert.equal(result.layout.nodes.length,original.nodes.length);
  const originalNodes=new Map(original.nodes.map(n=>[n.modelId,n]));
  for(const n of result.layout.nodes) {
    const before=originalNodes.get(n.modelId);assert.ok(before,n.modelId);
    assert.deepEqual([n.position,n.size,n.clusterId],[before.position,before.size,before.clusterId],n.modelId);
  }
  assert.equal(result.layout.routedEdges.length,original.routedEdges.length);
  const originalRoutes=new Map(original.routedEdges.map(e=>[e.edgeId,e]));
  for(const e of result.layout.routedEdges)assert.deepEqual(e.points,originalRoutes.get(e.edgeId)?.points,e.edgeId);
  assert.ok(!messages.some(s=>/omitted.*non-self|routeCompletenessStatus=miss/.test(s)));
  const scene=product.createDiagramRenderModel({...payload,layout:result.layout,view:{...payload.view,tableOptions:[]}});
  assert.ok(scene.relationshipOverview && scene.leafCardOverview);
  assert.deepEqual(new Set(scene.tables.map(t=>t.modelId)),new Set(payload.graph.nodes.map(n=>n.modelId)));
  const ids=consolidateEdges(payload.graph.structuralEdges).layoutEdges.filter(e=>e.sourceModelId!==e.targetModelId).map(e=>e.id);
  const coverage=[...scene.edges.flatMap(e=>e.memberEdgeIds?.length?e.memberEdgeIds:[e.edgeId]),...scene.leafCardOverview.internalEdgeIds];
  assert.equal(coverage.length,ids.length);
  assert.deepEqual(new Set(coverage),new Set(ids));
  const obstacles=product.getRenderedConnectionTables(scene),boxes=new Map(obstacles.map(n=>[n.modelId,n]));
  const lineCounts=new Map(scene.leafCards.map(c=>[c.id,0])),pairs=new Set();
  let endpoints=0;
  for(const edge of scene.edges) {
    const points=edge.points.split(' ').map(p=>p.split(',').map(Number));
    assert.equal(points.length,2);
    assert.ok(Math.hypot(points[0][0]-points[1][0],points[0][1]-points[1][1])>.01);
    const endpointIds=[edge.leafCardEndpointIds?.[0]||edge.sourceModelId,edge.leafCardEndpointIds?.[1]||edge.targetModelId];
    for(let end=0;end<2;end++) {
      const n=boxes.get(endpointIds[end]);assert.ok(n,endpointIds[end]);
      const [x,y]=points[end],left=n.position.x,top=n.position.y,right=left+n.size.width,bottom=top+n.size.height;
      assert.ok(Math.min(Math.abs(x-left),Math.abs(x-right),Math.abs(y-top),Math.abs(y-bottom))<.02,
        JSON.stringify({edge:edge.edgeId,endpoint:endpointIds[end],point:[x,y],rect:[left,top,right,bottom],members:edge.memberEdgeIds}));
      assert.ok(x>=left-.02&&x<=right+.02&&y>=top-.02&&y<=bottom+.02,edge.edgeId);endpoints++;
    }
    if(edge.leafCardEndpointIds) {
      for(const id of edge.leafCardEndpointIds.filter(Boolean))lineCounts.set(id,lineCounts.get(id)+1);
      const key=endpointIds.slice().sort().join('\0');assert.equal(pairs.has(key),false,key);pairs.add(key);
    }
  }
  assert.equal(lineCounts.size,49);
  assert.ok([...lineCounts.values()].every(count=>count===1));
  const metas=new Map(scene.tables.map(n=>[n.modelId,{basePosition:n.position,width:n.size.width,height:n.size.height}]));
  const current=new Map(scene.tables.map(n=>[n.modelId,{modelId:n.modelId,...n.position,width:n.size.width,height:n.size.height}]));
  const individual=scene.leafCardOverview.individualEdges,base=scene.leafCardOverview.baseEdges;
  const context={individualEdgeMeta:individual,edgeMeta:base,getActiveEdgeMeta:()=>base,tableMetaById:metas,
    isVisibleModel:()=>true,lookupPosition:id=>metas.get(id).basePosition};
  const runtime=vm.runInNewContext(getBrowserLeafCardSource()+getBrowserLayoutSource()
    +';getCurrentPosition=lookupPosition;({createLeafCardRecords,projectLeafCardEdges,getStaticOrCatalogEdgePaths})',context);
  const gpuScene={leafBundles:runtime.createLeafCardRecords(scene.leafCards,current,''),tablesById:current};
  const entries=base.map(meta=>({meta,sourceTable:metas.get(meta.sourceModelId),targetTable:metas.get(meta.targetModelId),
    sourcePosition:metas.get(meta.sourceModelId).basePosition,targetPosition:metas.get(meta.targetModelId).basePosition}));
  const routes=runtime.projectLeafCardEdges(gpuScene,runtime.getStaticOrCatalogEdgePaths(entries));
  const actual=JSON.parse(JSON.stringify(routes.map(r=>[r.edgeId,r.meta.points,r.meta.memberEdgeIds])));
  const expected=JSON.parse(JSON.stringify(scene.edges.map(e=>[e.edgeId,e.points,e.memberEdgeIds])));
  assert.equal(actual.length,expected.length);
  for(let i=0;i<actual.length;i++)assert.deepEqual(actual[i],expected[i],expected[i][0]);
  const metrics=product.measureRenderedVisualConflicts(scene),clearance=product.measureRenderedTableClearance(scene);
  const memberClearance=product.measureRenderedTableClearance({...scene,leafCardOverview:undefined});
  if(memberClearance.spacingViolations)for(let i=0;i<scene.tables.length;i++)for(let j=i+1;j<scene.tables.length;j++) {
    const a=scene.tables[i],b=scene.tables[j];
    const dx=Math.max(0,a.position.x-b.position.x-b.size.width,b.position.x-a.position.x-a.size.width);
    const dy=Math.max(0,a.position.y-b.position.y-b.size.height,b.position.y-a.position.y-a.size.height);
    if(dx+.01<56&&dy+.01<42)console.error(JSON.stringify({spacing:[a.modelId,b.modelId],dx,dy,a:{position:a.position,size:a.size},b:{position:b.position,size:b.size}}));
  }
  assert.equal(clearance.spacingViolations,0);assert.equal(memberClearance.spacingViolations,0);
  assert.ok(clearance.bboxArea<=1.5e9);assert.ok(metrics.visualCrossings<=500);
  assert.equal(metrics.visualCrossings,original.engineMetadata.visualCrossings);
  const digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const report={candidate,payloadFile,candidateSha256:digest(candidate),payloadSha256:digest(payloadFile),...metrics,
    bboxB:clearance.bboxArea/1e9,spacingViolations:clearance.spacingViolations,memberSpacingViolations:memberClearance.spacingViolations,
    physicalCards:obstacles.length,realModels:scene.tables.length,leafCards:scene.leafCards.length,
    groupedLeafModels:scene.leafCards.reduce((s,c)=>s+c.memberModelIds.length,0),singleLinePairs:pairs.size,
    oneLinePerLeafCard:true,canonicalRelationships:ids.length,canonicalCoverageExactlyOnce:true,
    internalRelationships:scene.leafCardOverview.internalEdgeIds.length,validBoundaryEndpoints:endpoints,
    individualVisualCrossings:scene.relationshipOverview.individualVisualCrossings,
    metricScope:result.layout.engineMetadata.visualCrossingsScope,productCoordinatesPreserved:true,
    browserRouteFunctionParity:true,browserVerified:false,warnings:messages.filter(s=>s.startsWith('WARN'))};
  fs.writeFileSync(outputStem+'.audit.json',JSON.stringify(report,null,2)+'\n');
  fs.writeFileSync(outputStem+'.product-load.log',messages.join('\n'));
  fs.writeFileSync(outputStem+'.scene.json',JSON.stringify({tables:obstacles,edges:scene.edges,leafCards:scene.leafCards}));
  console.log(JSON.stringify(report));
}
main().catch(error=>{console.error(String(error.stack).slice(0,6000));process.exitCode=1;});
