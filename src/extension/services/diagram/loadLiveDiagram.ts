import { normalizeLayoutMode, type LayoutMode } from "../../../shared/graph/layoutContract";
import { mergePipelineTimings } from "../../../shared/protocol/mergePipelineTimings";
import type {
  DiagramBootstrapPayload,
  LayoutExecutionSnapshot,
} from "../../../shared/protocol/webviewContract";
import type { DjangoWorkspaceDiscoveryResult } from "../discovery/discoveryTypes";
import { runAnalyzerBootstrap } from "../analyzer/runAnalyzerBootstrap";
import { runOgdfLayout } from "../layout/runOgdfLayout";
import type { Logger } from "../logging/logger";
import { createEmptyDiagramPayload } from "./createEmptyDiagramPayload";

export interface LiveDiagramResult {
  basePayload: DiagramBootstrapPayload;
  discovery: DjangoWorkspaceDiscoveryResult;
  layoutFailures: Partial<Record<LayoutMode, string>>;
  payload: DiagramBootstrapPayload;
}

export async function relayoutLiveDiagram(
  extensionRootPath: string,
  current: LiveDiagramResult,
  layoutMode: LayoutMode,
  logger?: Logger,
  requestId?: number,
): Promise<LiveDiagramResult> {
  const requestedLayoutMode = normalizeLayoutMode(layoutMode);
  const { layoutFailures, payload } = await applyRequestedLayout(
    extensionRootPath,
    current.basePayload,
    requestedLayoutMode,
    current.layoutFailures,
    logger,
    requestId,
  );

  return {
    basePayload: current.basePayload,
    discovery: current.discovery,
    layoutFailures,
    payload,
  };
}

export async function loadLiveDiagram(
  extensionRootPath: string,
  discovery: DjangoWorkspaceDiscoveryResult,
  layoutMode: LayoutMode,
  discoveryMs?: number,
  logger?: Logger,
  requestId?: number,
): Promise<LiveDiagramResult> {
  const requestedLayoutMode = normalizeLayoutMode(layoutMode);
  const basePayload =
    discovery.candidateModules.length > 0
      ? await loadAnalyzerPayload(extensionRootPath, discovery, requestedLayoutMode, logger)
      : createEmptyDiagramPayload(discovery.selectedRoot, requestedLayoutMode);

  if (discovery.candidateModules.length === 0) {
    logger?.warn("Analyzer skipped because discovery returned no candidate modules.");
  }

  basePayload.timings = mergePipelineTimings(basePayload.timings, {
    discoveryMs,
  });

  const { layoutFailures, payload } = await applyRequestedLayout(
    extensionRootPath,
    basePayload,
    requestedLayoutMode,
    {},
    logger,
    requestId,
  );

  payload.timings = mergePipelineTimings(payload.timings, {
    discoveryMs,
  });

  return {
    basePayload,
    discovery,
    layoutFailures,
    payload,
  };
}

async function loadAnalyzerPayload(
  extensionRootPath: string,
  discovery: DjangoWorkspaceDiscoveryResult,
  layoutMode: LayoutMode,
  logger?: Logger,
): Promise<DiagramBootstrapPayload> {
  const analyzerResult = await runAnalyzerBootstrap(
    extensionRootPath,
    discovery,
    layoutMode,
    logger,
  );
  const payload = clonePayload(analyzerResult.payload);
  payload.layoutExecution = createLayoutExecution({
    appliedMode: payload.layout.mode,
    durationMs: analyzerResult.durationMs,
    engine: "analyzer",
    requestedMode: layoutMode,
    status: "fallback",
  });
  payload.layoutFailures = {};
  payload.timings = mergePipelineTimings(payload.timings, {
    analyzerBootstrapMs: analyzerResult.durationMs,
  });
  payload.view.layoutMode = normalizeLayoutMode(payload.layout.mode);
  return payload;
}

async function applyRequestedLayout(
  extensionRootPath: string,
  basePayload: DiagramBootstrapPayload,
  requestedLayoutMode: LayoutMode,
  previousLayoutFailures: Partial<Record<LayoutMode, string>>,
  logger?: Logger,
  requestId?: number,
): Promise<{
  layoutFailures: Partial<Record<LayoutMode, string>>;
  payload: DiagramBootstrapPayload;
}> {
  const payload = clonePayload(basePayload);
  const nextLayoutFailures = cloneLayoutFailures(previousLayoutFailures);

  payload.layoutFailures = cloneLayoutFailures(nextLayoutFailures);

  if (payload.layout.nodes.length === 0) {
    payload.layoutExecution = createLayoutExecution({
      appliedMode: payload.layout.mode,
      engine: "empty",
      requestedMode: requestedLayoutMode,
      status: "empty",
    });
    payload.view.layoutMode = normalizeLayoutMode(payload.layout.mode);
    return {
      layoutFailures: nextLayoutFailures,
      payload,
    };
  }

  const ogdfResult = await runOgdfLayout(
    extensionRootPath,
    payload,
    requestedLayoutMode,
    logger,
    requestId,
  );

  payload.timings = mergePipelineTimings(payload.timings, {
    ogdfLayoutMs: ogdfResult.durationMs,
  });

  if (ogdfResult.applied) {
    payload.layout = ogdfResult.layout;
    payload.layoutExecution = createLayoutExecution({
      appliedMode: ogdfResult.layout.mode,
      durationMs: ogdfResult.durationMs,
      engine: "ogdf",
      engineMetadata: ogdfResult.engineMetadata,
      requestedMode: ogdfResult.requestedLayoutMode,
      status: "applied",
    });
    payload.view.layoutMode = normalizeLayoutMode(ogdfResult.layout.mode);
    delete nextLayoutFailures[ogdfResult.requestedLayoutMode];
  } else {
    const reason = ogdfResult.reason ?? "unknown OGDF layout failure";
    payload.layoutExecution = createLayoutExecution({
      appliedMode: payload.layout.mode,
      durationMs: ogdfResult.durationMs,
      engine: "analyzer",
      reason,
      requestedMode: ogdfResult.requestedLayoutMode,
      status: "fallback",
    });
    payload.view.layoutMode = normalizeLayoutMode(payload.layout.mode);
    nextLayoutFailures[ogdfResult.requestedLayoutMode] = reason;
  }

  payload.layoutFailures = cloneLayoutFailures(nextLayoutFailures);

  return {
    layoutFailures: nextLayoutFailures,
    payload,
  };
}

function cloneLayoutFailures(
  layoutFailures: Partial<Record<LayoutMode, string>>,
): Partial<Record<LayoutMode, string>> {
  return { ...layoutFailures };
}

function createLayoutExecution(
  execution: LayoutExecutionSnapshot,
): LayoutExecutionSnapshot {
  return {
    ...execution,
  };
}

function clonePayload(payload: DiagramBootstrapPayload): DiagramBootstrapPayload {
  return JSON.parse(JSON.stringify(payload)) as DiagramBootstrapPayload;
}
