import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import test, { after, before } from "node:test";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const { measureRenderedVisualConflicts, measureRenderedTableClearance } =
  require("../../out/webview/state/createDiagramRenderModel.js");
let directory;
let binary;
let baseline;
const nodes = Array.from({ length: 32 }, (_, i) => ({
  id: `node-${i}`, width: 280 + (i % 4) * 55, height: 110 + (i % 7) * 32,
  x: 500 + (i % 6) * 850, y: 500 + Math.floor(i / 6) * 900,
}));
const pairs = new Set();
let seed = 31;
const random = (size) => {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  return Math.floor((seed / 2 ** 32) * size);
};
while (pairs.size < 56) {
  const a = random(nodes.length), b = random(nodes.length);
  if (a !== b) pairs.add([a, b].sort((x, y) => x - y).join(","));
}
const edges = [...pairs].map((pair, i) => ({ id: `edge-${i}`, ends: pair.split(",").map(Number) }));

function execute(name, options = []) {
  const output = path.join(directory, `${name}.tsv`);
  const start = performance.now();
  const process = spawnSync(binary, [
    ...options,
    "--nodes", path.join(directory, "nodes.tsv"),
    "--edges", path.join(directory, "edges.tsv"),
    "--positions", path.join(directory, "positions.tsv"), "--out", output,
    "--fit", "0", "--width", "7000", "--height", "7000",
    "--steps", "0", "--seconds", "30", "--seed", "55",
  ], { encoding: "utf8", timeout: 35_000 });
  return { ...process, output, duration: performance.now() - start };
}

function audit(run, nodeRows = nodes, edgeRows = edges, maxBends = 0) {
  assert.equal(run.status, 0, run.stderr || String(run.error));
  const positions = new Map(fs.readFileSync(run.output, "utf8").trim().split("\n").map((row) => {
    const [id, x, y] = row.split("\t");
    return [id, { x: Number(x), y: Number(y) }];
  }));
  const routes = new Map(fs.readFileSync(`${run.output}.routes.tsv`, "utf8").trim().split("\n").map((row) => row.split("\t")));
  assert.deepEqual([...positions.keys()].sort(), nodeRows.map((n) => n.id).sort());
  assert.deepEqual([...routes.keys()].sort(), edgeRows.map((e) => e.id).sort());
  const model = {
    tables: nodeRows.map((n) => ({
      modelId: n.id, hidden: false,
      position: { x: positions.get(n.id).x - n.width / 2, y: positions.get(n.id).y - n.height / 2 },
      size: { width: n.width, height: n.height },
    })),
    edges: edgeRows.map((e) => ({
      edgeId: e.id, sourceModelId: nodeRows[e.ends[0]].id, targetModelId: nodeRows[e.ends[1]].id,
      points: routes.get(e.id), preserveRouteEndpoints: true,
    })),
  };
  for (const edge of edgeRows) {
    const points = routes.get(edge.id).split(" ").map((p) => p.split(",").map(Number));
    assert.ok(points.length >= 2 && points.length <= maxBends + 2, "every original relation respects its explicit bend limit");
    for (let i = 0; i < 2; i++) {
      const n = nodeRows[edge.ends[i]], center = positions.get(n.id);
      const p = points[i ? points.length - 1 : 0];
      const dx = Math.abs(p[0] - center.x), dy = Math.abs(p[1] - center.y);
      assert.ok(dx <= n.width / 2 + 0.011 && dy <= n.height / 2 + 0.011);
      assert.ok(Math.abs(dx - n.width / 2) <= 0.011 || Math.abs(dy - n.height / 2) <= 0.011,
        "a port stays on the original card boundary");
    }
  }
  const orientation = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  const segments = edgeRows.map(e => routes.get(e.id).split(" ").map(p => p.split(",").map(Number)));
  const same = (a, b) => Math.abs(a[0] - b[0]) < .001 && Math.abs(a[1] - b[1]) < .001;
  const on = (p, a, b) => Math.abs(orientation(a, b, p)) < 1e-5 && [0, 1].every(axis =>
    p[axis] >= Math.min(a[axis], b[axis]) - 1e-6 && p[axis] <= Math.max(a[axis], b[axis]) + 1e-6);
  for (let i = 0; i < edgeRows.length; i++) for (let j = i + 1; j < edgeRows.length; j++) {
    if (maxBends) {
      const a = segments[i], b = segments[j];
      const shared = edgeRows[i].ends.flatMap((n, x) => edgeRows[j].ends.flatMap((m, y) =>
        n === m && same(a[x ? a.length - 1 : 0], b[y ? b.length - 1 : 0]) ? [a[x ? a.length - 1 : 0]] : []));
      for (const [first, second] of [[a, b], [b, a]]) for (const p of first) for (let k = 1; k < second.length; k++)
        if (on(p, second[k - 1], second[k])) assert.ok(shared.some(q => same(p, q)), `route contact: ${edgeRows[i].id}/${edgeRows[j].id} at ${p}`);
    }
    if (!edgeRows[i].ends.some(n => edgeRows[j].ends.includes(n))) continue;
    for (let p = 1; p < segments[i].length; p++) for (let q = 1; q < segments[j].length; q++) {
      const [a, b] = [segments[i][p - 1], segments[i][p]], [c, d] = [segments[j][q - 1], segments[j][q]];
      assert.ok(!(orientation(a, b, c) * orientation(a, b, d) < -1e-9 && orientation(c, d, a) * orientation(c, d, b) < -1e-9),
        "edges sharing a real card must not cross each other");
    }
  }
  const metrics = measureRenderedVisualConflicts(model);
  const clearance = measureRenderedTableClearance(model);
  const reported = run.stderr.match(/done step=\d+ visual=(\d+) cross=(\d+) hit=(\d+) overlap=(\d+) spacing=(\d+)/);
  assert.ok(reported, run.stderr);
  assert.deepEqual(reported.slice(1).map(Number), [
    metrics.visualCrossings, metrics.edgeCrossings, metrics.edgeNodeIntersections,
    metrics.nodeOverlaps, clearance.spacingViolations,
  ], "incremental search and the independent production renderer must agree");
  assert.equal(clearance.spacingViolations, 0);
  assert.equal(metrics.nodeOverlaps, 0);
  assert.ok(clearance.bboxArea <= 49_000_000);
  for (const table of model.tables) {
    assert.ok(table.position.x >= 0 && table.position.y >= 0);
    assert.ok(table.position.x + table.size.width <= 7000);
    assert.ok(table.position.y + table.size.height <= 7000);
  }
  return metrics;
}

test("bounded detours preserve real cards, complete relations and their length limits", () => {
  const seedPath = path.join(directory, "detour-positions.tsv");
  // Avoid pre-existing straight T-junctions in the regular-grid fixture.
  fs.writeFileSync(seedPath, nodes.map((n, i) => `${n.id}\t${n.x + Math.sin(i * 2.71) * 2.41}\t${n.y + Math.cos(i * 1.31) * 3.83}`).join("\n") + "\n");
  const original = execute("detour-original", ["--positions", seedPath, "--port-steps", "20000"]);
  const originalMetrics = audit(original);
  audit(original, nodes, edges, 2);
  const options = ["--positions", seedPath, "--initial-ports", `${original.output}.routes.tsv`, "--detour-rounds", "2", "--detour-samples", "80", "--detour-fraction", "1"];
  const run = execute("detour-search", options);
  assert.ok(audit(run, nodes, edges, 2).visualCrossings < originalMetrics.visualCrossings, run.stderr);
  assert.equal(fs.readFileSync(run.output, "utf8"), fs.readFileSync(original.output, "utf8"));
  assert.match(run.stderr, /detour bentEdges=[1-9]\d* bends=[1-9]\d*/);
  const joined = execute("detour-joined", ["--detour-beam", "32", "--detour-initial-routes", `${run.output}.routes.tsv`, ...options]);
  assert.ok(audit(joined, nodes, edges, 2).visualCrossings < audit(run, nodes, edges, 2).visualCrossings, joined.stderr);
  const paths = file => new Map(fs.readFileSync(file, "utf8").trim().split("\n").map(row => {
    const [id, points] = row.split("\t"); return [id, points.split(" ").map(p => p.split(",").map(Number))];
  }));
  const length = points => points.slice(1).reduce((sum, p, i) => sum + Math.hypot(p[0] - points[i][0], p[1] - points[i][1]), 0);
  const base = paths(`${original.output}.routes.tsv`);
  for (const [id, points] of paths(`${run.output}.routes.tsv`)) assert.ok(length(points) <= 2 * length(base.get(id)) + .01);
  const replay = execute("detour-replay", ["--positions", seedPath, "--initial-ports", `${original.output}.routes.tsv`,
    "--detour-initial-routes", `${run.output}.routes.tsv`, "--detour-fraction", "1"]);
  assert.deepEqual(audit(replay, nodes, edges, 2), audit(run, nodes, edges, 2));
  assert.equal(fs.readFileSync(`${run.output}.routes.tsv`, "utf8"), fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"));
  const limited = execute("detour-deadline", ["--seconds", "0.03", "--detour-samples", "4000", ...options]);
  assert.ok(audit(limited, nodes, edges, 2).visualCrossings <= originalMetrics.visualCrossings);
  assert.ok(limited.duration < 3000);
});

test("hub relocation audits the complete scene after followers adapt and replays deterministically", () => {
  const options = ["--hub-rounds", "8", "--hub-steps", "6000", "--hub-candidates", "24"];
  const first = execute("hub-relocation", options);
  const metrics = audit(first);
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
  assert.match(first.stderr, /hub round=\d+ root=.* forced=\d+ relaxed=\d+ steps=6000/);
  const second = execute("hub-relocation-replay", options);
  audit(second);
  assert.equal(fs.readFileSync(first.output, "utf8"), fs.readFileSync(second.output, "utf8"));
  assert.equal(fs.readFileSync(`${first.output}.routes.tsv`, "utf8"), fs.readFileSync(`${second.output}.routes.tsv`, "utf8"));
});

test("hub relocation deadline restores a complete feasible best", () => {
  const run = execute("hub-relocation-deadline", [
    "--hub-rounds", "100", "--hub-steps", "1000000", "--seconds", "0.03",
  ]);
  const metrics = audit(run);
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
  assert.ok(run.duration < 3000);
});

test("group repairs preserve fixed cards and audit external relationships", () => {
  const members = nodes.slice(0, 12);
  const groups = path.join(directory, "repair-groups.tsv");
  fs.writeFileSync(groups, members.map(n => `0\t${n.id}\n`).join(""));
  const options = ["--groups", groups, "--group-rate", "1", "--group-repair", "1",
    "--steps", "12000", "--temperature", "10"];
  const first = execute("group-repair", options);
  const metrics = audit(first);
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
  assert.match(first.stderr, /group-repair attempts=[1-9]\d* feasible=[1-9]\d*/);
  const positions = new Map(fs.readFileSync(first.output, "utf8").trim().split("\n").map(row => {
    const [id, x, y] = row.split("\t"); return [id, [Number(x), Number(y)]];
  }));
  for (const n of nodes.slice(12)) assert.deepEqual(positions.get(n.id), [n.x, n.y]);
  const replay = execute("group-repair-replay", options);
  audit(replay);
  assert.equal(fs.readFileSync(first.output, "utf8"), fs.readFileSync(replay.output, "utf8"));
});

test("a crossing cap is maintained while reducing card intersections", () => {
  const run = execute("crossing-cap", ["--steps", "20000", "--crossing-cap", String(baseline.edgeCrossings),
    "--hit-weight", "5", "--best-weighted", "1", "--temperature", "20"]);
  const metrics = audit(run);
  assert.ok(metrics.edgeCrossings <= baseline.edgeCrossings);
  assert.ok(metrics.edgeCrossings + metrics.edgeNodeIntersections * 5 <= baseline.edgeCrossings + baseline.edgeNodeIntersections * 5);
  const rejected = execute("crossing-cap-reject", ["--crossing-cap", String(baseline.edgeCrossings - 1)]);
  assert.equal(rejected.status, 1);
  assert.match(rejected.stderr, /initial crossing count exceeds cap/);
});

for (const repair of [0, 1]) test(`whole-scene crossover audits 80 cards and paired exchanges, repair=${repair}`, () => {
  const cards = [], relations = [], parentRows = [[], []];
  for (let cluster = 0; cluster < 20; cluster++) {
    const left = 500 + (cluster % 4) * 1500, top = 500 + Math.floor(cluster / 4) * 1200;
    for (let corner = 0; corner < 4; corner++) {
      const id = `c-${cluster}-${corner}`;cards.push({ id, width: 100, height: 80 });
      for (let parent = 0; parent < 2; parent++) {
        const column = corner % 2 ^ (corner >= 2 && cluster % 2 === parent ? 1 : 0);
        parentRows[parent].push(`${id}\t${left + column * 800}\t${top + Math.floor(corner / 2) * 800}\n`);
      }
    }
    for (const [a, b] of [[0, 3], [1, 2]]) relations.push({ id: `r-${cluster}-${a}`, ends: [cluster * 4 + a, cluster * 4 + b] });
  }
  const files = ['cards', 'relations', 'parent-a', 'parent-b'].map(n => path.join(directory, `crossover-${n}.tsv`));
  fs.writeFileSync(files[0], cards.map(n => `${n.id}\t${n.width}\t${n.height}\n`).join(''));
  fs.writeFileSync(files[1], relations.map(e => `${e.id}\t${cards[e.ends[0]].id}\t${cards[e.ends[1]].id}\n`).join(''));
  parentRows.forEach((rows, i) => fs.writeFileSync(files[2 + i], rows.join('')));
  const inputs = ["--nodes", files[0], "--edges", files[1]];
  const a = execute("crossover-a", [...inputs, "--positions", files[2]]), b = execute("crossover-b", [...inputs, "--positions", files[3]]);
  const check = run => audit(run, cards, relations);
  const original = check(a);assert.equal(original.visualCrossings, 10);assert.equal(check(b).visualCrossings, 10);
  const options = [...inputs, "--positions", a.output, "--initial-ports", `${a.output}.routes.tsv`,
    "--crossover-positions", b.output, "--crossover-ports", `${b.output}.routes.tsv`, "--crossover-steps", "30000", "--crossover-repair-ports", String(repair)];
  const first = execute("crossover-search", options);
  assert.equal(check(first).visualCrossings, 0, first.stderr);
  assert.match(first.stderr, /crossover factors=[1-9]\d* cells=[1-9]\d*/);
  const selected = Number(first.stderr.match(/crossover selected=(\d+)/)?.[1]);
  assert.ok(selected > 0 && selected < cards.length, "the fixture must exercise a mixed scene");
  const rows = run => new Map(fs.readFileSync(run.output, "utf8").trim().split("\n").map(row => [row.split("\t")[0], row]));
  const left = rows(a), right = rows(b);
  for (const [n, row] of rows(first)) assert.ok(row === left.get(n) || row === right.get(n));
  const replay = execute("crossover-replay", options);
  check(replay);
  assert.equal(fs.readFileSync(first.output, "utf8"), fs.readFileSync(replay.output, "utf8"));
  assert.equal(fs.readFileSync(`${first.output}.routes.tsv`, "utf8"), fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"));
  const limited = execute("crossover-deadline", [...options, "--seconds", "0.003", "--crossover-relax", "1"]);
  assert.ok(check(limited).visualCrossings <= original.visualCrossings);
  assert.ok(limited.duration < 3000);
});

test("adaptive ports and positions preserve exact full-scene geometry and replay together", () => {
  const original = execute("adaptive-original", ["--port-steps", "20000"]);
  const originalMetrics = audit(original);
  const options = ["--initial-ports", `${original.output}.routes.tsv`, "--adaptive-steps", "8000",
    "--adaptive-samples", "8", "--adaptive-temperature", "6", "--adaptive-cycles", "2"];
  const first = execute("adaptive-search", options);
  assert.ok(audit(first).visualCrossings < originalMetrics.visualCrossings);
  assert.notEqual(fs.readFileSync(first.output, "utf8"), fs.readFileSync(original.output, "utf8"));
  assert.match(first.stderr, /adaptive attempts=\d+ feasible=[1-9]\d* positionAccepted=[1-9]\d* portAccepted=[1-9]\d*/);
  const replay = execute("adaptive-replay", options);
  audit(replay);
  assert.equal(fs.readFileSync(first.output, "utf8"), fs.readFileSync(replay.output, "utf8"));
  assert.equal(fs.readFileSync(`${first.output}.routes.tsv`, "utf8"), fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"));
});

test("adaptive port search deadline restores both best positions and best endpoints", () => {
  const run = execute("adaptive-deadline", ["--adaptive-steps", "10000000", "--seconds", "0.03"]);
  assert.ok(audit(run).visualCrossings <= baseline.visualCrossings);
  assert.ok(run.duration < 3000);
});

test("attached ports move with real cards while preserving fan order and exact deltas", () => {
  const original = execute("attached-original", ["--port-steps", "20000"]);
  const originalMetrics = audit(original);
  const options = ["--attached-ports", `${original.output}.routes.tsv`, "--steps", "30000", "--temperature", "12"];
  const first = execute("attached-search", options);
  assert.ok(audit(first).visualCrossings < originalMetrics.visualCrossings);
  const replay = execute("attached-replay", options);
  audit(replay);
  assert.equal(fs.readFileSync(first.output, "utf8"), fs.readFileSync(replay.output, "utf8"));
  assert.equal(fs.readFileSync(`${first.output}.routes.tsv`, "utf8"), fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"));
  const positions = new Map(fs.readFileSync(first.output, "utf8").trim().split("\n").map(row => {
    const [id, x, y] = row.split("\t"); return [id, [Number(x), Number(y)]];
  }));
  const routes = file => new Map(fs.readFileSync(file, "utf8").trim().split("\n").map(row => {
    const [id, points] = row.split("\t"); return [id, points.split(" ").map(p => p.split(",").map(Number))];
  }));
  const before = routes(`${original.output}.routes.tsv`), after = routes(`${first.output}.routes.tsv`);
  for (const e of edges) for (let end = 0; end < 2; end++) {
    const n = nodes[e.ends[end]], center = positions.get(n.id);
    for (let axis = 0; axis < 2; axis++) assert.ok(Math.abs(
      after.get(e.id)[end][axis] - center[axis] - (before.get(e.id)[end][axis] - [n.x, n.y][axis])
    ) < 0.011, "the same boundary point stays attached to the same model");
  }
});

test("linear separation proposals are checked against clipped production geometry", { skip: process.env.ERD_TEST_LINEAR !== "1" }, () => {
  const options = ["--linear-rounds", "8", "--linear-radius", "900", "--linear-limit", "3000"];
  const run = execute("linear-separation", options);
  const metrics = audit(run);
  assert.match(run.stderr, /linear round=\d+ status=0 rows=\d+ columns=\d+ candidate=/);
  assert.match(run.stderr, /valid=1/, run.stderr);
  assert.ok(metrics.visualCrossings < baseline.visualCrossings);
  const replay = execute("linear-separation-replay", options);
  audit(replay);
  assert.equal(fs.readFileSync(run.output, "utf8"), fs.readFileSync(replay.output, "utf8"));
});

test("linear separation deadline retains every model and relation", { skip: process.env.ERD_TEST_LINEAR !== "1" }, () => {
  const run = execute("linear-deadline", ["--linear-rounds", "10000", "--seconds", "0.03"]);
  assert.ok(audit(run).visualCrossings <= baseline.visualCrossings);
  assert.ok(run.duration < 3000);
});

test("linear backtracking evaluates partial moves with exact geometry", { skip: process.env.ERD_TEST_LINEAR !== "1" }, () => {
  const options = ["--linear-rounds", "8", "--linear-radius", "900", "--linear-limit", "3000", "--linear-backtracks", "3"];
  const run = execute("linear-backtracking", options);
  assert.ok(audit(run).visualCrossings < baseline.visualCrossings);
  assert.match(run.stderr, /fraction=0\.5 valid=1/);
  assert.match(run.stderr, /fraction=0\.125/);
  const replay = execute("linear-backtracking-replay", options);
  audit(replay);
  assert.equal(fs.readFileSync(run.output, "utf8"), fs.readFileSync(replay.output, "utf8"));
  assert.equal(fs.readFileSync(`${run.output}.routes.tsv`, "utf8"), fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"));
});

before(() => {
  directory = fs.mkdtempSync(path.join(os.tmpdir(), "erd-constrained-test-"));
  binary = path.join(directory, "optimizer");
  const linearFlags = process.env.ERD_TEST_LINEAR === "1" ? ["-DERD_ENABLE_LINEAR_SEARCH", "-isystem",
    path.join(root, ".tmp/ogdf-build-lowmem/source/ogdf-foxglove-202510/include"),
    path.join(root, ".tmp/ogdf-build-lowmem/build/ogdf/libCOIN.a")] : [];
  const compile = spawnSync(process.env.CXX || "c++", [
    "-O2", "-std=c++17", "-Wall", "-Wextra", ...linearFlags,
    path.join(root, "scripts/erd-poc/constrained_visual_optimizer.cpp"), "-o", binary,
  ], { encoding: "utf8", timeout: 60_000 });
  assert.equal(compile.status, 0, compile.stderr || String(compile.error));
  fs.writeFileSync(path.join(directory, "nodes.tsv"), nodes.map((n) => `${n.id}\t${n.width}\t${n.height}\n`).join(""));
  fs.writeFileSync(path.join(directory, "positions.tsv"), nodes.map((n) => `${n.id}\t${n.x}\t${n.y}\n`).join(""));
  fs.writeFileSync(path.join(directory, "edges.tsv"), edges.map((e) => `${e.id}\t${nodes[e.ends[0]].id}\t${nodes[e.ends[1]].id}\n`).join(""));
  baseline = audit(execute("baseline"));
});
after(() => { if (directory) fs.rmSync(directory, { recursive: true, force: true }); });

for (const [name, proposal] of [
  ["assignment-and-stars", ["--patch-assignment", "1", "--patch-star-candidates", "300"]],
  ["partial-reinsertion", ["--patch-reinsert-candidates", "250"]],
]) {
  test(`${name}: full-scene score includes edges outside an eight-node patch`, () => {
    const args = ["--neighborhood-rounds", "8", "--patch-steps", "800", "--patch-size", "8",
      "--patch-temperature", "2", "--patch-reorder", "1", "--patch-destroy", "1", ...proposal];
    const run = execute(name, args);
    const metrics = audit(run);
    assert.ok(metrics.visualCrossings < baseline.visualCrossings, "the fixture must exercise a successful change");
    const replay = execute(`${name}-replay`, args);
    audit(replay);
    assert.equal(fs.readFileSync(replay.output, "utf8"), fs.readFileSync(run.output, "utf8"));
    assert.equal(fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"), fs.readFileSync(`${run.output}.routes.tsv`, "utf8"));
  });
}

test("rejects a canvas exceeding 1B before searching", () => {
  // CLI uses the first occurrence, so replace the default arguments directly.
  const run = spawnSync(binary, ["--width", "40000", "--height", "40000"], { encoding: "utf8", timeout: 1000 });
  assert.equal(run.status, 1);
  assert.match(run.stderr, /canvas must be finite, positive and <= 1B/);
});

for (const lookahead of [0, 1]) test(`whole-shell reconstruction with lookahead=${lookahead} keeps the 3-core fixed`, () => {
  const neighbors = nodes.map(() => new Set());
  for (const { ends: [a, b] } of edges) { neighbors[a].add(b); neighbors[b].add(a); }
  const core = new Set(nodes.map((_, i) => i));
  let changed = true;
  while (changed) {
    changed = false;
    for (const n of core) if ([...neighbors[n]].filter((other) => core.has(other)).length < 3) {
      core.delete(n); changed = true;
    }
  }
  assert.ok(core.size > 0 && core.size < nodes.length, "the fixture must exercise both the core and shell");
  const run = execute(`shell-${lookahead}`, ["--shell-rounds", "3", "--shell-candidates", "300", "--shell-lookahead", String(lookahead)]);
  const metrics = audit(run);
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
  const positions = new Map(fs.readFileSync(run.output, "utf8").trim().split("\n").map((row) => {
    const [id, x, y] = row.split("\t"); return [id, [Number(x), Number(y)]];
  }));
  for (const n of core) assert.deepEqual(positions.get(nodes[n].id), [nodes[n].x, nodes[n].y]);
});

test("global projections retain complete card geometry and cannot replace a better scene", () => {
  const run = execute("projection", ["--global-projections", "24"]);
  const metrics = audit(run);
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
});

test("resuming a port-only search preserves the exact optimized endpoints", () => {
  const original = execute("port-original", ["--port-steps", "20000"]);
  const originalMetrics = audit(original);
  const resumed = execute("port-resumed", ["--initial-ports", `${original.output}.routes.tsv`]);
  assert.deepEqual(audit(resumed), originalMetrics);
  assert.equal(fs.readFileSync(`${resumed.output}.routes.tsv`, "utf8"), fs.readFileSync(`${original.output}.routes.tsv`, "utf8"));
  const improved = execute("port-improved", ["--initial-ports", `${original.output}.routes.tsv`, "--port-steps", "20000"]);
  assert.ok(audit(improved).visualCrossings <= originalMetrics.visualCrossings);
  const corrupt = path.join(directory, "corrupt-ports.tsv");
  const rows = fs.readFileSync(`${original.output}.routes.tsv`, "utf8").trim().split("\n");
  rows[0] = `${edges[0].id}\t0,0 0,1`;
  fs.writeFileSync(corrupt, rows.join("\n") + "\n");
  const rejected = execute("port-invalid", ["--initial-ports", corrupt]);
  assert.equal(rejected.status, 1);
  assert.match(rejected.stderr, /outside its card boundary/);
});

test("temporary spacing debt is confined to exploration and replay keeps the best feasible scene", () => {
  const args = ["--steps", "20000", "--temporary-spacing", "1", "--temperature", "30", "--cycles", "2", "--degree-temperature", "0.75"];
  // Numeric options use the first occurrence, so invoke without execute's --steps 0.
  const run = name => {
    const output = path.join(directory, `${name}.tsv`);
    return { ...spawnSync(binary, ["--nodes", path.join(directory, "nodes.tsv"),
      "--edges", path.join(directory, "edges.tsv"), "--positions", path.join(directory, "positions.tsv"),
      "--out", output, "--width", "7000", "--height", "7000", "--fit", "0", "--seconds", "5",
      "--seed", "61", ...args], { encoding: "utf8", timeout: 6000 }), output };
  };
  const first = run("temporary-spacing"), second = run("temporary-spacing-replay");
  assert.ok(audit(first).visualCrossings < baseline.visualCrossings);
  audit(second);
  assert.equal(fs.readFileSync(first.output, "utf8"), fs.readFileSync(second.output, "utf8"));
});

test("joint position labels include all moving-star interactions and replay deterministically", () => {
  const args = ["--joint-rounds", "4", "--joint-size", "8", "--joint-labels", "16",
    "--joint-samples", "120", "--joint-steps", "1000"];
  const run = execute("joint", args);
  const metrics = audit(run);
  assert.match(run.stderr, /joint round=\d+ variables=\d+ predictedGain=/);
  assert.ok(metrics.visualCrossings < baseline.visualCrossings);
  const replay = execute("joint-replay", args);
  audit(replay);
  assert.equal(fs.readFileSync(replay.output, "utf8"), fs.readFileSync(run.output, "utf8"));
  assert.equal(fs.readFileSync(`${replay.output}.routes.tsv`, "utf8"), fs.readFileSync(`${run.output}.routes.tsv`, "utf8"));
});

for (const leaves of [0, 1, 2]) test(`connected position factors retain every edge and leaf group, leaves=${leaves}`, () => {
  const args = ["--joint-rounds", "3", "--joint-size", "8", "--joint-labels", "8",
    "--joint-samples", "120", "--joint-steps", "500", "--joint-connected", "1",
    "--joint-leaves", String(leaves), "--joint-relax-spacing", "1"];
  const run = execute(`connected-${leaves}`, args);
  const metrics = audit(run);
  assert.match(run.stderr, /factor tables=\d+ cells=\d+/);
  assert.match(run.stderr, /connected joint round=\d+ variables=\d+ nodes=\d+ predictedGain=/);
  assert.ok(metrics.visualCrossings < baseline.visualCrossings);
  const replay = execute(`connected-${leaves}-replay`, args);
  audit(replay);
  assert.equal(fs.readFileSync(replay.output, "utf8"), fs.readFileSync(run.output, "utf8"));
});

test("connected factor construction deadline returns a complete feasible scene", () => {
  // A short run exercises cancellation while building labels/factors.
  const limited = spawnSync(binary, ["--nodes", path.join(directory, "nodes.tsv"),
    "--edges", path.join(directory, "edges.tsv"), "--positions", path.join(directory, "positions.tsv"),
    "--out", path.join(directory, "connected-limited.tsv"), "--width", "7000", "--height", "7000",
    "--fit", "0", "--steps", "0", "--seconds", "0.03", "--joint-rounds", "100",
    "--joint-connected", "1", "--joint-size", "24", "--joint-labels", "16", "--joint-samples", "4000"],
    { encoding: "utf8", timeout: 2000 });
  const metrics = audit({ ...limited, output: path.join(directory, "connected-limited.tsv") });
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
});

test("interrupted reconstruction restores a complete scene", () => {
  const run = spawnSync(binary, [
    "--nodes", path.join(directory, "nodes.tsv"), "--edges", path.join(directory, "edges.tsv"),
    "--positions", path.join(directory, "positions.tsv"), "--out", path.join(directory, "deadline.tsv"),
    "--fit", "0", "--width", "7000", "--height", "7000", "--steps", "0", "--seconds", "0.02",
    "--neighborhood-rounds", "100", "--patch-size", "24", "--patch-steps", "0",
    "--patch-reorder", "0", "--patch-reinsert-candidates", "10000000",
  ], { encoding: "utf8", timeout: 2000 });
  const metrics = audit({ ...run, output: path.join(directory, "deadline.tsv") });
  assert.deepEqual(metrics, baseline, "an incomplete patch cannot escape as an output layout");
});

test("global projection deadline retains the best complete scene", () => {
  const run = spawnSync(binary, [
    "--nodes", path.join(directory, "nodes.tsv"), "--edges", path.join(directory, "edges.tsv"),
    "--positions", path.join(directory, "positions.tsv"), "--out", path.join(directory, "projection-deadline.tsv"),
    "--fit", "0", "--width", "7000", "--height", "7000", "--steps", "0", "--seconds", "0.02",
    "--global-projections", "10000000",
  ], { encoding: "utf8", timeout: 2000 });
  const metrics = audit({ ...run, output: path.join(directory, "projection-deadline.tsv") });
  assert.ok(metrics.visualCrossings <= baseline.visualCrossings);
});
