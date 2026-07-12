import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";

const execFileAsync = promisify(execFile);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "../..");
const binaryPath = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN ?? path.join(
  repoRoot,
  "bin",
  "ogdf",
  `${process.platform}-${process.arch}`,
  process.platform === "win32"
    ? "django-erd-ogdf-layout.exe"
    : "django-erd-ogdf-layout",
);
const binaryAvailable = await pathExists(binaryPath);

test("native certifier proves standard K3,n lower bounds", {
  skip: !binaryAvailable,
}, async () => {
  for (const [rightSize, expected] of [[3, 1], [4, 2], [10, 20]]) {
    const report = await certify(completeBipartiteEdges(3, rightSize));
    assert.equal(report.lowerBound, expected, `K3,${rightSize}`);
    assert.equal(report.k3nContribution + report.kuratowskiContribution, expected);
    assert.equal(report.edgeCount, 3 * rightSize);
    assert.equal(report.domain, "canonical-simple-v1");
    assert.match(report.certifierVersion, /\S/);
    if (report.properDrawing) {
      assert.ok(report.routeCrossingPairs >= report.lowerBound);
      assert.equal(report.gap, report.routeCrossingPairs - report.lowerBound);
      assert.equal(report.boundViolation, false);
    } else {
      assert.equal(report.gap, undefined);
    }
  }
});

test("native certifier adds edge-disjoint non-planar certificates", {
  skip: !binaryAvailable,
}, async () => {
  const left = completeBipartiteEdges(3, 3, "a");
  const right = completeBipartiteEdges(3, 3, "b");
  const report = await certify([...left, ...right]);

  assert.equal(report.lowerBound, 2);
  assert.equal(report.k3nContribution + report.kuratowskiContribution, 2);
  assert.equal(report.k3nCertificates + report.kuratowskiCertificates, 2);
});

test("native certifier recognizes a subdivided K3,3", {
  skip: !binaryAvailable,
}, async () => {
  const subdivided = completeBipartiteEdges(3, 3).flatMap(([source, target], index) => {
    const middle = `s${index}`;
    return [[source, middle], [middle, target]];
  });
  const report = await certify(subdivided);

  assert.equal(report.lowerBound, 1);
  assert.equal(report.edgeCount, 18);
});

test("canonical graph normalization ignores loops, direction, parallels, and input order", {
  skip: !binaryAvailable,
}, async () => {
  const base = completeBipartiteEdges(3, 3);
  const noisy = [
    ...base.slice().reverse().map(([u, v], index) => index % 2 === 0 ? [v, u] : [u, v]),
    [base[0][1], base[0][0]],
    ["l0", "l0"],
  ];
  const [cleanReport, noisyReport] = await Promise.all([
    certify(base),
    certify(noisy),
  ]);

  assert.equal(cleanReport.lowerBound, 1);
  assert.equal(noisyReport.lowerBound, 1);
  assert.equal(noisyReport.edgeCount, cleanReport.edgeCount);
  assert.equal(noisyReport.nodeCount, cleanReport.nodeCount);
  assert.equal(noisyReport.method, cleanReport.method);
});

test("native certifier reports zero for a planar K4", {
  skip: !binaryAvailable,
}, async () => {
  const report = await certify([
    ["v0", "v1"], ["v0", "v2"], ["v0", "v3"],
    ["v1", "v2"], ["v1", "v3"], ["v2", "v3"],
  ]);
  assert.equal(report.lowerBound, 0);
  assert.equal(report.k3nContribution, 0);
  assert.equal(report.kuratowskiContribution, 0);
});

test("raw non-carrier multistart stops at a verified canonical floor", {
  skip: !binaryAvailable,
}, async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-crossing-stop-"));
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  const nodeIds = ["center", "a", "b", "c", "d"];
  const edges = nodeIds.slice(1).map((nodeId) => ["center", nodeId]);
  await fs.writeFile(
    nodesPath,
    `${nodeIds.map((modelId, index) => `${modelId}\t120\t80\t${index * 160}\t0\ttest`).join("\n")}\n`,
    "utf8",
  );
  await fs.writeFile(
    edgesPath,
    `${edges.map(([source, target], index) => `edge-${index}\t${source}\t${target}\tforeign_key\tdeclared`).join("\n")}\n`,
    "utf8",
  );

  try {
    const { stderr } = await execFileAsync(binaryPath, [
      "layout",
      "--mode", "fmmm",
      "--nodes-file", nodesPath,
      "--edges-file", edgesPath,
      "--edge-routing", "straight",
      "--cluster-graph", "1",
    ], {
      cwd: repoRoot,
      env: {
        ...process.env,
        DJERD_CANONICAL_CROSSING_CACHE: "0",
        DJERD_MULTISTART_BBOX_WEIGHT: "0",
        DJERD_MULTISTART_BUNDLE_AWARE: "0",
        DJERD_MULTISTART_RUNS: "8",
      },
      maxBuffer: 16 * 1024 * 1024,
      timeout: 30_000,
    });
    assert.match(stderr, /certified canonical crossing floor reached at run 0/);
    assert.match(stderr, /selected run .*runs=1/);
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
});

function completeBipartiteEdges(leftSize, rightSize, prefix = "") {
  const edges = [];
  for (let left = 0; left < leftSize; left += 1) {
    for (let right = 0; right < rightSize; right += 1) {
      edges.push([`${prefix}l${left}`, `${prefix}r${right}`]);
    }
  }
  return edges;
}

async function certify(edges) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-crossing-lb-"));
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  const nodeIds = [...new Set(edges.flat())].sort();
  const nodesTsv = nodeIds
    .map((modelId, index) => `${modelId}\t120\t80\t${(index % 5) * 180}\t${Math.floor(index / 5) * 140}\ttest`)
    .join("\n");
  const edgesTsv = edges
    .map(([source, target], index) => `edge-${index}\t${source}\t${target}\tforeign_key\tdeclared`)
    .join("\n");
  await fs.writeFile(nodesPath, `${nodesTsv}\n`, "utf8");
  await fs.writeFile(edgesPath, `${edgesTsv}\n`, "utf8");

  try {
    const { stdout } = await execFileAsync(binaryPath, [
      "layout",
      "--mode", "circular",
      "--nodes-file", nodesPath,
      "--edges-file", edgesPath,
      "--edge-routing", "straight",
    ], {
      cwd: repoRoot,
      env: {
        ...process.env,
        DJERD_MULTISTART_RUNS: "1",
        DJERD_STRESS_POST_PASS_ITERS: "0",
      },
      maxBuffer: 16 * 1024 * 1024,
      timeout: 30_000,
    });
    const parsed = JSON.parse(stdout);
    assert.ok(parsed.engineMetadata?.canonicalCrossing);
    return parsed.engineMetadata.canonicalCrossing;
  } finally {
    await fs.rm(directory, { force: true, recursive: true });
  }
}

async function pathExists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}
