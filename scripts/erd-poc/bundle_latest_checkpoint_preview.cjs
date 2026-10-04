const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {previewGraphSignature}=require('../../out/extension/services/layout/bundledMlPreview.js');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const base='data/erd-poc/candidates/captain-ml-latest-checkpoints';
const provenance=read(base+'.provenance.json'),audit=read(base+'.audit.json');
assert.equal(audit.actualProductFileLoadVerified,true);
assert.equal(audit.roundTripViewSwitchVerified,true);
assert.equal(sha(base+'.layout.json'),provenance.candidateSha256);
const layout=read(base+'.layout.json');
const payload=read('data/erd-poc/recovered/captain-2026-09-15-payload.json');
const folder='media/ml-preview';fs.mkdirSync(folder,{recursive:true});
fs.writeFileSync(path.join(folder,'layout.json'),JSON.stringify(layout)+'\n');
for(const view of ['overview','individual']){
  const cp=provenance.checkpoints[view];assert.equal(sha(cp.checkpoint),cp.checkpointSha256);
  fs.copyFileSync(cp.checkpoint,path.join(folder,view+'.npz'));
}
const manifest={schemaVersion:1,extensionVersion:read('package.json').version,
  graphSignature:previewGraphSignature({...payload,layout}),
  layoutSha256:sha(path.join(folder,'layout.json')),overviewVisual:audit.overviewVisual,individualVisual:audit.individualVisual,
  models:audit.models,canonicalRelationships:audit.canonicalRelationships,
  checkpoints:Object.fromEntries(Object.entries(provenance.checkpoints).map(([view,cp])=>[view,{
    path:view+'.npz',sha256:cp.checkpointSha256,trainedUpdates:cp.trainedUpdates,modelKind:cp.modelKind}])),
  sourceCandidateSha256:provenance.candidateSha256,latestNetworkForwardExported:true,coordinateSearchOrRepairs:0,
  browserVerified:false,thresholdsMet:false};
fs.writeFileSync(path.join(folder,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({version:manifest.extensionVersion,overview:manifest.overviewVisual,individual:manifest.individualVisual,
  layoutSha256:manifest.layoutSha256,graphSignature:manifest.graphSignature}));
