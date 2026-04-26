import type { ModelId } from "../domain/modelIdentity";
import type { DiagramGraph } from "../graph/diagramGraph";
import type { LayoutEngineMetadata, LayoutMode, LayoutSnapshot, Point } from "../graph/layoutContract";
import type { AnalyzerOutput } from "./analyzerContract";
import type { ContractVersion } from "./contractVersion";
import type { PipelineTimings } from "./pipelineTimingContract";

export interface TableViewOptions {
  hidden: boolean;
  manualPosition?: Point;
  modelId: ModelId;
  showMethodHighlights: boolean;
  showMethods: boolean;
  showProperties: boolean;
}

export interface SelectedMethodContext {
  methodName: string;
  modelId: ModelId;
}

export interface DiagramViewportSnapshot {
  panX: number;
  panY: number;
  zoom: number;
}

export interface DiagramViewportRectSnapshot {
  height: number;
  width: number;
}

export interface InitialViewState {
  collapseClusters?: boolean;
  edgeBundling?: boolean;
  layoutMode: LayoutMode;
  selectedMethodContext?: SelectedMethodContext;
  selectedModelId?: ModelId;
  tableOptions: TableViewOptions[];
  viewport?: DiagramViewportSnapshot;
}

export interface LayoutExecutionSnapshot {
  appliedMode: LayoutMode;
  durationMs?: number;
  engine: "analyzer" | "empty" | "ogdf";
  engineMetadata?: LayoutEngineMetadata;
  reason?: string;
  requestedMode: LayoutMode;
  status: "applied" | "empty" | "fallback";
}

export interface DiagramBootstrapPayload {
  analyzer: AnalyzerOutput;
  contractVersion: ContractVersion;
  graph: DiagramGraph;
  layout: LayoutSnapshot;
  layoutExecution?: LayoutExecutionSnapshot;
  layoutFailures?: Partial<Record<LayoutMode, string>>;
  timings?: PipelineTimings;
  view: InitialViewState;
}

export interface InitializeDiagramMessage {
  payload: DiagramBootstrapPayload;
  type: "diagram.initialize";
}

export interface RefreshDiagramMessage {
  payload: DiagramBootstrapPayload;
  type: "diagram.refresh";
}

export interface DiagramReadyMessage {
  type: "diagram.ready";
}

export interface DiagramLogMessage {
  details?: Record<string, boolean | number | string | undefined>;
  event: string;
  level: "debug" | "error" | "info" | "warn";
  message: string;
  timestamp: string;
  type: "diagram.log";
  version: string;
}

export interface DiagramInteractionSettingsSnapshot {
  panSpeed: number;
  zoomSpeed: number;
}

export interface RefreshViewStateSnapshot extends InitialViewState {
  viewport: DiagramViewportSnapshot;
  viewportRect: DiagramViewportRectSnapshot;
}

export type DiagramRefreshKind = "full" | "layout";

export interface RequestRefreshMessage {
  layoutMode?: LayoutMode;
  refreshKind?: DiagramRefreshKind;
  settings?: DiagramInteractionSettingsSnapshot;
  viewState?: RefreshViewStateSnapshot;
  type: "diagram.requestRefresh";
}

export interface UpdateSetupSettingsMessage {
  settings: DiagramInteractionSettingsSnapshot;
  type: "diagram.updateSetupSettings";
}

export type ExtensionToWebviewMessage = InitializeDiagramMessage | RefreshDiagramMessage;
export type WebviewToExtensionMessage =
  | DiagramReadyMessage
  | DiagramLogMessage
  | RequestRefreshMessage
  | UpdateSetupSettingsMessage;
