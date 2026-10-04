#!/usr/bin/env node

const fs = require("node:fs/promises");
const path = require("node:path");

function clean(value) {
  return String(value ?? "").replace(/\t/g, " ").replace(/\r?\n/g, " ");
}

async function main() {
  const payloadPath = process.argv[2];
  const outputDirectory = process.argv[3];
  if (!payloadPath || !outputDirectory) {
    throw new Error("usage: export_payload_graph.cjs PAYLOAD_JSON OUTPUT_DIR");
  }
  const payload = JSON.parse(await fs.readFile(path.resolve(payloadPath), "utf8"));
  const appByModel = new Map(
    payload.graph.nodes.map((node) => [node.modelId, node.appLabel])
  );
  const nodeRows = payload.layout.nodes.map((node) => [
    clean(node.modelId),
    Number(node.size.width) || 1,
    Number(node.size.height) || 1,
    Number(node.position.x) || 0,
    Number(node.position.y) || 0,
    clean(appByModel.get(node.modelId)),
  ].join("\t"));
  const edgeRows = payload.graph.structuralEdges.map((edge) => [
    clean(edge.id),
    clean(edge.sourceModelId),
    clean(edge.targetModelId),
    clean(edge.kind),
    clean(edge.provenance),
  ].join("\t"));
  const canonicalEdges = [];
  const seenCanonicalPairs = new Set();
  for (const edge of payload.graph.structuralEdges) {
    if (
      edge.provenance !== "declared"
      || edge.sourceModelId === edge.targetModelId
    ) {
      continue;
    }
    const pair = [edge.sourceModelId, edge.targetModelId].sort();
    const key = `${pair[0]}\u0000${pair[1]}`;
    if (seenCanonicalPairs.has(key)) continue;
    seenCanonicalPairs.add(key);
    canonicalEdges.push([
      clean(edge.id),
      clean(edge.sourceModelId),
      clean(edge.targetModelId),
      clean(edge.kind),
      clean(edge.provenance),
    ].join("\t"));
  }
  await fs.mkdir(path.resolve(outputDirectory), { recursive: true });
  await Promise.all([
    fs.writeFile(path.join(outputDirectory, "nodes.tsv"), nodeRows.join("\n"), "utf8"),
    fs.writeFile(path.join(outputDirectory, "edges.tsv"), edgeRows.join("\n"), "utf8"),
    fs.writeFile(
      path.join(outputDirectory, "canonical-edges.tsv"),
      canonicalEdges.join("\n"),
      "utf8"
    ),
  ]);
  process.stderr.write(
    `graph nodes=${nodeRows.length} edges=${edgeRows.length}`
    + ` canonicalEdges=${canonicalEdges.length} out=${outputDirectory}\n`
  );
}

main().catch((error) => {
  process.stderr.write(`${error instanceof Error ? error.stack : error}\n`);
  process.exitCode = 1;
});
