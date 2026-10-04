// Restore June-style leaf/hub memberships on an existing straight layout.
// This does not move cards, optimize ports, or remove canonical relationships.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const [layoutFile, payloadFile, membershipFile, outputFile] = process.argv.slice(2);
assert.ok(outputFile, 'layout.json payload.json memberships.json output.layout.json');
const layout = read(layoutFile), payload = read(payloadFile), membership = read(membershipFile);
const product = require(path.join(root, 'out/webview/state/createDiagramRenderModel.js'));
const {consolidateEdges} = require(path.join(root, 'out/extension/services/layout/consolidateEdges.js'));
const structural = new Map(payload.graph.structuralEdges.map(edge => [edge.id, edge]));
const pairKey = edge => JSON.stringify([edge.sourceModelId, edge.targetModelId].sort());
const canonical = consolidateEdges(payload.graph.structuralEdges).layoutEdges
  .filter(edge => edge.sourceModelId !== edge.targetModelId);
const canonicalByPair = new Map(canonical.map(edge => [pairKey(edge), edge]));
const canonicalIdRenames = [];
layout.routedEdges = layout.routedEdges.map(route => {
  const previous = structural.get(route.edgeId);
  assert.ok(previous);
  const edge = canonicalByPair.get(pairKey(previous));
  assert.ok(edge);
  if (route.edgeId !== edge.id) canonicalIdRenames.push({from:route.edgeId,to:edge.id});
  return {...route, edgeId:edge.id, sourceModelId:edge.sourceModelId, targetModelId:edge.targetModelId,
    points:previous.sourceModelId === edge.sourceModelId ? route.points : [...route.points].reverse()};
});
assert.deepEqual(new Set(layout.routedEdges.map(route => route.edgeId)), new Set(canonical.map(edge => edge.id)));
const nodeById = new Map(layout.nodes.map(node => [node.modelId, node]));
const leafToBundle = new Map();
membership.leafBundles.forEach((bundle, index) => {
  for (const id of bundle.leafModelIds) if (nodeById.has(id)) leafToBundle.set(id, index);
});
const sums = new Map();
for (const node of layout.nodes) {
  if (!node.clusterId) continue;
  const sum = sums.get(node.clusterId) ?? {x: 0, y: 0, count: 0};
  sum.x += node.position.x + node.size.width / 2;
  sum.y += node.position.y + node.size.height / 2;
  sum.count++;
  sums.set(node.clusterId, sum);
}
function cluster(id) {
  const node = nodeById.get(id);
  if (!node || node.clusterId) return node?.clusterId;
  let best, distance = Infinity;
  for (const [key, sum] of sums) {
    const d = (node.position.x + node.size.width / 2 - sum.x / sum.count) ** 2
      + (node.position.y + node.size.height / 2 - sum.y / sum.count) ** 2;
    if (d < distance) { best = key; distance = d; }
  }
  return best;
}
const pairs = new Map(), counts = new Map();
for (const route of layout.routedEdges) {
  const edge = structural.get(route.edgeId);
  assert.ok(edge, route.edgeId);
  if (edge.sourceModelId === edge.targetModelId || edge.kind === 'inheritance'
    || leafToBundle.has(edge.sourceModelId) || leafToBundle.has(edge.targetModelId)) continue;
  const source = cluster(edge.sourceModelId), target = cluster(edge.targetModelId);
  if (!source || !target || source === target) continue;
  pairs.set(edge.id, [source, target]);
  counts.set(source, (counts.get(source) ?? 0) + 1);
  counts.set(target, (counts.get(target) ?? 0) + 1);
}
const groups = new Map();
for (const route of layout.routedEdges) {
  const edge = structural.get(route.edgeId);
  const a = leafToBundle.get(edge.sourceModelId), b = leafToBundle.get(edge.targetModelId);
  const leaf = a === undefined ? b : b === undefined ? a : Math.min(a, b);
  let id = route.edgeId;
  if (leaf !== undefined) {
    // Incidental leaf relationships are retained in their overview group.
    // June dropped some of them; expansion here recovers every original line.
    id = 'june-leaf:' + leaf;
  } else if (pairs.has(edge.id)) {
    const [source, target] = pairs.get(edge.id);
    const sc = counts.get(source), tc = counts.get(target);
    if (Math.max(sc, tc) >= membership.hubThreshold) {
      id = 'june-hub:' + (sc > tc || (sc === tc && source < target) ? source : target);
    }
  }
  const members = groups.get(id) ?? [];
  members.push(route);
  groups.set(id, members);
}
const length = route => Math.hypot(route.points[1].x - route.points[0].x, route.points[1].y - route.points[0].y);
const result = structuredClone(layout);
result.engineMetadata = {
  ...result.engineMetadata,
  relationshipPresentation: 'june-bundled',
  renderedCarrierRoutes: [...groups].map(([id, members]) => {
    assert.ok(members.every(route => route.points.length === 2));
    members.sort((a, b) => length(a) - length(b) || a.edgeId.localeCompare(b.edgeId));
    return {carrierId: id, memberEdgeIds: members.map(route => route.edgeId), points: members[0].points};
  }),
};
const scene = product.createDiagramRenderModel({...payload, layout: result, view: {...payload.view, tableOptions: []}});
assert.ok(scene.relationshipOverview);
assert.equal(scene.relationshipOverview.individualEdges.length, result.routedEdges.length);
const metrics = product.measureRenderedVisualConflicts(scene), clearance = product.measureRenderedTableClearance(scene);
result.engineMetadata = {...result.engineMetadata, ...metrics, boundingBoxArea: clearance.bboxArea,
  visualCrossingsScope: 'rendered-june-bundled-overview-v1'};
fs.writeFileSync(outputFile, JSON.stringify(result));
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const report = {presentation: 'june-bundled', ...metrics, bboxB: clearance.bboxArea / 1e9,
  spacingViolations: clearance.spacingViolations, cards: scene.tables.length,
  canonicalRelationships: result.routedEdges.length, groups: scene.relationshipOverview.groupCount,
  individualVisualCrossings: scene.relationshipOverview.individualVisualCrossings,
  straight: scene.edges.every(edge => edge.points.split(/\s+/).length === 2),
  inputs: Object.fromEntries([layoutFile, payloadFile, membershipFile].map(file => [file, digest(file)])),
  output: outputFile, sha256: digest(outputFile), canonicalIdRenames, exactJuneReplay: false};
fs.writeFileSync(outputFile.replace(/\.layout\.json$/, '.audit.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report));
