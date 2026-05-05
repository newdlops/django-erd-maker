import { execFile } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import type { StructuralGraphEdge } from "../../../shared/graph/diagramGraph";
import {
  DEFAULT_EDGE_ROUTING,
  getOgdfLayoutDefinition,
  normalizeLayoutMode,
  type EdgeRoutingStyle,
  type LayoutEngineMetadata,
  type LayoutMode,
  type LayoutSnapshot,
} from "../../../shared/graph/layoutContract";
import { decodeLayoutSnapshot } from "../../../shared/protocol/decodeDiagramBootstrap";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";
import type { Logger } from "../logging/logger";
import { consolidateEdges } from "./consolidateEdges";
import { resolveOgdfLayoutBinaryPath } from "./resolveOgdfLayoutBinaryPath";

const OGDF_LAYOUT_TIMEOUT_MS = 300_000;

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
  requestId?: number,
  edgeRouting: EdgeRoutingStyle = DEFAULT_EDGE_ROUTING,
  clusterGraphLayout: boolean = false,
  bubbleLayout: boolean = false,
): Promise<OgdfLayoutResult> {
  const envEdgeRouting = process.env.DJANGO_ERD_EDGE_ROUTING;
  const envEdgeRoutingValid =
    envEdgeRouting === "straight"
    || envEdgeRouting === "straight_smart"
    || envEdgeRouting === "orthogonal";
  const effectiveEdgeRouting: EdgeRoutingStyle = envEdgeRoutingValid
    ? (envEdgeRouting as EdgeRoutingStyle)
    : edgeRouting;
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
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
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

  const requestDirectory = await mkdtemp(
    path.join(os.tmpdir(), `django-erd-ogdf-${normalizedRequestedLayoutMode}-`),
  );
  const nodesPath = path.join(requestDirectory, "nodes.tsv");
  const edgesPath = path.join(requestDirectory, "edges.tsv");
  const preserveInputs = Boolean(process.env.DJANGO_ERD_PRESERVE_LAYOUT_INPUTS);
  let preserveRequestDirectory = preserveInputs;

  // Edge consolidation (DJERD_CONSOLIDATE_EDGES=1, default off):
  // group multiple edges between the same (source, target) directional
  // pair into a single representative edge before sending to the OGDF
  // binary. The representative carries the first underlying edge's id;
  // the remaining edges have no route in the layout output (they will
  // share the representative's visual line in the webview when render-
  // side badges are added later).
  const consolidateEnv = process.env.DJERD_CONSOLIDATE_EDGES;
  const consolidateActive =
    consolidateEnv !== undefined && consolidateEnv !== "0";
  const layoutEdges: readonly StructuralGraphEdge[] = consolidateActive
    ? consolidateEdges(payload.graph.structuralEdges).layoutEdges
    : payload.graph.structuralEdges;

  try {
    await writeFile(nodesPath, serializeNodes(payload), "utf8");
    await writeFile(edgesPath, serializeEdges(layoutEdges), "utf8");

    if (preserveInputs) {
      logger?.info(
        `OGDF layout inputs preserved at ${requestDirectory} (DJANGO_ERD_PRESERVE_LAYOUT_INPUTS=${process.env.DJANGO_ERD_PRESERVE_LAYOUT_INPUTS})`,
      );
    }

    logger?.info(
      [
        "OGDF layout starting",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `binary=${binaryPath}`,
        `layout=${normalizedRequestedLayoutMode}`,
        `label=${layoutDefinition.label}`,
        `family=${layoutDefinition.family}`,
        `ogdfClass=${layoutDefinition.ogdfClass}`,
        `edgeRouting=${effectiveEdgeRouting}`,
        `edgeRoutingSource=${envEdgeRoutingValid ? "env(DJANGO_ERD_EDGE_ROUTING)" : "default"}`,
        `nodes=${payload.layout.nodes.length}`,
        `edges=${layoutEdges.length}`
          + (consolidateActive
            ? ` (consolidated from ${payload.graph.structuralEdges.length})`
            : ""),
      ].join(" · "),
    );

    // ML polish round-trip:
    //   DJERD_LAYOUT_FROM_FILE=<path>   load layout JSON from disk, skip
    //                                   C++. Used to visually verify
    //                                   ML-polished output. If file
    //                                   doesn't exist, falls through to
    //                                   normal C++ run (so first launch
    //                                   can populate it via
    //                                   DJERD_LAYOUT_OUTPUT_FILE).
    //   DJERD_LAYOUT_OUTPUT_FILE=<path> save C++ output JSON to disk
    //                                   alongside normal pipeline. Used
    //                                   to capture input for the polish
    //                                   round-trip.
    const layoutFromFile = process.env.DJERD_LAYOUT_FROM_FILE;
    const layoutOutputFile = process.env.DJERD_LAYOUT_OUTPUT_FILE;
    let stdout = "";
    let stderr = "";
    let loadedFromFile = false;
    if (layoutFromFile) {
      try {
        stdout = await readFile(layoutFromFile, "utf8");
        stderr = "";
        loadedFromFile = true;
        logger?.info(`OGDF layout loaded from file: ${layoutFromFile}`);
      } catch {
        logger?.warn(
          `OGDF layout file not found, running C++ binary: ${layoutFromFile}`,
        );
      }
    }
    if (!loadedFromFile) {
      ({ stderr, stdout } = await execFileAsync(
        binaryPath,
        [
          "layout",
          "--mode",
          normalizedRequestedLayoutMode,
          "--nodes-file",
          nodesPath,
          "--edges-file",
          edgesPath,
          "--edge-routing",
          effectiveEdgeRouting,
          ...(clusterGraphLayout ? ["--cluster-graph", "1"] : []),
          ...(bubbleLayout ? ["--bubble", "1"] : []),
        ],
        {
          cwd: extensionRootPath,
          maxBuffer: 100 * 1024 * 1024,
          timeout: OGDF_LAYOUT_TIMEOUT_MS,
        },
      ));
      if (layoutOutputFile) {
        try {
          await writeFile(layoutOutputFile, stdout, "utf8");
          logger?.info(`OGDF layout output saved to: ${layoutOutputFile}`);
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          logger?.warn(`OGDF layout output save failed: ${msg}`);
        }
      }
    }

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
        `leafBundles=${metadata?.leafBundles?.length ?? "absent"}`,
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `requested=${metadata?.requestedMode ?? normalizedRequestedLayoutMode}`,
        `reported=${layout.mode}`,
        `actual=${metadata?.actualMode ?? layout.mode}`,
        `requestedAlgorithm=${metadata?.requestedAlgorithm ?? layoutDefinition.ogdfClass}`,
        `actualAlgorithm=${metadata?.actualAlgorithm ?? getOgdfLayoutDefinition(layout.mode).ogdfClass}`,
        `strategy=${metadata?.strategy ?? "exact"}`,
        ...(metadata?.strategyReason ? [`strategyReason=${metadata.strategyReason}`] : []),
        `nodes=${layout.nodes.length}`,
        `routedEdges=${layout.routedEdges.length}`,
        `routePoints=${summary.routePointCount}`,
        ...(metadata?.routeSegments !== undefined ? [`routeSegments=${metadata.routeSegments}`] : []),
        ...(metadata?.nodeOverlaps !== undefined ? [`nodeOverlaps=${metadata.nodeOverlaps}`] : []),
        ...(metadata?.nodeSpacingOverlaps !== undefined ? [`nodeSpacingOverlaps=${metadata.nodeSpacingOverlaps}`] : []),
        ...(metadata?.edgeCrossings !== undefined ? [`edgeCrossings=${metadata.edgeCrossings}`] : []),
        ...(metadata?.edgeNodeIntersections !== undefined ? [`edgeNodeIntersections=${metadata.edgeNodeIntersections}`] : []),
        ...(metadata?.overlappingEdges !== undefined ? [`overlappingEdges=${metadata.overlappingEdges}`] : []),
        ...(metadata?.edgeSegmentOverlaps !== undefined ? [`edgeSegmentOverlaps=${metadata.edgeSegmentOverlaps}`] : []),
        ...(metadata?.bundleEdgeIntersections !== undefined ? [`bundleEdgeIntersections=${metadata.bundleEdgeIntersections}`] : []),
        ...(metadata?.bundleNodeOverlaps !== undefined ? [`bundleNodeOverlaps=${metadata.bundleNodeOverlaps}`] : []),
        ...(metadata?.visualCrossings !== undefined ? [`visualCrossings=${metadata.visualCrossings}`] : []),
        `crossings=${layout.crossings.length}`,
        `nodeBBoxWidth=${summary.nodeBBoxWidth.toFixed(1)}`,
        `nodeBBoxHeight=${summary.nodeBBoxHeight.toFixed(1)}`,
        `routeBBoxWidth=${summary.routeBBoxWidth.toFixed(1)}`,
        `routeBBoxHeight=${summary.routeBBoxHeight.toFixed(1)}`,
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
    preserveRequestDirectory = true;
    await writeFailureArtifacts(requestDirectory, error, reason);
    logger?.warn(
      [
        "OGDF layout failed; falling back to analyzer layout",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `requested=${normalizedRequestedLayoutMode}`,
        `fallback=${payload.layout.mode}`,
        `reason=${reason}`,
      ].join(" · "),
    );
    logger?.warn(
      [
        "OGDF layout input preserved for debugging",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `directory=${requestDirectory}`,
        `nodesFile=${nodesPath}`,
        `edgesFile=${edgesPath}`,
        `reproduce=${buildOgdfReproductionCommand(
          binaryPath,
          normalizedRequestedLayoutMode,
          nodesPath,
          edgesPath,
        )}`,
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
    if (!preserveRequestDirectory) {
      await rm(requestDirectory, { force: true, recursive: true });
    }
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

async function writeFailureArtifacts(
  directory: string,
  error: unknown,
  reason: string,
): Promise<void> {
  const details = error instanceof OgdfExecError ? error.details : undefined;
  const artifactWrites = [
    writeFile(
      path.join(directory, "failure.json"),
      `${JSON.stringify(
        {
          code: details?.code,
          killed: details?.killed,
          reason,
          signal: details?.signal,
          timedOut: details?.timedOut,
        },
        null,
        2,
      )}\n`,
      "utf8",
    ),
  ];

  if (details) {
    artifactWrites.push(
      writeFile(path.join(directory, "stdout.txt"), details.stdout, "utf8"),
      writeFile(path.join(directory, "stderr.txt"), details.stderr, "utf8"),
    );
  }

  await Promise.allSettled(artifactWrites);
}

function buildOgdfReproductionCommand(
  binaryPath: string,
  mode: LayoutMode,
  nodesPath: string,
  edgesPath: string,
): string {
  return [
    shellArg(binaryPath),
    "layout",
    "--mode",
    shellArg(mode),
    "--nodes-file",
    shellArg(nodesPath),
    "--edges-file",
    shellArg(edgesPath),
  ].join(" ");
}

function shellArg(value: string): string {
  return `'${value.replace(/'/g, `'\\''`)}'`;
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
  const appLabelByModelId = new Map(
    payload.graph.nodes.map((node) => [node.modelId, node.appLabel]),
  );
  return payload.layout.nodes
    .map((node) =>
      [
        tsvCell(node.modelId),
        numberCell(node.size.width),
        numberCell(node.size.height),
        numberCell(node.position.x),
        numberCell(node.position.y),
        tsvCell(appLabelByModelId.get(node.modelId) ?? ""),
      ].join("\t"),
    )
    .join("\n");
}

function serializeEdges(edges: readonly StructuralGraphEdge[]): string {
  return edges
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
  nodeBBoxHeight: number;
  nodeBBoxWidth: number;
  routeBBoxHeight: number;
  routeBBoxWidth: number;
  routePointCount: number;
} {
  const nodeBounds = emptyBounds();
  const routeBounds = emptyBounds();
  const combinedBounds = emptyBounds();
  let routePointCount = 0;

  for (const node of layout.nodes) {
    updateBounds(nodeBounds, node.position.x, node.position.y);
    updateBounds(nodeBounds, node.position.x + node.size.width, node.position.y + node.size.height);
    updateBounds(combinedBounds, node.position.x, node.position.y);
    updateBounds(combinedBounds, node.position.x + node.size.width, node.position.y + node.size.height);
  }

  for (const edge of layout.routedEdges) {
    for (const point of edge.points) {
      routePointCount += 1;
      updateBounds(routeBounds, point.x, point.y);
      updateBounds(combinedBounds, point.x, point.y);
    }
  }

  return {
    bboxHeight: boundsHeight(combinedBounds),
    bboxWidth: boundsWidth(combinedBounds),
    nodeBBoxHeight: boundsHeight(nodeBounds),
    nodeBBoxWidth: boundsWidth(nodeBounds),
    routeBBoxHeight: boundsHeight(routeBounds),
    routeBBoxWidth: boundsWidth(routeBounds),
    routePointCount,
  };
}

function emptyBounds(): {
  maxX: number;
  maxY: number;
  minX: number;
  minY: number;
} {
  return {
    maxX: Number.NEGATIVE_INFINITY,
    maxY: Number.NEGATIVE_INFINITY,
    minX: Number.POSITIVE_INFINITY,
    minY: Number.POSITIVE_INFINITY,
  };
}

function updateBounds(
  bounds: { maxX: number; maxY: number; minX: number; minY: number },
  x: number,
  y: number,
): void {
  bounds.minX = Math.min(bounds.minX, x);
  bounds.minY = Math.min(bounds.minY, y);
  bounds.maxX = Math.max(bounds.maxX, x);
  bounds.maxY = Math.max(bounds.maxY, y);
}

function boundsWidth(bounds: { maxX: number; minX: number }): number {
  return Number.isFinite(bounds.minX) && Number.isFinite(bounds.maxX)
    ? Math.max(0, bounds.maxX - bounds.minX)
    : 0;
}

function boundsHeight(bounds: { maxY: number; minY: number }): number {
  return Number.isFinite(bounds.minY) && Number.isFinite(bounds.maxY)
    ? Math.max(0, bounds.maxY - bounds.minY)
    : 0;
}
