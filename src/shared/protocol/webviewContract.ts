import type { ModelId } from "../domain/modelIdentity";
import type { DiagramGraph } from "../graph/diagramGraph";
import type { LayoutMode, LayoutSnapshot, Point } from "../graph/layoutContract";
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

export interface InitialViewState {
  layoutMode: LayoutMode;
  selectedMethodContext?: SelectedMethodContext;
  selectedModelId?: ModelId;
  tableOptions: TableViewOptions[];
}

export interface DiagramBootstrapPayload {
  analyzer: AnalyzerOutput;
  contractVersion: ContractVersion;
  graph: DiagramGraph;
  layout: LayoutSnapshot;
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

export interface DiagramInteractionSettingsSnapshot {
  panSpeed: number;
  zoomSpeed: number;
}

export interface RequestRefreshMessage {
  settings?: DiagramInteractionSettingsSnapshot;
  type: "diagram.requestRefresh";
}

export interface UpdateSetupSettingsMessage {
  settings: DiagramInteractionSettingsSnapshot;
  type: "diagram.updateSetupSettings";
}

export type ExtensionToWebviewMessage = InitializeDiagramMessage | RefreshDiagramMessage;
export type WebviewToExtensionMessage =
  | DiagramReadyMessage
  | RequestRefreshMessage
  | UpdateSetupSettingsMessage;
