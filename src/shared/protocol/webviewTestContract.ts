import type { ModelId } from "../domain/modelIdentity";
import type { LayoutMode, Point } from "../graph/layoutContract";
import type { SelectedMethodContext, TableViewOptions } from "./webviewContract";

export type DiagramTestTableToggle =
  | "hidden"
  | "showMethodHighlights"
  | "showMethods"
  | "showProperties";

export type DiagramTestAction =
  | { type: "snapshot" }
  | { modelId: ModelId; type: "clickTable" }
  | { methodName: string; modelId: ModelId; type: "clickMethod" }
  | { modelId: ModelId; toggle: DiagramTestTableToggle; type: "clickTableToggle" }
  | { layoutMode: LayoutMode; type: "clickLayoutMode" }
  | { modelId: ModelId; type: "clickShowHiddenModel" }
  | { modelId: ModelId; position: Point; type: "dragTableTo" }
  | { type: "resetView" };

export interface DiagramTestViewportSnapshot {
  panX: number;
  panY: number;
  zoom: number;
}

export interface DiagramTestStateSnapshot {
  layoutMode: LayoutMode;
  selectedMethodContext?: SelectedMethodContext;
  selectedModelId?: ModelId;
  tableOptions: TableViewOptions[];
  viewport: DiagramTestViewportSnapshot;
}

export interface DiagramTestTableSnapshot {
  hidden: boolean;
  isMethodTarget: boolean;
  modelId: ModelId;
  selected: boolean;
  showMethodHighlights: boolean;
  showMethods: boolean;
  showProperties: boolean;
  transform: string;
}

export interface DiagramTestPanelSnapshot {
  activeMethodNames: string[];
  hidden: boolean;
  methodListHidden: boolean;
  modelId: ModelId;
  propertyListHidden: boolean;
  selected: boolean;
}

export interface DiagramTestEdgeSnapshot {
  edgeId: string;
  hidden: boolean;
  points: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

export interface DiagramTestOverlaySnapshot {
  active: boolean;
  hidden: boolean;
  id: string;
  methodName: string;
  sourceModelId: ModelId;
  targetModelId: ModelId;
}

export interface DiagramTestCrossingSnapshot {
  hidden: boolean;
  id: string;
}

export interface DiagramTestSnapshot {
  crossings: DiagramTestCrossingSnapshot[];
  edges: DiagramTestEdgeSnapshot[];
  hiddenModelIds: ModelId[];
  overlays: DiagramTestOverlaySnapshot[];
  panels: DiagramTestPanelSnapshot[];
  state: DiagramTestStateSnapshot;
  tables: DiagramTestTableSnapshot[];
}

export interface RunDiagramTestActionMessage {
  action: DiagramTestAction;
  requestId: string;
  type: "diagram.test.run";
}

export interface DiagramTestSnapshotMessage {
  requestId: string;
  snapshot: DiagramTestSnapshot;
  type: "diagram.test.snapshot";
}

export interface DiagramTestErrorMessage {
  message: string;
  requestId: string;
  type: "diagram.test.error";
}
