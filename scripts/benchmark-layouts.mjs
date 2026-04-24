#!/usr/bin/env node
import { execFile } from "node:child_process";
import { access, mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const extensionRoot = path.resolve(__dirname, "..");

const ALL_MODES = [
  "hierarchical",
  "hierarchical_barycenter",
  "hierarchical_sifting",
  "hierarchical_global_sifting",
  "hierarchical_greedy_insert",
  "hierarchical_greedy_switch",
  "hierarchical_grid_sifting",
  "hierarchical_split",
  "circular",
  "linear",
  "constrained_force",
  "constrained_force_straight",
  "fmmm",
  "fast_multipole",
  "fast_multipole_multilevel",
  "stress_minimization",
  "pivot_mds",
  "davidson_harel",
  "planarization",
  "planarization_grid",
  "ortho",
  "planar_draw",
  "planar_straight",
  "schnyder",
  "upward_layer_based",
  "upward_planarization",
  "visibility",
  "cluster_planarization",
  "cluster_ortho",
  "uml_ortho",
  "uml_planarization",
  "tree",
  "radial_tree",
];

const options = parseArgs(process.argv.slice(2));
const nodesFile = path.resolve(requireOption(options, "nodes-file"));
const edgesFile = path.resolve(requireOption(options, "edges-file"));
const outputDir = path.resolve(
  options["output-dir"] ?? path.join(extensionRoot, ".benchmarks", timestampSlug()),
);
const modes = options.modes ? options.modes.split(",").map((value) => value.trim()).filter(Boolean) : ALL_MODES;
const timeoutMs = Number(options.timeout ?? 180_000);
const maxBuffer = Number(options["max-buffer"] ?? 500 * 1024 * 1024);

await ensureFile(nodesFile, "nodes TSV");
await ensureFile(edgesFile, "edges TSV");
const binary = await resolveBinary();
await mkdir(outputDir, { recursive: true });

const nodeCount = await countLines(nodesFile);
const edgeCount = await countLines(edgesFile);
process.stderr.write(
  `[benchmark] binary=${binary}\n`
  + `[benchmark] nodes=${nodeCount} edges=${edgeCount}\n`
  + `[benchmark] modes=${modes.length} output=${outputDir}\n`,
);

const results = [];
for (const mode of modes) {
  process.stderr.write(`[benchmark] ${mode} ... `);
  const started = Date.now();
  try {
    const { stdout } = await execFileAsync(
      binary,
      ["layout", "--mode", mode, "--nodes-file", nodesFile, "--edges-file", edgesFile],
      { cwd: extensionRoot, maxBuffer, timeout: timeoutMs },
    );
    const durationMs = Date.now() - started;
    const payload = JSON.parse(stdout);
    await writeFile(path.join(outputDir, `${mode}.json`), stdout, "utf8");
    const summary = pluckSummary(mode, durationMs, payload);
    results.push(summary);
    process.stderr.write(
      `${durationMs}ms crossings=${summary.edgeCrossings} overlappingEdges=${summary.overlappingEdges}\n`,
    );
  } catch (error) {
    const durationMs = Date.now() - started;
    const reason = error instanceof Error ? error.message : String(error);
    results.push({
      mode,
      durationMs,
      status: "error",
      reason: reason.length > 240 ? `${reason.slice(0, 237)}...` : reason,
    });
    process.stderr.write(`ERROR (${durationMs}ms): ${reason.slice(0, 120)}\n`);
  }
}

const summaryJsonPath = path.join(outputDir, "summary.json");
const summaryMdPath = path.join(outputDir, "summary.md");
await writeFile(
  summaryJsonPath,
  `${JSON.stringify({ nodeCount, edgeCount, nodesFile, edgesFile, results }, null, 2)}\n`,
  "utf8",
);
await writeFile(summaryMdPath, renderMarkdown(nodeCount, edgeCount, results), "utf8");
process.stderr.write(`[benchmark] wrote ${summaryMdPath}\n`);
process.stdout.write(`${outputDir}\n`);

function pluckSummary(mode, durationMs, payload) {
  const meta = payload?.engineMetadata ?? {};
  return {
    mode,
    status: "ok",
    durationMs,
    actualMode: meta.actualMode ?? null,
    actualAlgorithm: meta.actualAlgorithm ?? null,
    strategy: meta.strategy ?? null,
    strategyReason: meta.strategyReason ?? null,
    nodeOverlaps: meta.nodeOverlaps ?? 0,
    nodeSpacingOverlaps: meta.nodeSpacingOverlaps ?? 0,
    edgeCrossings: meta.edgeCrossings ?? 0,
    edgeNodeIntersections: meta.edgeNodeIntersections ?? 0,
    edgeSegmentOverlaps: meta.edgeSegmentOverlaps ?? 0,
    overlappingEdges: meta.overlappingEdges ?? 0,
    routeSegments: meta.routeSegments ?? 0,
    boundingBoxArea: round(meta.boundingBoxArea ?? 0, 0),
    aspectRatio: round(meta.aspectRatio ?? 0, 3),
    meanEdgeLength: round(meta.meanEdgeLength ?? 0, 1),
    edgeLengthStddev: round(meta.edgeLengthStddev ?? 0, 1),
    crossingRecords: Array.isArray(payload?.crossings) ? payload.crossings.length : 0,
  };
}

function renderMarkdown(nodeCount, edgeCount, results) {
  const ok = results.filter((r) => r.status === "ok");
  const fail = results.filter((r) => r.status !== "ok");

  const header = [
    `# Layout Benchmark`,
    ``,
    `- nodes: ${nodeCount}`,
    `- edges: ${edgeCount}`,
    `- success: ${ok.length} / ${results.length}`,
    ``,
  ].join("\n");

  const columns = [
    "mode",
    "ms",
    "crossings",
    "ovlEdges",
    "ovlSeg",
    "edgeNodeX",
    "nodeOv",
    "bbox(M)",
    "aspect",
    "meanLen",
    "stddev",
    "strategy",
  ];
  const rows = ok
    .slice()
    .sort((a, b) => {
      if (a.edgeCrossings !== b.edgeCrossings) return a.edgeCrossings - b.edgeCrossings;
      if (a.overlappingEdges !== b.overlappingEdges) return a.overlappingEdges - b.overlappingEdges;
      return a.durationMs - b.durationMs;
    })
    .map((r) => [
      r.mode,
      String(r.durationMs),
      String(r.edgeCrossings),
      String(r.overlappingEdges),
      String(r.edgeSegmentOverlaps),
      String(r.edgeNodeIntersections),
      String(r.nodeOverlaps),
      (r.boundingBoxArea / 1_000_000).toFixed(1),
      String(r.aspectRatio),
      String(r.meanEdgeLength),
      String(r.edgeLengthStddev),
      r.strategy ?? "",
    ]);

  const body = renderTable(columns, rows);

  const failureSection = fail.length > 0
    ? `\n## Failures\n\n${fail.map((r) => `- \`${r.mode}\` (${r.durationMs}ms): ${r.reason ?? "unknown"}`).join("\n")}\n`
    : "";

  return `${header}## Results (sorted by crossings, overlaps, duration)\n\n${body}\n${failureSection}`;
}

function renderTable(columns, rows) {
  const widths = columns.map((col, i) =>
    Math.max(col.length, ...rows.map((row) => (row[i] ?? "").length)),
  );
  const pad = (value, i) => (value ?? "").padEnd(widths[i]);
  const sep = widths.map((w) => "-".repeat(w)).join(" | ");
  const head = columns.map(pad).join(" | ");
  const lines = rows.map((row) => row.map(pad).join(" | "));
  return `| ${head} |\n| ${sep} |\n${lines.map((line) => `| ${line} |`).join("\n")}`;
}

function round(value, digits) {
  if (!Number.isFinite(value)) return 0;
  const factor = 10 ** digits;
  return Math.round(value * factor) / factor;
}

async function resolveBinary() {
  const envOverride = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN;
  const platformKey = `${process.platform}-${process.arch}`;
  const binaryName = process.platform === "win32" ? "django-erd-ogdf-layout.exe" : "django-erd-ogdf-layout";
  const candidates = [
    envOverride,
    path.join(extensionRoot, "bin", "ogdf", platformKey, binaryName),
  ].filter(Boolean);

  for (const candidate of candidates) {
    if (await pathExists(candidate)) {
      return candidate;
    }
  }
  throw new Error(`OGDF binary not found for ${platformKey}. Tried: ${candidates.join(", ")}`);
}

async function countLines(filePath) {
  const { readFile } = await import("node:fs/promises");
  const text = await readFile(filePath, "utf8");
  if (text.length === 0) return 0;
  return text.endsWith("\n") ? text.split("\n").length - 1 : text.split("\n").length;
}

function timestampSlug() {
  const now = new Date();
  const pad = (n) => String(n).padStart(2, "0");
  return `${now.getUTCFullYear()}${pad(now.getUTCMonth() + 1)}${pad(now.getUTCDate())}-${pad(now.getUTCHours())}${pad(now.getUTCMinutes())}${pad(now.getUTCSeconds())}`;
}

function parseArgs(argv) {
  const parsed = {};
  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index];
    const value = argv[index + 1];
    if (!flag?.startsWith("--") || value === undefined) {
      throw new Error(`Invalid argument sequence near ${flag ?? "<eof>"}`);
    }
    parsed[flag.slice(2)] = value;
  }
  return parsed;
}

function requireOption(options, key) {
  const value = options[key];
  if (!value) {
    throw new Error(`Missing --${key} option`);
  }
  return value;
}

async function ensureFile(filePath, label) {
  if (!(await pathExists(filePath))) {
    throw new Error(`${label} is missing: ${filePath}`);
  }
}

async function pathExists(filePath) {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}
