#!/usr/bin/env node

const fs = require("node:fs/promises");
const path = require("node:path");
const { discoverDjangoWorkspace } = require(
  "../../out/extension/services/discovery/discoverDjangoWorkspace.js"
);

async function main() {
  const workspacePath = process.argv[2];
  const outputPath = process.argv[3];
  if (!workspacePath || !outputPath) {
    throw new Error(
      "usage: export_discovered_analyzer_request.cjs WORKSPACE OUTPUT_JSON"
    );
  }
  const discovery = await discoverDjangoWorkspace(path.resolve(workspacePath));
  const request = {
    modules: discovery.candidateModules.map((module) => ({
      appLabel: module.appLabel,
      filePath: path.join(
        discovery.selectedRoot,
        ...module.filePath.split("/")
      ),
    })),
    workspaceRoot: discovery.selectedRoot,
  };
  await fs.writeFile(path.resolve(outputPath), JSON.stringify(request), "utf8");
  process.stderr.write(
    `discovery root=${request.workspaceRoot} modules=${request.modules.length}\n`
  );
}

main().catch((error) => {
  process.stderr.write(`${error instanceof Error ? error.stack : error}\n`);
  process.exitCode = 1;
});
