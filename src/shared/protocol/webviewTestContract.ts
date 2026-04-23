import type { ModelId } from "../domain/modelIdentity";
import type { LayoutMode, Point } from "../graph/layoutContract";
import type {
  DiagramInteractionSettingsSnapshot,
  SelectedMethodContext,
  TableViewOptions,
} from "./webviewContract";

export type DiagramTestTableToggle =
  | "hidden"
  | "showMethodHighlights"
  | "showMethods"
  | "showProperties";

export type DiagramTestSetupControlKey = keyof DiagramInteractionSettingsSnapshot;

export type DiagramTestAction =
  | { type: "snapshot" }
  | { modelId: ModelId; type: "clickTable" }
  | { methodName: string; modelId: ModelId; type: "clickMethod" }
  | { modelId: ModelId; toggle: DiagramTestTableToggle; type: "clickTableToggle" }
  | { layoutMode: LayoutMode; type: "clickLayoutMode" }
  | { type: "clickZoomAction"; zoomAction: "center" | "fit" | "in" | "out" }
  | { modelId: ModelId; type: "clickShowHiddenModel" }
  | { modelId: ModelId; position: Point; type: "dragTableTo" }
  | { modelId: ModelId; type: "pointerSelectTable" }
  | { delta: Point; modelId: ModelId; type: "pointerDragTableBy" }
  | { delta: Point; type: "pointerPanBy" }
  | { key: DiagramTestSetupControlKey; type: "setSetupControl"; value: number }
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
  settings: DiagramInteractionSettingsSnapshot;
  tableOptions: TableViewOptions[];
  viewport: DiagramTestViewportSnapshot;
}

export interface DiagramTestTableSnapshot {
  height: number;
  hidden: boolean;
  isMethodTarget: boolean;
  modelId: ModelId;
  screenRect: {
    bottom: number;
    left: number;
    right: number;
    top: number;
  };
  selected: boolean;
  showMethodHighlights: boolean;
  showMethods: boolean;
  showProperties: boolean;
  transform: string;
  width: number;
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

export interface DiagramTestLayoutButtonSnapshot {
  active: boolean;
  layoutMode: string;
  text: string;
}

export interface DiagramTestSceneSnapshot {
  canvasInkSample: {
    inkPixels: number;
    sampledPixels: number;
  };
  canvasRect: {
    height: number;
    width: number;
  };
  drawingCanvas: {
    height: number;
    rectHeight: number;
    rectWidth: number;
    width: number;
  };
  minimap: {
    canvasRect: {
      height: number;
      width: number;
    };
    viewport: {
      height: number;
      width: number;
      x: number;
      y: number;
    };
    visible: boolean;
  };
  minimapVisible: boolean;
  visibleTableIds: ModelId[];
}

export interface DiagramTestSnapshot {
  crossings: DiagramTestCrossingSnapshot[];
  edges: DiagramTestEdgeSnapshot[];
  hiddenModelIds: ModelId[];
  layoutButtons: DiagramTestLayoutButtonSnapshot[];
  overlays: DiagramTestOverlaySnapshot[];
  panels: DiagramTestPanelSnapshot[];
  scene: DiagramTestSceneSnapshot;
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
