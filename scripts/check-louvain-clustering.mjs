#!/usr/bin/env node
// Regression check: confirms the OGDF binary's clustered-layout fallback
// chain still reaches Louvain on a hub-dominant ERD fixture (the real-fixture
// snapshot we use as a stable reference for layout quality).
//
// Asserts:
//   - actualAlgorithm includes "ClusteredLayout"
//   - cluster count > 100 (Louvain produces ~459 on this fixture; if we drop
//     below 100 that means the chain bailed early to BFS-hub or single-cluster)
//   - edgeCrossings < 25000 (current Louvain result is ~16k; baseline before
//     Louvain was ~30k)
//   - strategyReason contains "Louvain"
//
// Exit codes:
//   0 on pass, non-zero on any failed assertion or runtime error.

import { execFile } from "node:child_process";
import { access } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..");

const fixtureDir = path.join(repoRoot, ".benchmarks", "real-fixture");
const nodesFile = path.join(fixtureDir, "nodes.tsv");
const edgesFile = path.join(fixtureDir, "edges.tsv");
const binary = path.join(repoRoot, "bin", "ogdf", `${process.platform}-${process.arch}`, "django-erd-ogdf-layout");

for (const file of [binary, nodesFile, edgesFile]) {
  try {
    await access(file);
  } catch {
    console.error(`[check-louvain] missing file: ${file}`);
    process.exit(2);
  }
}

const { stdout } = await execFileAsync(
  binary,
  [
    "layout",
    "--mode", "hierarchical_barycenter",
    "--nodes-file", nodesFile,
    "--edges-file", edgesFile,
    "--edge-routing", "straight",
  ],
  { maxBuffer: 200 * 1024 * 1024, timeout: 60_000 },
);

const payload = JSON.parse(stdout);
const meta = payload.engineMetadata ?? {};
const alg = String(meta.actualAlgorithm ?? "");
const reason = String(meta.strategyReason ?? "");
const xings = Number(meta.edgeCrossings ?? -1);
const clusters = Number((alg.match(/clusters=(\d+)/) ?? [])[1] ?? -1);

const failures = [];
if (!alg.includes("ClusteredLayout")) {
  failures.push(`actualAlgorithm should be a ClusteredLayout, got: ${alg}`);
}
if (!Number.isFinite(clusters) || clusters < 100) {
  failures.push(`expected cluster count > 100 (Louvain), got: ${clusters}`);
}
if (!Number.isFinite(xings) || xings >= 25_000) {
  failures.push(`expected edgeCrossings < 25000, got: ${xings}`);
}
if (!reason.includes("Louvain")) {
  failures.push(`strategyReason should mention Louvain, got: ${reason}`);
}

if (failures.length > 0) {
  console.error("[check-louvain] FAIL");
  for (const f of failures) console.error("  -", f);
  console.error("  alg:", alg);
  console.error("  clusters:", clusters, "xings:", xings);
  process.exit(1);
}

console.log("[check-louvain] OK");
console.log(`  alg: ${alg}`);
console.log(`  clusters=${clusters} xings=${xings}`);
