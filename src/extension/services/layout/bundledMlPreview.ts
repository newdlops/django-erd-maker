import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";

interface PreviewPayload {
  graph: {
    nodes: readonly { modelId: string }[];
    structuralEdges: readonly {
      id: string; sourceModelId: string; targetModelId: string;
      kind: string; provenance: string;
    }[];
  };
  layout: { nodes: readonly { modelId: string; size: { width: number; height: number } }[] };
}

export function previewGraphSignature(payload: PreviewPayload): string {
  const byId = (a: readonly (string | number)[], b: readonly (string | number)[]) =>
    String(a[0]).localeCompare(String(b[0]));
  return createHash("sha256").update(JSON.stringify({
    nodes: payload.graph.nodes.map(node => node.modelId).sort(),
    dimensions: payload.layout.nodes.map(node => [node.modelId, node.size.width, node.size.height]).sort(byId),
    edges: payload.graph.structuralEdges.map(edge => [edge.id, edge.sourceModelId,
      edge.targetModelId, edge.kind, edge.provenance]).sort(byId),
  })).digest("hex");
}

export async function loadBundledMlPreview(extensionRoot: string, payload: PreviewPayload): Promise<{
  text: string; overviewVisual: number; individualVisual: number;
} | undefined> {
  try {
    const folder = path.join(extensionRoot, "media", "ml-preview");
    const manifest = JSON.parse(await readFile(path.join(folder, "manifest.json"), "utf8")) as {
      schemaVersion: number; graphSignature: string; layoutSha256: string;
      overviewVisual: number; individualVisual: number;
    };
    if (manifest.schemaVersion !== 1 || manifest.graphSignature !== previewGraphSignature(payload)) return undefined;
    const text = await readFile(path.join(folder, "layout.json"), "utf8");
    if (createHash("sha256").update(text).digest("hex") !== manifest.layoutSha256) return undefined;
    return { text, overviewVisual: manifest.overviewVisual, individualVisual: manifest.individualVisual };
  } catch {
    return undefined;
  }
}
