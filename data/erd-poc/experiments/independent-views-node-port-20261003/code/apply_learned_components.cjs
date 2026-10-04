// Apply a frozen learned policy's complete two-view snapshot. No coordinate
// selection, geometry repair, omitted relationships, or heuristic fallback.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const [sourceFile,payloadFile,directory,proposalFile]=process.argv.slice(2);
assert.ok(proposalFile,'source.layout.json payload.json directory policy-output');
const experimental=process.argv.includes('--experimental');
const allowAreaGrowth=process.argv.includes('--allow-area-growth');
assert.ok(!allowAreaGrowth||experimental,'area growth requires an explicit isolated experiment');
if(experimental)assert.ok(path.resolve(directory).startsWith(path.resolve(__dirname,'../../.tmp')+path.sep),
  'unpromoted experimental snapshots must stay inside the project .tmp directory');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const exported=read(path.join(directory,'export.json')),source=read(sourceFile),payload=read(payloadFile);
assert.equal(digest(sourceFile),exported.sourceSha256);assert.equal(digest(payloadFile),exported.payloadSha256);
const product=require(path.resolve(__dirname,'../../out/webview/state/createDiagramRenderModel.js'));
const render=layout=>product.createDiagramRenderModel({...payload,layout,view:{...payload.view,tableOptions:[]}});
const individual=model=>({...model,edges:model.leafCardOverview.individualEdges,leafCardOverview:undefined});
const scene=render(source),full=individual(scene);
const points=text=>text.trim().split(/\s+/).map(pair=>{const p=pair.split(',').map(Number);assert.equal(p.length,2);assert.ok(p.every(Number.isFinite));return {x:p[0],y:p[1]};});
const serial=ps=>ps.map(p=>`${p.x},${p.y}`).join(' ');
function tsv(file,convert) {
  const map=new Map();for(const line of fs.readFileSync(file,'utf8').trim().split('\n')) {
    const [id,...fields]=line.split('\t');assert.ok(!map.has(id));map.set(id,convert(fields));
  }return map;
}
const positions=file=>tsv(file,fields=>{assert.equal(fields.length,2);const [x,y]=fields.map(Number);assert.ok([x,y].every(Number.isFinite));return {x,y};});
const routes=file=>tsv(file,fields=>{assert.equal(fields.length,1);const p=points(fields[0]);assert.equal(p.length,2);return p;});
const mainPositions=positions(proposalFile),memberPositions=positions(proposalFile+'.individual');
const mainRoutes=routes(proposalFile+'.routes.tsv'),memberRoutes=routes(proposalFile+'.individual.routes.tsv');
for(const [map,ids] of [[mainPositions,product.getRenderedConnectionTables(scene).map(n=>n.modelId)],
  [memberPositions,scene.tables.map(n=>n.modelId)],[mainRoutes,scene.edges.map(e=>e.edgeId)],[memberRoutes,full.edges.map(e=>e.edgeId)]])
  assert.deepEqual(new Set(map.keys()),new Set(ids));
const stats=read(proposalFile+'.stats.json'),policy=read(proposalFile+'.policy.json');
assert.equal(stats.proposalAuthority,'external-policy');assert.equal(stats.heuristicSearchCalls,0);
assert.equal(policy.untrainedControl,false);assert.equal(policy.checkpointSha256,digest(policy.checkpoint));assert.deepEqual(policy.final,stats);
assert.ok(!(stats.overviewOnly||policy.overviewOnly)||experimental,'isolated overview objective cannot be promoted as a shared-layout candidate');
assert.ok(!(stats.allowNeutral||policy.allowNeutral)||experimental,'neutral exploration requires an isolated experiment');
if(stats.areaExploration)assert.ok(experimental&&allowAreaGrowth&&stats.temporaryRegressionBudget===200
  &&stats.visual<=stats.initialVisual+stats.temporaryRegressionBudget,'bounded spacing exploration cannot be promoted directly');
if(allowAreaGrowth)assert.ok(stats.globalScale&&stats.bboxLimit>0&&stats.bboxLimit<=1.5e9,'only a bounded neural global action can grow the layout');
const candidate=structuredClone(source),changedNodes=[],changedRoutes=[];
for(const n of candidate.nodes) {
  const p=memberPositions.get(n.modelId),position={x:p.x-n.size.width/2,y:p.y-n.size.height/2};
  if(Math.hypot(position.x-n.position.x,position.y-n.position.y)>1e-6)changedNodes.push({modelId:n.modelId,before:n.position,after:position});
  n.position=position;
}
const sourceNodes=new Map(source.nodes.map(n=>[n.modelId,n])),candidateNodes=new Map(candidate.nodes.map(n=>[n.modelId,n]));
for(const card of scene.leafCards) {
  const changes=card.memberModelIds.map(id=>{const old=sourceNodes.get(id),next=candidateNodes.get(id);return {x:next.position.x-old.position.x,y:next.position.y-old.position.y};});
  for(const p of changes)assert.ok(Math.hypot(p.x-changes[0].x,p.y-changes[0].y)<1e-6,'Leaf members must translate rigidly');
}
for(const e of candidate.routedEdges) {
  const p=memberRoutes.get(e.edgeId);assert.ok(p);
  if(serial(p)!==serial(e.points))changedRoutes.push(e.edgeId);
  e.points=p;
}
for(const group of candidate.engineMetadata.renderedCarrierRoutes) {
  const member=group.memberEdgeIds.map(id=>({id,points:memberRoutes.get(id)})).sort((a,b)=>
    Math.hypot(a.points[0].x-a.points[1].x,a.points[0].y-a.points[1].y)-Math.hypot(b.points[0].x-b.points[1].x,b.points[0].y-b.points[1].y)||a.id.localeCompare(b.id))[0];
  group.points=member.points;
}
assert.deepEqual(candidate.engineMetadata.leafBundles,source.engineMetadata.leafBundles);
assert.deepEqual(candidate.engineMetadata.renderedCarrierRoutes.map(g=>[g.carrierId,g.memberEdgeIds]),source.engineMetadata.renderedCarrierRoutes.map(g=>[g.carrierId,g.memberEdgeIds]));
const actual=render(candidate),actualFull=individual(actual);
assert.deepEqual(actual.leafCards,scene.leafCards);
assert.equal(actual.edges.length,scene.edges.length);assert.equal(actualFull.edges.length,full.edges.length);
const membership=model=>new Map(model.edges.map(e=>[e.edgeId,e.memberEdgeIds.slice().sort()]));
assert.deepEqual(membership(actual),membership(scene));
for(const [model,expected] of [[actual,mainRoutes],[actualFull,memberRoutes]])
  {
    const boxes=new Map(product.getRenderedConnectionTables(model).map(n=>[n.modelId,n]));
    for(const edge of model.edges) {
      const ps=points(edge.points);assert.equal(serial(ps),serial(expected.get(edge.edgeId)),edge.edgeId);
      const ids=[edge.leafCardEndpointIds?.[0]||edge.sourceModelId,edge.leafCardEndpointIds?.[1]||edge.targetModelId];
      for(let k=0;k<2;k++) {
        const n=boxes.get(ids[k]),p=ps[k];assert.ok(n);
        const l=n.position.x,r=l+n.size.width,t=n.position.y,b=t+n.size.height;
        assert.ok(p.x>=l-.02&&p.x<=r+.02&&p.y>=t-.02&&p.y<=b+.02);
        assert.ok(Math.min(Math.abs(p.x-l),Math.abs(p.x-r),Math.abs(p.y-t),Math.abs(p.y-b))<.02,'every canonical endpoint stays on its real card');
      }
    }
  }
for(const table of product.getRenderedConnectionTables(actual)) {
  const p=mainPositions.get(table.modelId),center={x:table.position.x+table.size.width/2,y:table.position.y+table.size.height/2};
  assert.ok(Math.hypot(center.x-p.x,center.y-p.y)<1e-6,JSON.stringify({modelId:table.modelId,product:center,native:p,size:table.size}));
}
const baseline=product.measureRenderedVisualConflicts(scene),metrics=product.measureRenderedVisualConflicts(actual);
const originalIndividual=product.measureRenderedVisualConflicts(full),candidateIndividual=product.measureRenderedVisualConflicts(actualFull);
const clearance=product.measureRenderedTableClearance(actual),memberClearance=product.measureRenderedTableClearance(actualFull);
assert.equal(stats.initialVisual,baseline.visualCrossings);assert.equal(stats.visual,metrics.visualCrossings);
assert.equal(stats.initialIndividualVisual,originalIndividual.visualCrossings);assert.equal(stats.individualVisual,candidateIndividual.visualCrossings);
const dominatesSource=metrics.visualCrossings<=baseline.visualCrossings&&candidateIndividual.visualCrossings<=originalIndividual.visualCrossings
  &&(metrics.visualCrossings<baseline.visualCrossings||candidateIndividual.visualCrossings<originalIndividual.visualCrossings);
if(!experimental)assert.ok(dominatesSource,'strict improvement in at least one view, with neither view regressing');
assert.ok(stats.hardConditions<=stats.initialHardConditions&&stats.individualHardConditions<=stats.initialIndividualHardConditions);
assert.equal(clearance.spacingViolations,0);assert.equal(memberClearance.spacingViolations,0);
assert.ok(clearance.bboxArea<=1.5e9&&memberClearance.bboxArea<=1.5e9,'both views must fit the product area budget');
assert.ok(clearance.bboxArea<=(allowAreaGrowth?stats.bboxLimit:product.measureRenderedTableClearance(scene).bboxArea)+.01);
candidate.engineMetadata={...candidate.engineMetadata,...metrics,boundingBoxArea:clearance.bboxArea,
  strategy:'learned-component-policy',actualAlgorithm:policy.modelKind==='joint-node-perimeter-network'?'JointNodePerimeterNetwork(frozen card and endpoint outputs)':policy.modelKind==='coordinated-displacement-network'?'CoordinatedDisplacementNetwork(frozen joint translations, direct straight ports)':stats.pairActions?'NeuralPairRanker(exact card-center swaps)':'ConditionalMixtureDensityPolicy(rigid components, direct straight ports)',
  researchCandidateStatus:experimental?'experimental-not-promoted':'verified-source-improvement'};
const candidateFile=path.join(directory,'candidate.layout.json');assert.ok(!fs.existsSync(candidateFile),'preserve existing candidate');
fs.writeFileSync(candidateFile,JSON.stringify(candidate)+'\n');
const report={sourceFile,candidateFile,sourceSha256:digest(sourceFile),candidateSha256:digest(candidateFile),baseline,metrics,
  originalIndividual,candidateIndividual,changedNodes,changedRoutes,sizesPreserved:true,membershipsPreserved:true,
  rigidLeafGeometryPreserved:true,productRouteParity:true,nativeProposal:stats,learnedPolicy:policy,browserVerified:false,
  experimental,dominatesSource,allowAreaGrowth,bboxArea:clearance.bboxArea,individualBboxArea:memberClearance.bboxArea};
fs.writeFileSync(path.join(directory,'proposal.audit.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({...report,changedNodes:changedNodes.length,changedRoutes:changedRoutes.length}));
