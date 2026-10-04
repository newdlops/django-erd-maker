// Refresh the preserved layout against today's analyzer graph. Known card
// positions stay fixed; new cards fill free space near an existing neighbour.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const {consolidateEdges} = require('../../out/extension/services/layout/consolidateEdges.js');
const product = require('../../out/webview/state/createDiagramRenderModel.js');
const {decodeLayoutSnapshot} = require('../../out/shared/protocol/decodeDiagramBootstrap.js');
const [sourceFile, payloadFile, outputStem] = process.argv.slice(2);
assert.ok(outputStem, 'source.layout.json current-payload.json output-stem');
const source = JSON.parse(fs.readFileSync(sourceFile));
const payload = JSON.parse(fs.readFileSync(payloadFile));
// These mappings reuse a spatial slot only. Current analyzer identities and
// relationships are authoritative; no old model or edge is relabelled into it.
const slotReuse = {'db.Rsu':'db.RsuRsa', 'db.InvestmentContractDocument':'db.InvestmentAgreementDocument',
  'db.InvestmentContractConsentSummary':'db.RtccConsentSummary', 'db.RtccAnalysisRun':'db.RtccSummaryAnalysisRequest'};
const rename = id => slotReuse[id] || id;
const sceneTables = product.createDiagramRenderModel(payload).tables;
const currentTables = new Map(sceneTables.map(n => [n.modelId, n]));
const canonical = consolidateEdges(payload.graph.structuralEdges).layoutEdges.filter(e => e.sourceModelId !== e.targetModelId);
const key = (a,b) => JSON.stringify([a,b].sort());
const currentByPair = new Map(canonical.map(e => [key(e.sourceModelId,e.targetModelId),e]));
const nodes = source.nodes.filter(n => currentTables.has(rename(n.modelId))).map(n => ({...structuredClone(n), modelId:rename(n.modelId)}));
const byId = new Map(nodes.map(n => [n.modelId,n]));
const bundles = source.engineMetadata.leafBundles.map(b => ({...structuredClone(b),
  parentModelId:rename(b.parentModelId), leafModelIds:b.leafModelIds.map(rename).filter(id => byId.has(id)),
  sharedRootModelIds:(b.sharedRootModelIds||[]).map(rename).filter(id => byId.has(id))}));
const owner = new Map(bundles.flatMap((b,i) => b.leafModelIds.map(id => [id,i])));
const table = n => ({...n,size:currentTables.get(n.modelId).size});
const bounds = b => {
  const ns=b.leafModelIds.map(id => table(byId.get(id)));
  const x=Math.min(...ns.map(n=>n.position.x))-24, y=Math.min(...ns.map(n=>n.position.y))-24;
  return {position:{x,y},size:{width:Math.max(...ns.map(n=>n.position.x+n.size.width))+24-x,
    height:Math.max(...ns.map(n=>n.position.y+n.size.height))+24-y}};
};
const center = n => ({x:n.position.x+n.size.width/2,y:n.position.y+n.size.height/2});
const overlaps = (a,b,gap=42) => a.position.x < b.position.x+b.size.width+gap && a.position.x+a.size.width+gap > b.position.x
  && a.position.y < b.position.y+b.size.height+gap && a.position.y+a.size.height+gap > b.position.y;
const neighbours = new Map(payload.graph.nodes.map(n=>[n.modelId,[]]));
for(const e of canonical) {neighbours.get(e.sourceModelId).push(e.targetModelId);neighbours.get(e.targetModelId).push(e.sourceModelId);}
const additions = [];
const pending = sceneTables.filter(n=>!byId.has(n.modelId));
while(pending.length) {
  pending.sort((a,b)=>neighbours.get(b.modelId).filter(id=>byId.has(id)).length-neighbours.get(a.modelId).filter(id=>byId.has(id)).length || a.modelId.localeCompare(b.modelId));
  const n=pending.shift(), peers=neighbours.get(n.modelId).filter(id=>byId.has(id));
  peers.sort((a,b)=>neighbours.get(a).length-neighbours.get(b).length || a.localeCompare(b));
  const anchor=byId.get(peers[0]) || nodes[0], ac=center(table(anchor));
  const node={modelId:n.modelId,size:{...n.size},position:{x:0,y:0},clusterId:anchor.clusterId};
  let chosen;
  // A new degree-one leaf may occupy unused space in its existing parent card.
  const bundleIndex=neighbours.get(n.modelId).length===1 ? bundles.findIndex(b=>b.parentModelId===peers[0]) : -1;
  if(bundleIndex>=0) {
    const box=bounds(bundles[bundleIndex]);
    for(let y=box.position.y+24;y+node.size.height<=box.position.y+box.size.height-24&&!chosen;y+=116) {
      for(let x=box.position.x+24;x+node.size.width<=box.position.x+box.size.width-24&&!chosen;x+=292) {
        const candidate={...node,position:{x,y}};
        if(nodes.every(other=>!overlaps(candidate,table(other)))) chosen=candidate;
      }
    }
  }
  if(chosen) {bundles[bundleIndex].leafModelIds.push(node.modelId);owner.set(node.modelId,bundleIndex);}
  else {
    const obstacles=[...nodes.filter(n=>!owner.has(n.modelId)).map(table),...bundles.map(bounds)];
    const candidates=[];
    for(let ring=1;ring<=24;ring++) {
      for(let ix=-ring;ix<=ring;ix++) for(let iy=-ring;iy<=ring;iy++) {
        if(Math.max(Math.abs(ix),Math.abs(iy))!==ring) continue;
        const candidate={...node,position:{x:ac.x+ix*440-node.size.width/2,y:ac.y+iy*300-node.size.height/2}};
        if(obstacles.some(other=>overlaps(candidate,other))) continue;
        const c=center(candidate);
        const distance=peers.reduce((sum,id)=>{const p=center(table(byId.get(id)));return sum+Math.hypot(c.x-p.x,c.y-p.y)/Math.sqrt(neighbours.get(id).length);},0);
        candidates.push({node:candidate,distance});
      }
      if(candidates.length>=30) break;
    }
    assert.ok(candidates.length,n.modelId);
    candidates.sort((a,b)=>a.distance-b.distance);
    chosen=candidates[0].node;
  }
  nodes.push(chosen);byId.set(chosen.modelId,chosen);
  additions.push({modelId:chosen.modelId,position:chosen.position,anchor:anchor.modelId,leafCard:owner.get(chosen.modelId)});
}
// The renamed document gained 20px of height. Its slot is in the top row of
// Company's 2x2 leaf card: move it upward to retain the 42px gap below it.
const slotAdjustments=[{modelId:'db.InvestmentAgreementDocument',dx:0,dy:-20}];
for(const adjustment of slotAdjustments) {
  const n=byId.get(adjustment.modelId);
  n.position.x+=adjustment.dx;n.position.y+=adjustment.dy;
}
const boundary = (n,p) => {
  const c=center(n),dx=p.x-c.x,dy=p.y-c.y;
  const t=Math.min(dx ? n.size.width/2/Math.abs(dx) : Infinity,dy ? n.size.height/2/Math.abs(dy) : Infinity);
  return {x:Math.round((c.x+dx*t)*100)/100,y:Math.round((c.y+dy*t)*100)/100};
};
const routeRename = new Map(), retained = new Map();
for(const old of source.routedEdges) {
  const e=currentByPair.get(key(rename(old.sourceModelId),rename(old.targetModelId)));
  if(!e) continue;
  routeRename.set(old.edgeId,e.id);
  retained.set(e.id,{...old,edgeId:e.id,sourceModelId:e.sourceModelId,targetModelId:e.targetModelId,
    points:rename(old.sourceModelId)===e.sourceModelId?old.points:[...old.points].reverse()});
}
const routes=canonical.map(e=>retained.get(e.id)||{edgeId:e.id,crossingIds:[],sourceModelId:e.sourceModelId,targetModelId:e.targetModelId,
  points:[boundary(table(byId.get(e.sourceModelId)),center(table(byId.get(e.targetModelId)))),
    boundary(table(byId.get(e.targetModelId)),center(table(byId.get(e.sourceModelId))))]});
const onBoundary=(point,n)=>{
  const left=n.position.x,top=n.position.y,right=left+n.size.width,bottom=top+n.size.height;
  return point.x>=left-.02&&point.x<=right+.02&&point.y>=top-.02&&point.y<=bottom+.02
    &&Math.min(Math.abs(point.x-left),Math.abs(point.x-right),Math.abs(point.y-top),Math.abs(point.y-bottom))<.02;
};
let reattachedRoutes=0;
for(const route of routes) {
  const a=table(byId.get(route.sourceModelId)),b=table(byId.get(route.targetModelId));
  // Degree and label changes can enlarge a catalog card. An old endpoint
  // inside its new rectangle is not a valid boundary port.
  if(!onBoundary(route.points[0],a)||!onBoundary(route.points[1],b)) {
    route.points=[boundary(a,center(b)),boundary(b,center(a))];reattachedRoutes++;
  }
}
const routeById=new Map(routes.map(e=>[e.edgeId,e]));
const groups=new Map(), represented=new Set(), groupByEdge=new Map();
for(const g of source.engineMetadata.renderedCarrierRoutes) {
  const members=g.memberEdgeIds.map(id=>routeRename.get(id)).filter(id=>id&&!represented.has(id));
  if(!members.length)continue;
  const id=g.carrierId.startsWith('june-')?g.carrierId:members[0];
  groups.set(id,members);
  for(const m of members){represented.add(m);groupByEdge.set(m,id);}
}
const leafGroup=new Map();
for(const route of routes) {
  const group=groupByEdge.get(route.edgeId);
  if(!group?.startsWith('june-leaf:'))continue;
  for(const id of [route.sourceModelId,route.targetModelId]) if(owner.has(id))leafGroup.set(owner.get(id),group);
}
for(const e of canonical) {
  if(represented.has(e.id))continue;
  const leaf=[owner.get(e.sourceModelId),owner.get(e.targetModelId)].find(i=>i!==undefined&&leafGroup.has(i));
  const hub=e.kind==='inheritance'?undefined:[byId.get(e.sourceModelId).clusterId,byId.get(e.targetModelId).clusterId]
    .filter((id,_,ids)=>ids[0]!==ids[1]&&groups.has('june-hub:'+id))
    .sort((a,b)=>groups.get('june-hub:'+b).length-groups.get('june-hub:'+a).length)[0];
  const id=leaf!==undefined?leafGroup.get(leaf):hub?'june-hub:'+hub:e.id;
  if(!groups.has(id))groups.set(id,[]);
  groups.get(id).push(e.id);represented.add(e.id);
}
assert.equal(represented.size,canonical.length);
const metadata={...source.engineMetadata,leafBundles:bundles,renderedCarrierRoutes:[...groups].map(([carrierId,memberEdgeIds])=>({carrierId,memberEdgeIds,points:routeById.get(memberEdgeIds[0]).points})),strategy:'preserved-leaf-layout-current-graph'};
const layout={...source,nodes,routedEdges:routes,engineMetadata:metadata};
// Bake the current catalog dimensions and any reattached ports, so browser
// geometry and the saved snapshot agree after model names/degrees change.
let scene=product.createDiagramRenderModel({...payload,layout,view:{...payload.view,tableOptions:[]}});
assert.ok(scene.relationshipOverview);
layout.nodes=layout.nodes.map(n=>({...n,size:scene.tables.find(t=>t.modelId===n.modelId).size}));
layout.routedEdges=scene.leafCardOverview.individualEdges.map(e=>({...routeById.get(e.edgeId),points:e.points.split(' ').map(p=>{const [x,y]=p.split(',').map(Number);return{x,y};})}));
scene=product.createDiagramRenderModel({...payload,layout,view:{...payload.view,tableOptions:[]}});
const metrics=product.measureRenderedVisualConflicts(scene),clearance=product.measureRenderedTableClearance(scene);
layout.engineMetadata={...metadata,...metrics,boundingBoxArea:clearance.bboxArea,visualCrossingsScope:'rendered-leaf-card-connections-v1'};
const output=outputStem+'.layout.json';
decodeLayoutSnapshot(layout);
fs.writeFileSync(output,JSON.stringify(layout));
const report={output,...metrics,bboxB:clearance.bboxArea/1e9,spacingViolations:clearance.spacingViolations,
  models:scene.tables.length,canonicalRelationships:canonical.length,groups:scene.relationshipOverview.groupCount,leafCards:scene.leafCards.length,
  leaves:scene.leafCards.reduce((sum,c)=>sum+c.memberModelIds.length,0),reattachedRoutes,slotReuse,slotAdjustments,additions};
fs.writeFileSync(outputStem+'.refresh.json',JSON.stringify(report,null,2));
console.log(JSON.stringify(report));
