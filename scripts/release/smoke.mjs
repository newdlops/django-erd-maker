import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { binaryPaths, cleanRuntimeEnv, run } from "./common.mjs";

// Runs entirely from the supplied extension directory. A VSIX extraction has no
// analyzer/target, node_modules, source checkout, or developer path overrides.
export async function smokePackagedRuntime(extensionRoot) {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), "django-erd-release-smoke-"));
  const oldOverride = process.env.DJANGO_ERD_ANALYZER_BIN;
  delete process.env.DJANGO_ERD_ANALYZER_BIN;
  try {
    const require = createRequire(import.meta.url);
    const { resolveAnalyzerBinaryPath } = require(path.join(extensionRoot,
      "out/extension/services/analyzer/resolveAnalyzerBinaryPath.js"));
    const analyzer = await resolveAnalyzerBinaryPath(extensionRoot);
    assert.equal(analyzer, path.join(extensionRoot, binaryPaths[0]));
    const modelFile = path.join(temporary, "models.py");
    await fs.writeFile(modelFile, `from django.db import models

class Team(models.Model):
    name = models.CharField(max_length=100)

class Person(models.Model):
    team = models.ForeignKey(Team, on_delete=models.CASCADE)

class Note(models.Model):
    author = models.ForeignKey(Person, on_delete=models.CASCADE)
`);
    const runtime = { cwd: temporary, capture: true, env: cleanRuntimeEnv(), timeout: 60000 };
    const bootstrap = JSON.parse(await run(analyzer, [
      "bootstrap", "--mode", "hierarchical", "--workspace-root", temporary,
      "--module", `demo=${modelFile}`,
    ], runtime));
    assert.equal(bootstrap.contractVersion, "1.0");
    assert.equal(bootstrap.graph.nodes.length, 3);
    assert.equal(bootstrap.graph.structuralEdges.filter((edge) => edge.provenance === "declared").length, 2);
    assert.equal(bootstrap.layout.nodes.length, 3);
    assert.equal(typeof bootstrap.timings.parseMs, "number");

    const nodesFile = path.join(temporary, "nodes.tsv");
    const edgesFile = path.join(temporary, "edges.tsv");
    await fs.writeFile(nodesFile, "demo.Team\t200\t120\t0\t0\tdemo\ndemo.Person\t200\t120\t320\t0\tdemo\n");
    await fs.writeFile(edgesFile, "person_team\tdemo.Person\tdemo.Team\tforeign_key\tfield\n");
    const layout = JSON.parse(await run(path.join(extensionRoot, binaryPaths[1]), [
      "layout", "--mode", "hierarchical", "--nodes-file", nodesFile,
      "--edges-file", edgesFile, "--edge-routing", "straight",
    ], runtime));
    assert.equal(layout.nodes.length, 2);
    assert.ok(layout.nodes.every((node) => Number.isFinite(node.position.x) && Number.isFinite(node.position.y)));
    assert.equal(layout.routedEdges.length, 1);
    return { analyzerModels: 3, analyzerRelations: 2, nativeLayoutNodes: 2, developerOverrides: false };
  } finally {
    if (oldOverride === undefined) delete process.env.DJANGO_ERD_ANALYZER_BIN;
    else process.env.DJANGO_ERD_ANALYZER_BIN = oldOverride;
    await fs.rm(temporary, { recursive: true, force: true });
  }
}
