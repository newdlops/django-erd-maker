import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..");

const requiredArtifacts = [
  path.join(repoRoot, "bin", "ogdf", "darwin-arm64", "django-erd-ogdf-layout"),
  path.join(repoRoot, "vendor", "ogdf", "ogdf-foxglove-202510.tar.gz"),
];

for (const artifactPath of requiredArtifacts) {
  const stats = await statFile(artifactPath);
  assert.ok(stats.isFile(), `Missing OGDF artifact: ${artifactPath}`);
  assert.ok(stats.size > 0, `OGDF artifact is empty: ${artifactPath}`);
}

console.log("OGDF bundled binary and source archive verified");

async function statFile(filePath) {
  try {
    return await fs.stat(filePath);
  } catch (error) {
    throw new Error(`Missing OGDF artifact: ${filePath}`, { cause: error });
  }
}
