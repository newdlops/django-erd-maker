// Build inputs in JS, print first few values to compare with Python
import { readFileSync } from "node:fs";

const layout = JSON.parse(readFileSync(
  "data/erd-poc/layouts/real-main.json", "utf8"));
const expert = JSON.parse(readFileSync(
  "data/erd-poc/expert-strong/real-main.json", "utf8"));

const EDGE_KIND_VOCAB = ["foreign_key", "many_to_many",
                         "one_to_one", "inheritance"];

const nodes = layout.nodes;
const n = nodes.length;
const idToIdx = new Map(nodes.map((nd, i) => [nd.modelId, i]));

// expert frame stats
const expCx = expert.nodes.map(nd => nd.position.x + nd.size.width / 2);
const expCy = expert.nodes.map(nd => nd.position.y + nd.size.height / 2);
const expMx = expCx.reduce((a, b) => a + b, 0) / expCx.length;
const expMy = expCy.reduce((a, b) => a + b, 0) / expCy.length;
const expCombined = [...expCx, ...expCy];
const expMeanFlat = expCombined.reduce((a, b) => a + b, 0)
                   / expCombined.length;
const expVar = expCombined.reduce((s, v) =>
  s + (v - expMeanFlat) ** 2, 0) / (expCombined.length - 1);
const expStd = Math.sqrt(expVar) + 1e-3;
console.log(`pos_mean=[${expMx.toFixed(4)}, ${expMy.toFixed(4)}]`);
console.log(`pos_std=${expStd.toFixed(4)}`);

// baseline normalized
console.log(`baseline[0:3]:`);
for (let i = 0; i < 3; i++) {
  const cx = nodes[i].position.x + nodes[i].size.width / 2;
  const cy = nodes[i].position.y + nodes[i].size.height / 2;
  console.log(`  [${((cx - expMx)/expStd).toFixed(4)}, `
              + `${((cy - expMy)/expStd).toFixed(4)}]`);
}

// degree + interClusterDeg
const routed = layout.routedEdges || [];
const deg = new Int32Array(n);
const interClusterDeg = new Int32Array(n);
const clusterById = new Map();
for (const nd of nodes) clusterById.set(nd.modelId, nd.clusterId || "");
const appLabels = nodes.map(nd => nd.modelId.split(".")[0]);
let edgeCount = 0;
for (const re of routed) {
  const s = idToIdx.get(re.sourceModelId);
  const t = idToIdx.get(re.targetModelId);
  if (s === undefined || t === undefined || s === t) continue;
  edgeCount++;
  deg[s]++; deg[t]++;
  const sc = clusterById.get(re.sourceModelId);
  const tc = clusterById.get(re.targetModelId);
  if (sc && tc && sc !== tc) {
    interClusterDeg[s]++; interClusterDeg[t]++;
  }
}
console.log(`forward edges: ${edgeCount}, edge_index will be 2x = ${edgeCount*2}`);

// Leaf bundles
const isBundleParent = new Float32Array(n);
const isBundleLeaf = new Float32Array(n);
const bundleSize = new Float32Array(n);
const leafBundles = (layout.engineMetadata || {}).leafBundles || [];
for (const b of leafBundles) {
  const pi = idToIdx.get(b.parentModelId);
  if (pi !== undefined) {
    isBundleParent[pi] = 1;
    bundleSize[pi] = (b.leafModelIds || []).length;
  }
  for (const lid of (b.leafModelIds || [])) {
    const li = idToIdx.get(lid);
    if (li !== undefined) {
      isBundleLeaf[li] = 1;
      bundleSize[li] = (b.leafModelIds || []).length;
    }
  }
}

// x features for first 3 nodes
console.log(`x[0:3]:`);
for (let i = 0; i < 3; i++) {
  const w = nodes[i].size.width;
  const h = nodes[i].size.height;
  const dgi = deg[i];
  const icd = interClusterDeg[i];
  const features = [
    Math.log(dgi + 1),
    dgi >= 10 ? 1 : 0,
    dgi === 1 ? 1 : 0,
    Math.log(w + 1) - 5.0,
    Math.log(h + 1) - 4.0,
    isBundleParent[i],
    isBundleLeaf[i],
    Math.log(bundleSize[i] + 1),
    icd > 0 ? 1 : 0,
    Math.log(icd + 1),
  ];
  console.log(`  [${features.map(f => f.toFixed(4)).join(", ")}]`);
}

// FNV hash for first label
function hash16(s) {
  let h = 0x811c9dc5 >>> 0;
  for (let i = 0; i < s.length; i++) {
    h = ((h ^ s.charCodeAt(i)) * 0x01000193) >>> 0;
  }
  return h % 16;
}
console.log(`app_idx[0..9] for first 10 labels:`);
console.log(`  ${appLabels.slice(0, 10).map(l => hash16(l)).join(", ")}`);
console.log(`  labels: ${appLabels.slice(0, 5).join(", ")}`);

// Compare edge_attr first row
console.log(`first edge: ${routed[0]?.sourceModelId} → ${routed[0]?.targetModelId}, `
            + `kind=${routed[0]?.kind}`);
