// Node.js PoC: load v12 ONNX, run inference, time it.
// Run: node scripts/test-onnx-inference.mjs
import * as ort from "onnxruntime-node";
import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";

const MODEL = "data/erd-poc/checkpoints/v12-fnv.onnx";
const BASELINE = "data/erd-poc/layouts/real-main.json";
const EXPERT = "data/erd-poc/expert-strong/real-main.json";
const EDGES_TSV = "data/erd-poc/graphs/real-main/edges.tsv";

const EDGE_KIND_VOCAB = ["foreign_key", "many_to_many",
                         "one_to_one", "inheritance"];

function buildInputs(layout, expert) {
  const nodes = layout.nodes;
  const n = nodes.length;
  const idToIdx = new Map(nodes.map((nd, i) => [nd.modelId, i]));
  // Centers
  const cxArr = new Float32Array(n);
  const cyArr = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    cxArr[i] = nodes[i].position.x + nodes[i].size.width / 2;
    cyArr[i] = nodes[i].position.y + nodes[i].size.height / 2;
  }
  // Expert frame normalization (matches apply_distill_real.py)
  const expCx = expert.nodes.map(nd => nd.position.x + nd.size.width / 2);
  const expCy = expert.nodes.map(nd => nd.position.y + nd.size.height / 2);
  const expMx = expCx.reduce((a, b) => a + b, 0) / expCx.length;
  const expMy = expCy.reduce((a, b) => a + b, 0) / expCy.length;
  // std of expert positions (combined xy as in PyTorch: t.std())
  const expCombined = [...expCx, ...expCy];
  const expMeanFlat = expCombined.reduce((a, b) => a + b, 0)
                     / expCombined.length;
  // PyTorch t.std() uses unbiased=True (default). Mimic:
  const expVar = expCombined.reduce((s, v) =>
    s + (v - expMeanFlat) ** 2, 0) / (expCombined.length - 1);
  const expStd = Math.sqrt(expVar) + 1e-3;
  // Baseline normalized to expert frame: (cx - expMx)/expStd
  const baseline = new Float32Array(n * 2);
  for (let i = 0; i < n; i++) {
    baseline[i * 2] = (cxArr[i] - expMx) / expStd;
    baseline[i * 2 + 1] = (cyArr[i] - expMy) / expStd;
  }

  // Edges from layout.routedEdges
  const routed = layout.routedEdges || [];
  const clusterById = new Map();
  for (const nd of nodes) {
    clusterById.set(nd.modelId, nd.clusterId || "");
  }
  const appLabels = nodes.map(nd => nd.modelId.split(".")[0]);
  const deg = new Int32Array(n);
  const interClusterDeg = new Int32Array(n);
  const edgePairs = [];
  const edgeAttrs = [];
  for (const re of routed) {
    const s = idToIdx.get(re.sourceModelId);
    const t = idToIdx.get(re.targetModelId);
    if (s === undefined || t === undefined || s === t) continue;
    edgePairs.push([s, t]);
    edgePairs.push([t, s]);
    deg[s]++; deg[t]++;
    const kindIdx = EDGE_KIND_VOCAB.indexOf(re.kind || "foreign_key");
    const kindOh = [0, 0, 0, 0];
    kindOh[Math.max(0, kindIdx)] = 1;
    const intra = appLabels[s] === appLabels[t] ? 1 : 0;
    const sc = clusterById.get(re.sourceModelId);
    const tc = clusterById.get(re.targetModelId);
    if (sc && tc && sc !== tc) {
      interClusterDeg[s]++;
      interClusterDeg[t]++;
    }
    edgeAttrs.push([...kindOh, intra, 1]);
    edgeAttrs.push([...kindOh, intra, 0]);
  }
  const E = edgePairs.length;
  const edgeIndex = new BigInt64Array(E * 2);
  for (let i = 0; i < E; i++) {
    edgeIndex[i] = BigInt(edgePairs[i][0]);  // source row
    edgeIndex[E + i] = BigInt(edgePairs[i][1]);  // target row
  }
  const edgeAttrFlat = new Float32Array(E * 6);
  for (let i = 0; i < E; i++) {
    for (let j = 0; j < 6; j++) {
      edgeAttrFlat[i * 6 + j] = edgeAttrs[i][j];
    }
  }
  // Leaf bundles from layout.engineMetadata
  const isBundleParent = new Float32Array(n);
  const isBundleLeaf = new Float32Array(n);
  const bundleSize = new Float32Array(n);
  const leafBundles = (layout.engineMetadata || {}).leafBundles || [];
  for (const b of leafBundles) {
    const pid = b.parentModelId;
    const pi = idToIdx.get(pid);
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

  // Node features: 10 dims
  const x = new Float32Array(n * 10);
  for (let i = 0; i < n; i++) {
    const w = nodes[i].size.width;
    const h = nodes[i].size.height;
    const dgi = deg[i];
    const icd = interClusterDeg[i];
    const features = [
      Math.log(dgi + 1),                // log_deg
      dgi >= 10 ? 1 : 0,                 // is_hub
      dgi === 1 ? 1 : 0,                 // is_leaf
      Math.log(w + 1) - 5.0,
      Math.log(h + 1) - 4.0,
      isBundleParent[i],
      isBundleLeaf[i],
      Math.log(bundleSize[i] + 1),       // log_bundle_size
      icd > 0 ? 1 : 0,                   // is_cluster_boundary
      Math.log(icd + 1),                // log_inter_cluster_deg
    ];
    for (let j = 0; j < 10; j++) x[i * 10 + j] = features[j];
  }
  // app_idx
  function hash16(s) {
    // FNV-1a 32-bit (must match Python's app_idx_for_label exactly).
    // Use Math.imul to keep 32-bit signed integer multiplication;
    // plain `*` loses precision at intermediate magnitudes (e.g. 0x811c9dc5
    // × 0x01000193 ≈ 2^61 — beyond JS double's 2^53 mantissa).
    let h = 0x811c9dc5 | 0;
    for (let i = 0; i < s.length; i++) {
      h = Math.imul(h ^ s.charCodeAt(i), 0x01000193);
    }
    return ((h >>> 0) % 16);
  }
  const appIdx = new BigInt64Array(n);
  for (let i = 0; i < n; i++) {
    appIdx[i] = BigInt(hash16(appLabels[i]));
  }

  return {
    tensors: {
      x: new ort.Tensor("float32", x, [n, 10]),
      app_idx: new ort.Tensor("int64", appIdx, [n]),
      baseline: new ort.Tensor("float32", baseline, [n, 2]),
      edge_index: new ort.Tensor("int64", edgeIndex, [2, E]),
      edge_attr: new ort.Tensor("float32", edgeAttrFlat, [E, 6]),
    },
    n, E,
    posMean: [expMx, expMy],
    posStd: expStd,
  };
}

async function main() {
  console.log("loading layout + expert...");
  const layout = JSON.parse(readFileSync(BASELINE, "utf8"));
  const expert = JSON.parse(readFileSync(EXPERT, "utf8"));
  const { tensors, n, E, posMean, posStd } = buildInputs(layout, expert);
  console.log(`inputs: N=${n} E=${E / 2}`);
  console.log(`pos_mean=[${posMean[0].toFixed(1)},${posMean[1].toFixed(1)}]`
              + ` pos_std=${posStd.toFixed(1)}`);

  console.log(`loading ONNX session...`);
  const t0 = performance.now();
  const session = await ort.InferenceSession.create(MODEL, {
    intraOpNumThreads: 4,
  });
  console.log(`  session loaded in ${(performance.now() - t0).toFixed(0)}ms`);
  console.log(`  inputs: ${session.inputNames.join(", ")}`);
  console.log(`  outputs: ${session.outputNames.join(", ")}`);

  // Warm up
  await session.run(tensors);

  // Benchmark
  const N = 10;
  const times = [];
  for (let i = 0; i < N; i++) {
    const s = performance.now();
    const out = await session.run(tensors);
    times.push(performance.now() - s);
  }
  times.sort((a, b) => a - b);
  const mean = times.reduce((a, b) => a + b, 0) / N;
  console.log(`inference: mean=${mean.toFixed(1)}ms `
              + `min=${times[0].toFixed(1)}ms max=${times[N-1].toFixed(1)}ms`);

  // Inspect output
  const out = await session.run(tensors);
  const positions = out.positions.data;
  console.log(`output shape: ${out.positions.dims}`);
  // Denormalize first few positions to real coords
  for (let i = 0; i < Math.min(3, n); i++) {
    const xn = positions[i * 2];
    const yn = positions[i * 2 + 1];
    const xr = xn * posStd + posMean[0];
    const yr = yn * posStd + posMean[1];
    console.log(`  node[${i}]: norm=(${xn.toFixed(4)},${yn.toFixed(4)}) `
                + `real=(${xr.toFixed(1)},${yr.toFixed(1)})`);
  }
}

main().catch(e => { console.error(e); process.exit(1); });
