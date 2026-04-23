import type { LayoutMode } from "../../../shared/graph/layoutContract";
import { mergePipelineTimings } from "../../../shared/protocol/mergePipelineTimings";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";
import type { DjangoWorkspaceDiscoveryResult } from "../discovery/discoveryTypes";
import { runOgdfLayout } from "../layout/runOgdfLayout";
import type { Logger } from "../logging/logger";
import { runAnalyzerBootstrap } from "../analyzer/runAnalyzerBootstrap";
import { createEmptyDiagramPayload } from "./createEmptyDiagramPayload";

export interface LiveDiagramResult {
  discovery: DjangoWorkspaceDiscoveryResult;
  payload: DiagramBootstrapPayload;
}

export async function relayoutLiveDiagram(
  extensionRootPath: string,
  current: LiveDiagramResult,
  layoutMode: LayoutMode,
  logger?: Logger,
): Promise<LiveDiagramResult> {
  const payload = clonePayload(current.payload);
  payload.layout.mode = layoutMode;
  payload.view.layoutMode = layoutMode;

  const ogdfResult = await runOgdfLayout(
    extensionRootPath,
    payload,
    logger,
  );
  payload.layout = ogdfResult.layout;
  payload.view.layoutMode = ogdfResult.layout.mode;
  payload.timings = mergePipelineTimings(payload.timings, {
    ...(ogdfResult.applied ? { ogdfLayoutMs: ogdfResult.durationMs } : {}),
  });

  return {
    discovery: current.discovery,
    payload,
  };
}

export async function loadLiveDiagram(
  extensionRootPath: string,
  discovery: DjangoWorkspaceDiscoveryResult,
  layoutMode: LayoutMode,
  discoveryMs?: number,
  logger?: Logger,
): Promise<LiveDiagramResult> {
  const payload =
    discovery.candidateModules.length > 0
      ? await loadAnalyzerPayload(extensionRootPath, discovery, layoutMode, logger)
      : createEmptyDiagramPayload(discovery.selectedRoot, layoutMode);
  if (discovery.candidateModules.length === 0) {
    logger?.warn("Analyzer skipped because discovery returned no candidate modules.");
  }
  payload.timings = mergePipelineTimings(payload.timings, {
    discoveryMs,
  });

  return {
    discovery,
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
  const ogdfResult = await runOgdfLayout(
    extensionRootPath,
    analyzerResult.payload,
    logger,
  );
  analyzerResult.payload.layout = ogdfResult.layout;
  analyzerResult.payload.timings = mergePipelineTimings(analyzerResult.payload.timings, {
    analyzerBootstrapMs: analyzerResult.durationMs,
    ...(ogdfResult.applied ? { ogdfLayoutMs: ogdfResult.durationMs } : {}),
  });
  return analyzerResult.payload;
}

function clonePayload(payload: DiagramBootstrapPayload): DiagramBootstrapPayload {
  return JSON.parse(JSON.stringify(payload)) as DiagramBootstrapPayload;
}
