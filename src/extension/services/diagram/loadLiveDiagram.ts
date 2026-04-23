import type { LayoutMode } from "../../../shared/graph/layoutContract";
import { mergePipelineTimings } from "../../../shared/protocol/mergePipelineTimings";
import type {
  DiagramBootstrapPayload,
  DiagramInteractionSettingsSnapshot,
} from "../../../shared/protocol/webviewContract";
import type { DjangoWorkspaceDiscoveryResult } from "../discovery/discoveryTypes";
import type { Logger } from "../logging/logger";
import { runAnalyzerBootstrap } from "../analyzer/runAnalyzerBootstrap";
import { createEmptyDiagramPayload } from "./createEmptyDiagramPayload";
import type { GraphvizRuntimeResolution } from "../graphviz/graphvizRuntime";

export interface LiveDiagramResult {
  discovery: DjangoWorkspaceDiscoveryResult;
  payload: DiagramBootstrapPayload;
}

export async function loadLiveDiagram(
  extensionRootPath: string,
  discovery: DjangoWorkspaceDiscoveryResult,
  layoutMode: LayoutMode,
  discoveryMs?: number,
  logger?: Logger,
  interactionSettings?: DiagramInteractionSettingsSnapshot,
  graphvizRuntime?: GraphvizRuntimeResolution,
): Promise<LiveDiagramResult> {
  const payload =
    discovery.candidateModules.length > 0
      ? await loadAnalyzerPayload(
          extensionRootPath,
          discovery,
          layoutMode,
          logger,
          interactionSettings,
          graphvizRuntime,
        )
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
  interactionSettings?: DiagramInteractionSettingsSnapshot,
  graphvizRuntime?: GraphvizRuntimeResolution,
): Promise<DiagramBootstrapPayload> {
  const analyzerResult = await runAnalyzerBootstrap(
    extensionRootPath,
    discovery,
    layoutMode,
    logger,
    interactionSettings,
    graphvizRuntime,
  );
  analyzerResult.payload.timings = mergePipelineTimings(analyzerResult.payload.timings, {
    analyzerBootstrapMs: analyzerResult.durationMs,
  });
  return analyzerResult.payload;
}
