import assert from "node:assert/strict";
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
