// Additional exact ownership/projection data for the learned component policy.
// Run after probe_overview_boundary.cjs export --with-individual.
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const crypto = require('node:crypto');
const [sourceFile,payloadFile,directory] = process.argv.slice(2);
const product = require(path.resolve(__dirname,'../../out/webview/state/createDiagramRenderModel.js'));
const read = file => JSON.parse(fs.readFileSync(file,'utf8'));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const exported = read(path.join(directory,'export.json'));
assert.equal(sha(sourceFile),exported.sourceSha256);assert.equal(sha(payloadFile),exported.payloadSha256);
const payload=read(payloadFile),layout=read(sourceFile);
const scene=product.createDiagramRenderModel({...payload,layout,view:{...payload.view,tableOptions:[]}});
const cards=new Map(scene.leafCards.map(c=>[c.id,c.memberModelIds]));
const owner=new Map(scene.leafCards.flatMap(c=>c.memberModelIds.map(id=>[id,c.id])));
const individual=new Map(scene.leafCardOverview.individualEdges.map(e=>[e.edgeId,e]));
const tables=product.getRenderedConnectionTables(scene);
const components=tables.map(n=>[n.modelId,...(cards.get(n.modelId)||[n.modelId])]);
assert.deepEqual(new Set(components.flatMap(r=>r.slice(1))),new Set(scene.tables.map(n=>n.modelId)));
assert.equal(components.reduce((n,r)=>n+r.length-1,0),scene.tables.length);
const groups=scene.edges.map(e=>[e.edgeId,...e.memberEdgeIds]);
assert.equal(groups.reduce((n,r)=>n+r.length-1,0),individual.size);
// Every final line currently represents one June group. Leaf groups can also
// contain incidental non-parent relations; the environment rejects a move if
// a newly shortest representative would change that group's physical pair.
const memberKey = e => e.memberEdgeIds.slice().sort().join('\t');
assert.deepEqual(new Set(scene.edges.map(memberKey)),new Set(scene.leafCardOverview.baseEdges.map(memberKey)));
for(const [name,rows] of [['components.tsv',components],['groups.tsv',groups]])
  fs.writeFileSync(path.join(directory,name),rows.map(r=>r.join('\t')).join('\n')+'\n');
console.log(JSON.stringify({components:components.length,models:scene.tables.length,groups:groups.length,relations:individual.size}));
