import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const require = createRequire(import.meta.url);
const execFileAsync = promisify(execFile);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "../..");
const discoveryModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/discovery/discoverDjangoWorkspace.js",
);
const liveDiagramModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/diagram/loadLiveDiagram.js",
);
const bundledOgdfBinaryPath = path.join(
  repoRoot,
  "bin",
  "ogdf",
  `${process.platform}-${process.arch}`,
  process.platform === "win32"
    ? "django-erd-ogdf-layout.exe"
    : "django-erd-ogdf-layout",
);
const { discoverDjangoWorkspace } = require(discoveryModulePath);
const { loadLiveDiagram } = require(liveDiagramModulePath);
const analyzerBuild = execFileAsync(
  "cargo",
  ["build", "--manifest-path", "analyzer/Cargo.toml"],
  {
    cwd: repoRoot,
    maxBuffer: 10 * 1024 * 1024,
  },
);

test("phase10 live diagram service loads real analyzer bootstrap for feature_rich_project", async () => {
  await analyzerBuild;
  const workspacePath = path.join(
    repoRoot,
    "test/fixtures/django/feature_rich_project",
  );
  const discovery = await discoverDjangoWorkspace(workspacePath);
  const result = await loadLiveDiagram(
    repoRoot,
    discovery,
    "hierarchical",
    12.5,
  );

  assert.deepEqual(
    result.payload.graph.nodes.map((node) => node.modelId).sort(),
    ["accounts.Author", "blog.Post", "taxonomy.Tag"],
  );
  assert.equal(result.payload.graph.structuralEdges.length, 4);
  assert.equal(result.payload.layout.mode, "hierarchical");
  assert.equal(result.payload.timings.discoveryMs, 12.5);
  assert.equal(typeof result.payload.timings.parseMs, "number");
  assert.equal(typeof result.payload.timings.graphMs, "number");
  assert.equal(typeof result.payload.timings.layoutMs, "number");
  assert.equal(typeof result.payload.timings.analyzerBootstrapMs, "number");
  if (await pathExists(bundledOgdfBinaryPath)) {
    assert.equal(typeof result.payload.timings.ogdfLayoutMs, "number");
  }
  assert.match(
    JSON.stringify(result.payload.analyzer.models),
    /display_title/,
  );
});

test("phase10 live diagram service preserves diagnostics for partial references", async () => {
  await analyzerBuild;
  const workspacePath = path.join(
    repoRoot,
    "test/fixtures/django/partial_reference_project",
  );
  const discovery = await discoverDjangoWorkspace(workspacePath);
  const result = await loadLiveDiagram(
    repoRoot,
    discovery,
    "hierarchical",
    7.25,
  );

  assert.equal(result.payload.graph.structuralEdges.length, 0);
  assert.deepEqual(
    result.payload.graph.diagnostics.map((diagnostic) => diagnostic.code),
    ["unresolved_reference"],
  );
  assert.equal(result.payload.timings.discoveryMs, 7.25);
});

test("phase10 live diagram service applies OGDF layout from a platform binary process", async () => {
  await analyzerBuild;
  const workspacePath = path.join(
    repoRoot,
    "test/fixtures/django/feature_rich_project",
  );
  const tempDirectory = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-ogdf-test-"));
  const hookPath = path.join(tempDirectory, "fake-ogdf-hook.cjs");
  const previousBinary = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN;
  const previousNodeOptions = process.env.NODE_OPTIONS;

  await fs.writeFile(hookPath, fakeOgdfHookSource(), "utf8");
  process.env.DJANGO_ERD_OGDF_LAYOUT_BIN = process.execPath;
  process.env.NODE_OPTIONS = appendNodeRequire(previousNodeOptions, hookPath);

  try {
    const discovery = await discoverDjangoWorkspace(workspacePath);
    const result = await loadLiveDiagram(
      repoRoot,
      discovery,
      "hierarchical",
      3.5,
    );

    assert.equal(result.payload.layout.mode, "hierarchical");
    assert.equal(typeof result.payload.timings.ogdfLayoutMs, "number");
    assert.deepEqual(
      result.payload.layout.nodes.map((node) => node.position.x),
      [100, 600, 1100],
    );
    assert.deepEqual(
      result.payload.layout.nodes.map((node) => node.position.y),
      [200, 350, 500],
    );
    assert.equal(
      result.payload.layout.routedEdges.length,
      result.payload.graph.structuralEdges.length,
    );
  } finally {
    restoreEnvValue("DJANGO_ERD_OGDF_LAYOUT_BIN", previousBinary);
    restoreEnvValue("NODE_OPTIONS", previousNodeOptions);
    await fs.rm(tempDirectory, { force: true, recursive: true });
  }
});

test("phase10 live diagram service maps advanced OGDF layouts through analyzer fallback modes", async () => {
  await analyzerBuild;
  const workspacePath = path.join(
    repoRoot,
    "test/fixtures/django/feature_rich_project",
  );
  const tempDirectory = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-ogdf-advanced-"));
  const hookPath = path.join(tempDirectory, "fake-ogdf-hook.cjs");
  const previousBinary = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN;
  const previousNodeOptions = process.env.NODE_OPTIONS;

  await fs.writeFile(hookPath, fakeOgdfHookSource(), "utf8");
  process.env.DJANGO_ERD_OGDF_LAYOUT_BIN = process.execPath;
  process.env.NODE_OPTIONS = appendNodeRequire(previousNodeOptions, hookPath);

  try {
    const discovery = await discoverDjangoWorkspace(workspacePath);
    const result = await loadLiveDiagram(
      repoRoot,
      discovery,
      "fmmm",
      4.5,
    );

    assert.equal(result.payload.layout.mode, "fmmm");
    assert.equal(result.payload.view.layoutMode, "fmmm");
    assert.equal(typeof result.payload.timings.ogdfLayoutMs, "number");
    assert.equal(result.payload.layout.nodes.length, result.payload.graph.nodes.length);
  } finally {
    restoreEnvValue("DJANGO_ERD_OGDF_LAYOUT_BIN", previousBinary);
    restoreEnvValue("NODE_OPTIONS", previousNodeOptions);
    await fs.rm(tempDirectory, { force: true, recursive: true });
  }
});

function appendNodeRequire(existingValue, hookPath) {
  const requireOption = `--require=${hookPath}`;

  return existingValue ? `${existingValue} ${requireOption}` : requireOption;
}

function restoreEnvValue(key, value) {
  if (value === undefined) {
    delete process.env[key];
    return;
  }

  process.env[key] = value;
}

async function pathExists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}

function fakeOgdfHookSource() {
  return `
const fs = require("node:fs");

if (!process.argv.includes("--nodes-file") || !process.argv.includes("--edges-file")) {
  return;
}

function argValue(name) {
  const index = process.argv.indexOf(name);
  if (index < 0 || index + 1 >= process.argv.length) {
    throw new Error("missing argument: " + name);
  }
  return process.argv[index + 1];
}

const mode = argValue("--mode");
const nodesFile = argValue("--nodes-file");
const edgesFile = argValue("--edges-file");
const nodes = fs.readFileSync(nodesFile, "utf8")
  .trim()
  .split(/\\r?\\n/)
  .filter(Boolean)
  .map((line, index) => {
    const [modelId, width, height] = line.split("\\t");
    return {
      modelId,
      position: { x: 100 + index * 500, y: 200 + index * 150 },
      size: { width: Number(width), height: Number(height) },
    };
  });
const nodesById = new Map(nodes.map((node) => [node.modelId, node]));
const routedEdges = fs.readFileSync(edgesFile, "utf8")
  .trim()
  .split(/\\r?\\n/)
  .filter(Boolean)
  .map((line) => {
    const [edgeId, sourceModelId, targetModelId] = line.split("\\t");
    const source = nodesById.get(sourceModelId);
    const target = nodesById.get(targetModelId);
    return {
      crossingIds: [],
      edgeId,
      points: [
        { x: source.position.x, y: source.position.y },
        { x: target.position.x, y: target.position.y },
      ],
    };
  });

process.stdout.write(JSON.stringify({ crossings: [], mode, nodes, routedEdges }));
process.exit(0);
`;
}
