#!/usr/bin/env node
"use strict";

// Execute the compiled product layout service without opening or controlling
// a VS Code webview.  This is intentionally a thin adapter so research runs
// inherit the exact environment defaults, cache policy, and candidate audit
// used by runOgdfLayout itself.  Put this process under run_memory_bounded.py.

const fs = require("node:fs/promises");
const path = require("node:path");

function argument(name) {
  const index = process.argv.indexOf(name);
  if (index < 0 || index + 1 >= process.argv.length) {
    throw new Error(`missing argument: ${name}`);
  }
  return process.argv[index + 1];
}

async function main() {
  const root = path.resolve(__dirname, "../..");
  const payloadPath = path.resolve(argument("--payload"));
  const outputPath = path.resolve(argument("--out"));
  const payload = JSON.parse(await fs.readFile(payloadPath, "utf8"));
  const { runOgdfLayout } = require(
    path.join(root, "out/extension/services/layout/runOgdfLayout.js")
  );
  const logger = {
    info(message) {
      process.stderr.write(`[product-layout] ${message}\n`);
    },
    warn(message) {
      process.stderr.write(`[product-layout:warn] ${message}\n`);
    },
  };
  const result = await runOgdfLayout(
    root,
    payload,
    "fmmm",
    logger,
    1,
    "straight",
    true,
    false,
    true,
  );
  if (!result.applied) {
    throw new Error(`product layout was not applied: ${result.reason}`);
  }
  await fs.mkdir(path.dirname(outputPath), { recursive: true });
  await fs.writeFile(outputPath, JSON.stringify(result.layout));
  const metrics = result.layout.engineMetadata || {};
  process.stderr.write(
    `[product-layout:done] visual=${metrics.visualCrossings}`
      + ` edgeCross=${metrics.edgeCrossings}`
      + ` edgeNode=${metrics.edgeNodeIntersections}`
      + ` nodeOverlap=${metrics.nodeOverlaps}`
      + ` durationMs=${result.durationMs}\n`,
  );
}

main().catch((error) => {
  process.stderr.write(`${error && error.stack ? error.stack : error}\n`);
  process.exitCode = 1;
});
