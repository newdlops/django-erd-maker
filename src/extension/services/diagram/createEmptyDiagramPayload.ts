import type { AnalyzerOutput } from "../../../shared/protocol/analyzerContract";
import { CONTRACT_VERSION } from "../../../shared/protocol/contractVersion";
import type { DiagramGraph } from "../../../shared/graph/diagramGraph";
import type { LayoutMode, LayoutSnapshot } from "../../../shared/graph/layoutContract";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";

export function createEmptyDiagramPayload(
  workspaceRoot: string,
  layoutMode: LayoutMode,
): DiagramBootstrapPayload {
  return {
    analyzer: emptyAnalyzerOutput(workspaceRoot),
    contractVersion: CONTRACT_VERSION,
    graph: emptyDiagramGraph(),
    layout: emptyLayoutSnapshot(layoutMode),
    layoutExecution: {
      appliedMode: layoutMode,
      engine: "empty",
      requestedMode: layoutMode,
      status: "empty",
    },
    layoutFailures: {},
    view: {
      layoutMode,
      tableOptions: [],
    },
  };
}

function emptyAnalyzerOutput(workspaceRoot: string): AnalyzerOutput {
  return {
    contractVersion: CONTRACT_VERSION,
    diagnostics: [],
    models: [],
    summary: {
      diagnosticCount: 0,
      discoveredAppCount: 0,
      discoveredModelCount: 0,
      workspaceRoot,
    },
  };
}

function emptyDiagramGraph(): DiagramGraph {
  return {
    diagnostics: [],
    methodAssociations: [],
    nodes: [],
    structuralEdges: [],
  };
}

function emptyLayoutSnapshot(layoutMode: LayoutMode): LayoutSnapshot {
  return {
    crossings: [],
    mode: layoutMode,
    nodes: [],
    routedEdges: [],
  };
}
