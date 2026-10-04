// Preserve the scored card/edge geometry and carry the layout actors' leaf
// memberships into the product protocol so the canvas can draw the containers.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const path = require('node:path');
const [sourceFile, compoundsFile, outputFile] = process.argv.slice(2);
assert.ok(outputFile, 'layout.json compounds.json output.layout.json');
assert.notEqual(path.resolve(sourceFile), path.resolve(outputFile));
const layout = JSON.parse(fs.readFileSync(sourceFile, 'utf8'));
const {compounds} = JSON.parse(fs.readFileSync(compoundsFile, 'utf8'));
const byId = new Map(layout.nodes.map(node => [node.modelId, node]));
const seen = new Set();
const bundles = compounds.map(compound => {
  const parent = byId.get(compound.parentModelId);
  assert.ok(parent);
  for (const member of compound.members) {
    const node = byId.get(member.modelId);
    assert.ok(node);
    assert.ok(!seen.has(member.modelId));
    seen.add(member.modelId);
    assert.deepEqual(node.size, member.size);
    for (const axis of ['x', 'y']) assert.ok(Math.abs(node.position[axis] - compound.position[axis] - member.offset[axis]) < .001);
  }
  return {parentModelId: compound.parentModelId, leafModelIds: compound.members.map(member => member.modelId),
    sharedRootModelIds: compound.sharedRootModelIds,
    bbox: {...compound.position, ...compound.size},
    anchor: {x: parent.position.x + parent.size.width / 2, y: parent.position.y + parent.size.height / 2}};
});
layout.engineMetadata = {...layout.engineMetadata, leafBundles: bundles};
fs.writeFileSync(outputFile, JSON.stringify(layout));
console.log(JSON.stringify({outputFile, groups: bundles.length, leaves: seen.size,
  cards: layout.nodes.length, relationships: layout.routedEdges.length,
  sha256: crypto.createHash('sha256').update(fs.readFileSync(outputFile)).digest('hex')}));
