import { execFile } from "node:child_process";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import {
  getOgdfLayoutDefinition,
  normalizeLayoutMode,
  type LayoutSnapshot,
} from "../../../shared/graph/layoutContract";
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
  const requestedLayoutMode = normalizeLayoutMode(payload.view.layoutMode ?? payload.layout.mode);
  const layoutDefinition = getOgdfLayoutDefinition(requestedLayoutMode);

  if (!binaryPath) {
    logger?.warn(
      [
        "OGDF layout skipped because no native binary was found",
        `layout=${requestedLayoutMode}`,
        `label=${layoutDefinition.label}`,
        `ogdfClass=${layoutDefinition.ogdfClass}`,
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
        `layout=${requestedLayoutMode}`,
        `label=${layoutDefinition.label}`,
        `family=${layoutDefinition.family}`,
        `ogdfClass=${layoutDefinition.ogdfClass}`,
        `nodes=${payload.layout.nodes.length}`,
        `edges=${payload.graph.structuralEdges.length}`,
      ].join(" · "),
    );

    const { stderr, stdout } = await execFileAsync(
      binaryPath,
      [
        "layout",
        "--mode",
        requestedLayoutMode,
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
    const summary = summarizeLayout(layout);
    logger?.info(
      [
        `OGDF layout completed in ${Date.now() - started}ms`,
        `layout=${layout.mode}`,
        `label=${getOgdfLayoutDefinition(layout.mode).label}`,
        `ogdfClass=${getOgdfLayoutDefinition(layout.mode).ogdfClass}`,
        `nodes=${layout.nodes.length}`,
        `routedEdges=${layout.routedEdges.length}`,
        `crossings=${layout.crossings.length}`,
        `bboxWidth=${summary.bboxWidth.toFixed(1)}`,
        `bboxHeight=${summary.bboxHeight.toFixed(1)}`,
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

function summarizeLayout(layout: LayoutSnapshot): {
  bboxHeight: number;
  bboxWidth: number;
} {
  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;

  for (const node of layout.nodes) {
    minX = Math.min(minX, node.position.x);
    minY = Math.min(minY, node.position.y);
    maxX = Math.max(maxX, node.position.x + node.size.width);
    maxY = Math.max(maxY, node.position.y + node.size.height);
  }

  for (const edge of layout.routedEdges) {
    for (const point of edge.points) {
      minX = Math.min(minX, point.x);
      minY = Math.min(minY, point.y);
      maxX = Math.max(maxX, point.x);
      maxY = Math.max(maxY, point.y);
    }
  }

  if (!Number.isFinite(minX) || !Number.isFinite(minY) || !Number.isFinite(maxX) || !Number.isFinite(maxY)) {
    return {
      bboxHeight: 0,
      bboxWidth: 0,
    };
  }

  return {
    bboxHeight: Math.max(0, maxY - minY),
    bboxWidth: Math.max(0, maxX - minX),
  };
}
