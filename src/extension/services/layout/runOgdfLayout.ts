import { execFile } from "node:child_process";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import {
  getOgdfLayoutDefinition,
  normalizeLayoutMode,
  type LayoutEngineMetadata,
  type LayoutMode,
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
  engineMetadata?: LayoutEngineMetadata;
  reason?: string;
  requestedLayoutMode: LayoutMode;
}

export async function runOgdfLayout(
  extensionRootPath: string,
  payload: DiagramBootstrapPayload,
  requestedLayoutMode: LayoutMode,
  logger?: Logger,
): Promise<OgdfLayoutResult> {
  const started = Date.now();
  const binaryPath = await resolveOgdfLayoutBinaryPath(extensionRootPath);
  const normalizedRequestedLayoutMode = normalizeLayoutMode(requestedLayoutMode);
  const layoutDefinition = getOgdfLayoutDefinition(normalizedRequestedLayoutMode);

  if (!binaryPath) {
    const reason =
      `no native OGDF binary for ${process.platform}-${process.arch}`;
    logger?.warn(
      [
        "OGDF layout skipped because no native binary was found",
        `layout=${normalizedRequestedLayoutMode}`,
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
      reason,
      requestedLayoutMode: normalizedRequestedLayoutMode,
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
        `layout=${normalizedRequestedLayoutMode}`,
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
        normalizedRequestedLayoutMode,
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

    let layout: LayoutSnapshot;
    try {
      layout = decodeLayoutSnapshot(JSON.parse(stdout), "ogdfLayout");
    } catch (error) {
      const reason = error instanceof Error ? error.message : String(error);
      throw new Error(`invalid JSON from native layout: ${reason}`);
    }

    const summary = summarizeLayout(layout);
    const metadata = layout.engineMetadata;
    logger?.info(
      [
        `OGDF layout completed in ${Date.now() - started}ms`,
        `requested=${metadata?.requestedMode ?? normalizedRequestedLayoutMode}`,
        `reported=${layout.mode}`,
        `actual=${metadata?.actualMode ?? layout.mode}`,
        `requestedAlgorithm=${metadata?.requestedAlgorithm ?? layoutDefinition.ogdfClass}`,
        `actualAlgorithm=${metadata?.actualAlgorithm ?? getOgdfLayoutDefinition(layout.mode).ogdfClass}`,
        `strategy=${metadata?.strategy ?? "exact"}`,
        ...(metadata?.strategyReason ? [`strategyReason=${metadata.strategyReason}`] : []),
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
      engineMetadata: metadata,
      layout,
      requestedLayoutMode: normalizedRequestedLayoutMode,
    };
  } catch (error) {
    const reason = formatOgdfFailureReason(error);
    logger?.warn(
      [
        "OGDF layout failed; falling back to analyzer layout",
        `requested=${normalizedRequestedLayoutMode}`,
        `fallback=${payload.layout.mode}`,
        `reason=${reason}`,
      ].join(" · "),
    );
    return {
      applied: false,
      durationMs: Date.now() - started,
      layout: payload.layout,
      reason,
      requestedLayoutMode: normalizedRequestedLayoutMode,
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
        const signal =
          "signal" in error && typeof error.signal === "string" ? error.signal : undefined;
        const killed = "killed" in error ? Boolean(error.killed) : false;
        reject(
          new OgdfExecError(
            buildExecFailureMessage(error, stderr),
            {
              code: readExecErrorCode(error),
              killed,
              signal,
              stderr,
              stdout,
              timedOut: error.message.includes("timed out") || (killed && signal === "SIGTERM"),
            },
          ),
        );
        return;
      }

      resolve({ stderr, stdout });
    });
  });
}

class OgdfExecError extends Error {
  constructor(
    message: string,
    readonly details: {
      code?: number | string | null;
      killed: boolean;
      signal?: string;
      stderr: string;
      stdout: string;
      timedOut: boolean;
    },
  ) {
    super(message);
    this.name = "OgdfExecError";
  }
}

function buildExecFailureMessage(error: Error, stderr: string): string {
  const stderrSummary = trimFailureText(stderr);
  return [error.message, stderrSummary].filter(Boolean).join(" · ");
}

function formatOgdfFailureReason(error: unknown): string {
  if (error instanceof OgdfExecError) {
    if (error.details.timedOut) {
      return `native layout timed out after ${OGDF_LAYOUT_TIMEOUT_MS}ms`;
    }

    const fragments = ["native layout process failed"];
    if (error.details.code !== undefined && error.details.code !== null) {
      fragments.push(`exitCode=${error.details.code}`);
    }
    if (error.details.signal) {
      fragments.push(`signal=${error.details.signal}`);
    }

    const stderrSummary = trimFailureText(error.details.stderr);
    if (stderrSummary) {
      fragments.push(stderrSummary);
    }

    return fragments.join(" · ");
  }

  return error instanceof Error ? error.message : String(error);
}

function trimFailureText(value: string): string {
  const trimmed = value.trim();
  if (!trimmed) {
    return "";
  }

  return trimmed.length > 240
    ? `${trimmed.slice(0, 237)}...`
    : trimmed;
}

function readExecErrorCode(error: Error): number | string | null | undefined {
  if (!("code" in error)) {
    return undefined;
  }

  const code = error.code;
  return typeof code === "number" || typeof code === "string" || code === null
    ? code
    : undefined;
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
