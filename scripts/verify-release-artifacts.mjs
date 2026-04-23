import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import { execFile } from "node:child_process";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..");

const manifest = JSON.parse(
  await fs.readFile(path.join(repoRoot, "package.json"), "utf8"),
);
await execFileAsync(
  "cargo",
  ["build", "--manifest-path", "analyzer/Cargo.toml"],
  {
    cwd: repoRoot,
    maxBuffer: 10 * 1024 * 1024,
  },
);
const extensionEntryPath = path.join(repoRoot, manifest.main.replace(/^\.\//, ""));
const analyzerBinaryPath = path.join(
  repoRoot,
  "analyzer/target/debug",
  process.platform === "win32"
    ? "django-erd-maker-analyzer.exe"
    : "django-erd-maker-analyzer",
);
const ogdfBundledBinaryPath = path.join(
  repoRoot,
  "bin/ogdf/darwin-arm64/django-erd-ogdf-layout",
);
const ogdfSourceArchivePath = path.join(
  repoRoot,
  "vendor/ogdf/ogdf-foxglove-202510.tar.gz",
);

await assertFileExists(extensionEntryPath, "built extension entrypoint");
await assertFileExists(analyzerBinaryPath, "built analyzer binary");
await assertFileExists(ogdfBundledBinaryPath, "bundled mac OGDF binary");
await assertFileExists(ogdfSourceArchivePath, "bundled OGDF source archive");
assert.ok(
  manifest.contributes.commands.some((command) => command.command === "djangoErd.openDiagram"),
  "package.json must contribute djangoErd.openDiagram",
);
assert.ok(
  manifest.contributes.commands.some((command) => command.command === "djangoErd.refreshDiagram"),
  "package.json must contribute djangoErd.refreshDiagram",
);

const featureRichFixtureRoot = path.join(
  repoRoot,
  "test/fixtures/django/feature_rich_project",
);
const { stdout } = await execFileAsync(
  analyzerBinaryPath,
  [
    "bootstrap",
    "--mode",
    "hierarchical",
    "--workspace-root",
    featureRichFixtureRoot,
    "--module",
    `accounts=${path.join(featureRichFixtureRoot, "accounts/models.py")}`,
    "--module",
    `blog=${path.join(featureRichFixtureRoot, "blog/models.py")}`,
    "--module",
    `taxonomy=${path.join(featureRichFixtureRoot, "taxonomy/models.py")}`,
  ],
  {
    cwd: repoRoot,
    maxBuffer: 10 * 1024 * 1024,
  },
);
const payload = JSON.parse(stdout);

assert.equal(payload.contractVersion, "1.0");
assert.ok(payload.graph.nodes.length >= 3, "bootstrap payload must contain graph nodes");
assert.ok(payload.layout.nodes.length >= 3, "bootstrap payload must contain layout nodes");
assert.equal(typeof payload.timings.parseMs, "number");
assert.equal(typeof payload.timings.graphMs, "number");
assert.equal(typeof payload.timings.layoutMs, "number");

console.log("release artifacts verified");

async function assertFileExists(filePath, label) {
  try {
    await fs.access(filePath);
  } catch {
    throw new Error(`Missing ${label}: ${filePath}`);
  }
}
