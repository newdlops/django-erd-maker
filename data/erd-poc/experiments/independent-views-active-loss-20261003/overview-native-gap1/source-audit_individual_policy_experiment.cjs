// An isolated individual-view benchmark, not a shared Leaf-layout candidate.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const [sourceFile,payloadFile,directory]=process.argv.slice(2);
assert.ok(path.resolve(directory).startsWith(path.resolve(__dirname,'../../.tmp')+path.sep));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const product=require(path.resolve(__dirname,'../../out/webview/state/createDiagramRenderModel.js'));
const original=read(sourceFile),payload=read(payloadFile),proposal=path.join(directory,'learned.tsv');
const stats=read(proposal+'.stats.json'),policy=read(proposal+'.policy.json');
assert.equal(policy.untrainedControl,false);assert.equal(policy.checkpointSha256,digest(policy.checkpoint));assert.equal(stats.heuristicSearchCalls,0);
const positions=new Map(fs.readFileSync(proposal+'.individual','utf8').trim().split('\n').map(line=>{
  const [id,x,y]=line.split('\t');return [id,{x:Number(x),y:Number(y)}];}));
const routes=new Map(fs.readFileSync(proposal+'.individual.routes.tsv','utf8').trim().split('\n').map(line=>{
  const [id,text]=line.split('\t');return [id,text.split(' ').map(pair=>{const [x,y]=pair.split(',').map(Number);return {x,y};})];}));
const candidate=structuredClone(original);
assert.deepEqual(new Set(positions.keys()),new Set(candidate.nodes.map(n=>n.modelId)));
assert.deepEqual(new Set(routes.keys()),new Set(candidate.routedEdges.map(e=>e.edgeId)));
for(const n of candidate.nodes){const p=positions.get(n.modelId);n.position={x:p.x-n.size.width/2,y:p.y-n.size.height/2};}
for(const e of candidate.routedEdges)e.points=routes.get(e.edgeId);
candidate.engineMetadata={...candidate.engineMetadata,leafBundles:[],renderedCarrierRoutes:[],relationshipPresentation:'individual',
  strategy:'learned-independent-individual-experiment',researchCandidateStatus:'individual-view-experiment-not-promoted'};
const scene=product.createDiagramRenderModel({...payload,layout:candidate,view:{...payload.view,tableOptions:[]}});
assert.equal(scene.edges.length,1727);assert.equal(scene.tables.length,1244);assert.equal(scene.leafCards.length,0);
const tables=new Map(scene.tables.map(n=>[n.modelId,n]));
const serial=ps=>ps.map(p=>`${p.x},${p.y}`).join(' ');
let endpoints=0;
for(const edge of scene.edges) {
  const ps=routes.get(edge.edgeId);assert.equal(edge.points,serial(ps));
  for(const [k,id] of [edge.sourceModelId,edge.targetModelId].entries()) {
    const n=tables.get(id),p=ps[k],peer=ps[1-k],l=n.position.x,r=l+n.size.width,t=n.position.y,b=t+n.size.height,eps=.011;
    assert.ok(Number.isFinite(p.x)&&Number.isFinite(p.y)&&p.x>=l-eps&&p.x<=r+eps&&p.y>=t-eps&&p.y<=b+eps);
    const outward=(Math.abs(p.x-l)<=eps&&peer.x<=p.x+eps)||(Math.abs(p.x-r)<=eps&&peer.x>=p.x-eps)
      ||(Math.abs(p.y-t)<=eps&&peer.y<=p.y+eps)||(Math.abs(p.y-b)<=eps&&peer.y>=p.y-eps);
    assert.ok(outward&&Math.hypot(p.x-peer.x,p.y-peer.y)>eps,`outward-facing endpoint: ${edge.edgeId} / ${k}`);endpoints++;
  }
}
const metrics=product.measureRenderedVisualConflicts(scene),clearance=product.measureRenderedTableClearance(scene);
assert.equal(metrics.visualCrossings,stats.individualVisual);assert.equal(clearance.spacingViolations,0);
assert.ok(clearance.bboxArea<=1.5e9,'individual layout must stay within the product area budget');
if(stats.globalScale)assert.ok(stats.bboxLimit>0&&stats.bboxLimit<=1.5e9&&clearance.bboxArea<=stats.bboxLimit+.01);
if(stats.areaExploration)assert.ok(stats.globalScale&&stats.temporaryRegressionBudget===200
  &&stats.individualVisual<=stats.initialIndividualVisual+stats.temporaryRegressionBudget);
assert.equal(stats.hardConditions,0);assert.equal(stats.individualHardConditions,0);
candidate.engineMetadata={...candidate.engineMetadata,...metrics,boundingBoxArea:clearance.bboxArea};
const output=path.join(directory,'candidate.individual.layout.json');assert.ok(!fs.existsSync(output));fs.writeFileSync(output,JSON.stringify(candidate)+'\n');
const report={sourceFile,sourceSha256:digest(sourceFile),candidate:output,candidateSha256:digest(output),scope:'independent individual-view experiment',
  visualCrossings:metrics.visualCrossings,metrics,bboxArea:clearance.bboxArea,spacingViolations:clearance.spacingViolations,
  models:scene.tables.length,relations:scene.edges.length,validBoundaryEndpoints:endpoints,outwardBoundaryEndpointsVerified:true,allSizesAndRelationsPreserved:true,
  actualProductRendererVerified:true,browserVerified:false,sharedOverviewPreserved:false,promoted:false};
fs.writeFileSync(path.join(directory,'individual.audit.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
