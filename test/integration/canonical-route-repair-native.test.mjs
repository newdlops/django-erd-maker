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
  repoRoot, "bin", "ogdf", `${process.platform}-${process.arch}`,
  process.platform === "win32" ? "django-erd-ogdf-layout.exe" : "django-erd-ogdf-layout",
);

test("native canonical route repair clears a node without category or visual debt", {
  skip: !(await pathExists(binaryPath)),
}, async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-route-repair-"));
  const nodesPath = path.join(directory, "nodes.tsv");
  const edgesPath = path.join(directory, "edges.tsv");
  const positionsPath = path.join(directory, "positions.tsv");
  const positions = [
    ["a", 0, 0], ["b", 600, 0], ["blocker", 300, -20], ["anchor", 300, 300],
    ["hub", 0, 500], ["upper", 600, 440], ["lower", 600, 560],
  ];
  await fs.writeFile(nodesPath, `${positions.map(([id, x, y]) =>
    `${id}\t100\t${id === "blocker" ? 220 : 70}\t${x}\t${y}\ttest`).join("\n")}\n`);
  await fs.writeFile(positionsPath, `${positions.map(([id, x, y]) =>
    `${id}\t${x}\t${y}`).join("\n")}\n`);
  await fs.writeFile(edgesPath, [
    "through\ta\tb\tforeign_key\tdeclared",
    "blocker-anchor\tblocker\tanchor\tforeign_key\tdeclared",
    "join-a-anchor\ta\tanchor\tforeign_key\tdeclared",
    "join-hub-anchor\thub\tanchor\tforeign_key\tdeclared",
    "fan-upper\thub\tupper\tforeign_key\tdeclared",
    "fan-lower\thub\tlower\tforeign_key\tdeclared",
  ].join("\n") + "\n");

  const run = async (enabled) => {
    const { stdout, stderr } = await execFileAsync(binaryPath, [
      "layout", "--mode", "fmmm", "--nodes-file", nodesPath,
      "--edges-file", edgesPath, "--edge-routing", "straight",
      "--positions-tsv", positionsPath, "--rigid-positions", "1",
    ], {
      cwd: repoRoot,
      env: { ...process.env, DJERD_CANONICAL_ROUTE_REPAIR: enabled ? "1" : "0" },
      maxBuffer: 8 * 1024 * 1024,
      timeout: 30_000,
    });
    return { result: JSON.parse(stdout), stderr };
  };

  try {
    const baseline = await run(false);
    const repaired = await run(true);
    const before = baseline.result.engineMetadata.canonicalCrossing;
    const after = repaired.result.engineMetadata.canonicalCrossing;
    assert.ok(before.nonIncidentNodeHits > 0);
    assert.ok(
      after.nonIncidentNodeHits < before.nonIncidentNodeHits,
      `${repaired.stderr}\n${JSON.stringify(repaired.result.nodes)}\n${JSON.stringify(repaired.result.routedEdges)}`,
    );
    for (const key of [
      "adjacentEdgeIntersections", "routeCrossingPairs", "routeCrossingPoints",
      "invariantViolations", "degenerateSegments", "collinearOverlaps",
      "pointContacts", "selfIntersections",
    ]) assert.ok(after[key] <= before[key], key);
    assert.equal(after.completeRoutes, true);
    assert.ok(
      repaired.result.engineMetadata.visualCrossings
        <= baseline.result.engineMetadata.visualCrossings,
    );
    assert.match(repaired.stderr, /\[canonical-route-repair\] accepted/);
    assert.match(
      repaired.stderr,
      /canonical=\{invariant=\d+->\d+,degenerate=\d+->\d+,collinearOverlap=/,
    );
    assert.match(repaired.stderr, /pointContact=\d+->\d+,selfIntersection=/);
    assert.match(repaired.stderr, /allComplete=1->1,properDrawing=/);
    assert.match(repaired.stderr, /boundViolation=0->0/);
    assert.match(
      repaired.stderr,
      /visual=\{total=\d+->\d+,edgeCross=\d+->\d+,edgeNode=/,
    );
    assert.match(repaired.stderr, /overlappingEdges=\d+->\d+,edgeSegmentOverlap=/);
    assert.match(repaired.stderr, /failed=\{none\}/);
    assert.match(repaired.stderr, /\(canonicalSafe=1 visualSafe=1\)/);

    const overlapPositions = [
      ...positions, ["rail-left", 100, -142], ["rail-right", 500, -142],
    ];
    await fs.writeFile(nodesPath, `${overlapPositions.map(([id, x, y]) =>
      `${id}\t100\t${id === "blocker" ? 220 : 70}\t${x}\t${y}\ttest`).join("\n")}\n`);
    await fs.writeFile(positionsPath, `${overlapPositions.map(([id, x, y]) =>
      `${id}\t${x}\t${y}`).join("\n")}\n`);
    await fs.appendFile(
      edgesPath,
      "rail\trail-left\trail-right\tforeign_key\tdeclared\n",
    );
    const guardedBaseline = await run(false);
    const guarded = await run(true);
    const guardedBefore =
      guardedBaseline.result.engineMetadata.canonicalCrossing;
    const guardedAfter = guarded.result.engineMetadata.canonicalCrossing;
    assert.deepEqual(guardedAfter, guardedBefore);
    assert.equal(guardedBefore.collinearOverlaps, 0);
    assert.equal(guardedAfter.collinearOverlaps, 0);
    assert.ok(
      guarded.result.engineMetadata.edgeSegmentOverlaps
        <= guardedBaseline.result.engineMetadata.edgeSegmentOverlaps,
    );
    assert.match(
      guarded.stderr,
      /\[canonical-route-repair\] no safe obstacle detour candidate \(1 hit edges\)/,
    );
    assert.doesNotMatch(guarded.stderr, /\[canonical-route-repair\] accepted/);
    assert.doesNotMatch(guarded.stderr, /\[canonical-route-repair\] reverted/);
  } finally {
    await fs.rm(directory, { recursive: true, force: true });
  }
});

async function pathExists(filePath) {
  try { await fs.access(filePath); return true; } catch { return false; }
}
