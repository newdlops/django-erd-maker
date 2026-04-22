import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { execFile } from "node:child_process";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "../..");

test("phase11 boundary audit prevents webview imports from extension layer", async () => {
  const webviewFiles = await collectFiles(path.join(repoRoot, "src/webview"));

  for (const filePath of webviewFiles) {
    if (!filePath.endsWith(".ts")) {
      continue;
    }

    const source = await fs.readFile(filePath, "utf8");
    assert.doesNotMatch(
      source,
      /from\s+["'][^"']*extension\/|from\s+["'][^"']*\.\.\/\.\.\/extension\//,
      `webview layer must not import extension layer: ${path.relative(repoRoot, filePath)}`,
    );
  }
});

test("phase11 size audit keeps non-markdown files under 500 lines", async () => {
  const roots = [
    path.join(repoRoot, "src"),
    path.join(repoRoot, "analyzer/src"),
    path.join(repoRoot, "test"),
    path.join(repoRoot, "scripts"),
  ];

  for (const rootPath of roots) {
    for (const filePath of await collectFiles(rootPath)) {
      if (path.extname(filePath) === ".md") {
        continue;
      }

      const lineCount = (await fs.readFile(filePath, "utf8")).split("\n").length;
      assert.ok(
        lineCount <= 500,
        `${path.relative(repoRoot, filePath)} has ${lineCount} lines`,
      );
    }
  }
});

test("phase11 release verification script passes after build", async () => {
  const scriptPath = path.join(repoRoot, "scripts/verify-release-artifacts.mjs");
  const { stdout } = await execFileAsync(
    "node",
    [scriptPath],
    {
      cwd: repoRoot,
      maxBuffer: 10 * 1024 * 1024,
    },
  );

  assert.match(stdout, /release artifacts verified/);
});

async function collectFiles(rootPath) {
  const entries = await fs.readdir(rootPath, { withFileTypes: true });
  const files = [];

  for (const entry of entries) {
    const entryPath = path.join(rootPath, entry.name);

    if (entry.isDirectory()) {
      files.push(...await collectFiles(entryPath));
      continue;
    }

    if (entry.isFile()) {
      files.push(entryPath);
    }
  }

  files.sort();
  return files;
}
