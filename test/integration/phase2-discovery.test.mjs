import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const fixturesRoot = path.resolve(__dirname, "../fixtures/django");
const discoveryModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/discovery/discoverDjangoWorkspace.js",
);
const { discoverDjangoWorkspace } = require(discoveryModulePath);

test("single_app_project discovers one app and one model file", async () => {
  const fixturePath = path.join(fixturesRoot, "single_app_project");
  const result = await discoverDjangoWorkspace(fixturePath);

  assert.equal(result.strategy, "manage_py");
  assert.equal(result.apps.length, 1);
  assert.equal(result.apps[0].appLabel, "blog");
  assert.deepEqual(result.candidateModelFiles, ["blog/models.py"]);
  assertDiscoveredModulesContain(result, ["blog/models.py"]);
  assert.ok(result.timings);
  assert.ok(result.timings.rootSelectionMs >= 0);
  assert.ok(result.timings.appDiscoveryMs >= 0);
  assert.ok(result.timings.candidateModulesMs >= 0);
});

test("multi_app_project discovers cross-app model modules", async () => {
  const fixturePath = path.join(fixturesRoot, "multi_app_project");
  const result = await discoverDjangoWorkspace(fixturePath);

  assert.deepEqual(
    result.apps.map((app) => app.appLabel),
    ["accounts", "blog", "taxonomy"],
  );
  assert.deepEqual(result.candidateModelFiles, [
    "accounts/models.py",
    "blog/models.py",
    "taxonomy/models.py",
  ]);
  assertDiscoveredModulesContain(result, [
    "accounts/models.py",
    "blog/models.py",
    "taxonomy/models.py",
  ]);
});

test("disconnected_project preserves connected and disconnected app candidates", async () => {
  const fixturePath = path.join(fixturesRoot, "disconnected_project");
  const result = await discoverDjangoWorkspace(fixturePath);

  assert.deepEqual(
    result.apps.map((app) => app.appLabel),
    ["audit", "crm", "sales"],
  );
  assert.deepEqual(result.candidateModelFiles, [
    "audit/models.py",
    "crm/models.py",
    "sales/models.py",
  ]);
  assertDiscoveredModulesContain(result, [
    "audit/models.py",
    "crm/models.py",
    "sales/models.py",
  ]);
});

test("feature_rich_project discovers choices, property, and method fixture apps", async () => {
  const fixturePath = path.join(fixturesRoot, "feature_rich_project");
  const result = await discoverDjangoWorkspace(fixturePath);

  assert.deepEqual(
    result.apps.map((app) => app.appLabel),
    ["accounts", "blog", "taxonomy"],
  );
  assert.deepEqual(result.candidateModelFiles, [
    "accounts/models.py",
    "blog/models.py",
    "taxonomy/models.py",
  ]);
  assertDiscoveredModulesContain(result, [
    "accounts/models.py",
    "blog/models.py",
    "taxonomy/models.py",
  ]);
});

test("partial_reference_project emits discovery diagnostics for ambiguous and partial structure", async () => {
  const fixturePath = path.join(fixturesRoot, "partial_reference_project");
  const result = await discoverDjangoWorkspace(fixturePath);
  const diagnosticCodes = result.diagnostics.map((diagnostic) => diagnostic.code).sort();

  assert.equal(path.basename(result.selectedRoot), "partial_reference_project");
  assert.deepEqual(result.candidateModelFiles, ["orphan/models.py"]);
  assertDiscoveredModulesContain(result, ["orphan/models.py"]);
  assert.deepEqual(diagnosticCodes, [
    "app_without_model_modules",
    "models_package_missing_init",
    "multiple_manage_py_roots",
  ]);
});

test("crossing_layout_project remains executable as a focused discovery fixture", async () => {
  const fixturePath = path.join(fixturesRoot, "crossing_layout_project");
  const result = await discoverDjangoWorkspace(fixturePath);

  assert.deepEqual(
    result.apps.map((app) => app.appLabel),
    ["mesh"],
  );
  assert.deepEqual(result.candidateModelFiles, ["mesh/models.py"]);
  assertDiscoveredModulesContain(result, [
    "mesh/models.py",
  ]);
});

test("project_wide_scan_project does not forward non-model modules to the analyzer", async () => {
  const fixturePath = path.join(fixturesRoot, "project_wide_scan_project");
  const result = await discoverDjangoWorkspace(fixturePath);

  assert.deepEqual(
    result.apps.map((app) => app.appLabel),
    ["catalog"],
  );
  assert.deepEqual(result.candidateModelFiles, []);
  assert.deepEqual(result.candidateModules, []);
  assert.ok(
    result.candidateModules.every((module) => module.filePath !== "manage.py"),
    "manage.py should not be forwarded to the analyzer",
  );
});

test("hidden directories do not contribute Django roots or apps", async (t) => {
  const workspacePath = await mkdtemp(
    path.join(os.tmpdir(), "django-erd-hidden-discovery-"),
  );
  t.after(() => rm(workspacePath, { force: true, recursive: true }));

  await writeFile(path.join(workspacePath, "manage.py"), "# visible root\n", "utf8");

  const visibleAppPath = path.join(workspacePath, "visible_app");
  await mkdir(visibleAppPath);
  await writeFile(path.join(visibleAppPath, "apps.py"), "# visible app\n", "utf8");
  await writeFile(path.join(visibleAppPath, "models.py"), "# visible models\n", "utf8");

  for (const hiddenDirectoryName of [".vscode", ".lh", ".hidden-django"]) {
    const hiddenDirectoryPath = path.join(workspacePath, hiddenDirectoryName);
    await mkdir(hiddenDirectoryPath);
    await writeFile(path.join(hiddenDirectoryPath, "manage.py"), "# hidden root\n", "utf8");
    await writeFile(path.join(hiddenDirectoryPath, "apps.py"), "# hidden app\n", "utf8");
    await writeFile(path.join(hiddenDirectoryPath, "models.py"), "# hidden models\n", "utf8");
  }

  const result = await discoverDjangoWorkspace(workspacePath);

  assert.equal(result.strategy, "manage_py");
  assert.equal(result.selectedRoot, workspacePath);
  assert.deepEqual(
    result.apps.map((app) => app.appLabel),
    ["visible_app"],
  );
  assert.deepEqual(result.candidateModelFiles, ["visible_app/models.py"]);
  assertDiscoveredModulesContain(result, ["visible_app/models.py"]);
  assert.ok(
    result.diagnostics.every(
      (diagnostic) => diagnostic.code !== "multiple_manage_py_roots",
    ),
    "manage.py files under hidden directories should not create extra roots",
  );
});

function assertDiscoveredModulesContain(result, expectedPaths) {
  const discoveredPaths = result.candidateModules.map((module) => module.filePath);

  for (const expectedPath of expectedPaths) {
    assert.ok(
      discoveredPaths.includes(expectedPath),
      `Expected discovery to include ${expectedPath}, got ${discoveredPaths.join(", ")}`,
    );
  }
}
