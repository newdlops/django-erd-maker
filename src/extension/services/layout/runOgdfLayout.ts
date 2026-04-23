import { execFile } from "node:child_process";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import type { LayoutSnapshot } from "../../../shared/graph/layoutContract";
import { decodeLayoutSnapshot } from "../../../shared/protocol/decodeDiagramBootstrap";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";
import type { Logger } from "../logging/logger";
import { resolveOgdfLayoutBinaryPath } from "./resolveOgdfLayoutBinaryPath";

const OGDF_LAYOUT_TIMEOUT_MS = 20_000;

export interface OgdfLayoutResult {
  applied: boolean;
  durationMs: number;
  layout: LayoutSnapshot;
}

export async function runOgdfLayout(
  extensionRootPath: string,
  payload: DiagramBootstrapPayload,
  logger?: Logger,
): Promise<OgdfLayoutResult> {
  const started = Date.now();
  const binaryPath = await resolveOgdfLayoutBinaryPath(extensionRootPath);

  if (!binaryPath) {
    logger?.warn(
      [
        "OGDF layout skipped because no native binary was found",
        `platform=${process.platform}`,
        `arch=${process.arch}`,
      ].join(" · "),
    );
    return {
      applied: false,
      durationMs: Date.now() - started,
      layout: payload.layout,
    };
  }

  const requestDirectory = await mkdtemp(path.join(os.tmpdir(), "django-erd-ogdf-"));
  const nodesPath = path.join(requestDirectory, "nodes.tsv");
  const edgesPath = path.join(requestDirectory, "edges.tsv");

  try {
    await writeFile(nodesPath, serializeNodes(payload), "utf8");
    await writeFile(edgesPath, serializeEdges(payload), "utf8");

    logger?.info(
      [
        "OGDF layout starting",
        `binary=${binaryPath}`,
        `layout=${payload.layout.mode}`,
        `nodes=${payload.layout.nodes.length}`,
        `edges=${payload.graph.structuralEdges.length}`,
      ].join(" · "),
    );

    const { stderr, stdout } = await execFileAsync(
      binaryPath,
      [
        "layout",
        "--mode",
        payload.layout.mode,
        "--nodes-file",
        nodesPath,
        "--edges-file",
        edgesPath,
      ],
      {
        cwd: extensionRootPath,
        maxBuffer: 100 * 1024 * 1024,
        timeout: OGDF_LAYOUT_TIMEOUT_MS,
      },
    );

    if (stderr.trim().length > 0) {
      logger?.warn(`OGDF stderr: ${stderr.trim()}`);
    }

    const layout = decodeLayoutSnapshot(JSON.parse(stdout), "ogdfLayout");
    logger?.info(
      [
        `OGDF layout completed in ${Date.now() - started}ms`,
        `nodes=${layout.nodes.length}`,
        `routedEdges=${layout.routedEdges.length}`,
        `crossings=${layout.crossings.length}`,
      ].join(" · "),
    );

    return {
      applied: true,
      durationMs: Date.now() - started,
      layout,
    };
  } catch (error) {
    const reason = error instanceof Error ? error.message : String(error);
    logger?.warn(`OGDF layout failed; falling back to analyzer layout: ${reason}`);
    return {
      applied: false,
      durationMs: Date.now() - started,
      layout: payload.layout,
    };
  } finally {
    await rm(requestDirectory, { force: true, recursive: true });
  }
}

function execFileAsync(
  filePath: string,
  args: string[],
  options: {
    cwd: string;
    maxBuffer: number;
    timeout: number;
  },
): Promise<{ stderr: string; stdout: string }> {
  return new Promise((resolve, reject) => {
    execFile(filePath, args, options, (error, stdout, stderr) => {
      if (error) {
        const details = [error.message, stderr.trim()].filter(Boolean).join("\n");
        reject(new Error(details));
        return;
      }

      resolve({ stderr, stdout });
    });
  });
}

function serializeNodes(payload: DiagramBootstrapPayload): string {
  return payload.layout.nodes
    .map((node) =>
      [
        tsvCell(node.modelId),
        numberCell(node.size.width),
        numberCell(node.size.height),
        numberCell(node.position.x),
        numberCell(node.position.y),
      ].join("\t"),
    )
    .join("\n");
}

function serializeEdges(payload: DiagramBootstrapPayload): string {
  return payload.graph.structuralEdges
    .map((edge) =>
      [
        tsvCell(edge.id),
        tsvCell(edge.sourceModelId),
        tsvCell(edge.targetModelId),
        tsvCell(edge.kind),
        tsvCell(edge.provenance),
      ].join("\t"),
    )
    .join("\n");
}

function numberCell(value: number): string {
  return Number.isFinite(value) ? String(value) : "0";
}

function tsvCell(value: string): string {
  return value.replace(/\t/g, " ").replace(/\r?\n/g, " ");
}
