import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { cleanRuntimeEnv, repoRoot } from "../../scripts/release/common.mjs";

const require = createRequire(import.meta.url);
const { resolveAnalyzerBinaryPath } = require(path.join(repoRoot,
  "out/extension/services/analyzer/resolveAnalyzerBinaryPath.js"));

test("installed analyzer resolves without a source checkout and beats development builds", async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-binary-resolution-"));
  const previous = process.env.DJANGO_ERD_ANALYZER_BIN;
  delete process.env.DJANGO_ERD_ANALYZER_BIN;
  const binary = process.platform === "win32" ? "django-erd-maker-analyzer.exe" : "django-erd-maker-analyzer";
  const bundled = path.join(root, "bin/analyzer", `${process.platform}-${process.arch}`, binary);
  const development = path.join(root, "analyzer/target/release", binary);
  try {
    await fs.mkdir(path.dirname(bundled), { recursive: true });
    await fs.writeFile(bundled, "test fixture");
    assert.equal(await resolveAnalyzerBinaryPath(root), bundled);
    await fs.mkdir(path.dirname(development), { recursive: true });
    await fs.writeFile(development, "test fixture");
    assert.equal(await resolveAnalyzerBinaryPath(root), bundled);
    process.env.DJANGO_ERD_ANALYZER_BIN = development;
    assert.equal(await resolveAnalyzerBinaryPath(root), development);
    process.env.DJANGO_ERD_ANALYZER_BIN = path.join(root, "nonexistent-override");
    assert.equal(await resolveAnalyzerBinaryPath(root), bundled);
    delete process.env.DJANGO_ERD_ANALYZER_BIN;
    await fs.unlink(bundled);
    assert.equal(await resolveAnalyzerBinaryPath(root), development);
    await fs.unlink(development);
    await assert.rejects(resolveAnalyzerBinaryPath(root), /Install the VSIX for this extension host/);
  } finally {
    if (previous === undefined) delete process.env.DJANGO_ERD_ANALYZER_BIN;
    else process.env.DJANGO_ERD_ANALYZER_BIN = previous;
    await fs.rm(root, { recursive: true, force: true });
  }
});

test("release smoke environments cannot inherit developer binaries or layout overrides", () => {
  const oldAnalyzer = process.env.DJANGO_ERD_ANALYZER_BIN;
  const oldLayout = process.env.DJERD_TEST_LAYOUT_OVERRIDE;
  try {
    process.env.DJANGO_ERD_ANALYZER_BIN = "/not-a-packaged-binary";
    process.env.DJERD_TEST_LAYOUT_OVERRIDE = "test";
    const environment = cleanRuntimeEnv();
    assert.equal(environment.DJANGO_ERD_ANALYZER_BIN, undefined);
    assert.equal(environment.DJERD_TEST_LAYOUT_OVERRIDE, undefined);
    assert.equal(environment.PATH, process.env.PATH);
  } finally {
    if (oldAnalyzer === undefined) delete process.env.DJANGO_ERD_ANALYZER_BIN;
    else process.env.DJANGO_ERD_ANALYZER_BIN = oldAnalyzer;
    if (oldLayout === undefined) delete process.env.DJERD_TEST_LAYOUT_OVERRIDE;
    else process.env.DJERD_TEST_LAYOUT_OVERRIDE = oldLayout;
  }
});
