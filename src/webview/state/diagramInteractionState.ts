import type { ModelId } from "../../shared/domain/modelIdentity";
import type { LayoutMode, Point } from "../../shared/graph/layoutContract";
import type {
  InitialViewState,
  SelectedMethodContext,
  TableViewOptions,
} from "../../shared/protocol/webviewContract";
import {
  clampInteractionSetting,
  DEFAULT_INTERACTION_SETTINGS,
  normalizeInteractionSettings,
  type DiagramInteractionSettingKey,
  type DiagramInteractionSettings,
} from "./interactionSettings";

export interface DiagramViewportState {
  panX: number;
  panY: number;
  zoom: number;
}

export interface DiagramInteractionState extends InitialViewState {
  settings: DiagramInteractionSettings;
  viewport: DiagramViewportState;
}

export type DiagramInteractionAction =
  | { modelId: ModelId; type: "select-model" }
  | { methodName: string; modelId: ModelId; type: "toggle-method" }
  | { layoutMode: LayoutMode; type: "set-layout-mode" }
  | { hidden: boolean; modelId: ModelId; type: "set-table-hidden" }
  | { manualPosition?: Point; modelId: ModelId; type: "set-table-manual-position" }
  | { modelId: ModelId; showMethodHighlights: boolean; type: "set-table-show-method-highlights" }
  | { modelId: ModelId; showMethods: boolean; type: "set-table-show-methods" }
  | { modelId: ModelId; showProperties: boolean; type: "set-table-show-properties" }
  | { key: DiagramInteractionSettingKey; type: "set-interaction-setting"; value: number }
  | { panX: number; panY: number; type: "set-viewport-pan" }
  | { type: "set-viewport-zoom"; zoom: number }
  | { initialState: DiagramInteractionState; type: "reset-view" };

export const DEFAULT_VIEWPORT: DiagramViewportState = {
  panX: 32,
  panY: 24,
  zoom: 1,
};

export function createDiagramInteractionState(
  view: InitialViewState,
  settingsOverride?: Partial<DiagramInteractionSettings>,
): DiagramInteractionState {
  return {
    layoutMode: view.layoutMode,
    settings: normalizeInteractionSettings(settingsOverride ?? DEFAULT_INTERACTION_SETTINGS),
    selectedMethodContext: cloneSelectedMethodContext(view.selectedMethodContext),
    selectedModelId: view.selectedModelId,
    tableOptions: view.tableOptions.map(cloneTableViewOptions),
    viewport: view.viewport ? { ...view.viewport } : { ...DEFAULT_VIEWPORT },
  };
}

export function reduceDiagramInteractionState(
  state: DiagramInteractionState,
  action: DiagramInteractionAction,
): DiagramInteractionState {
  switch (action.type) {
    case "reset-view":
      return {
        ...cloneDiagramInteractionState(action.initialState),
        settings: { ...state.settings },
      };
    case "select-model":
      return {
        ...state,
        selectedMethodContext:
          state.selectedMethodContext?.modelId === action.modelId
            ? cloneSelectedMethodContext(state.selectedMethodContext)
            : undefined,
        selectedModelId: action.modelId,
      };
    case "toggle-method":
      return {
        ...state,
        selectedMethodContext: nextSelectedMethodContext(
          state.selectedMethodContext,
          action.modelId,
          action.methodName,
        ),
        selectedModelId: action.modelId,
      };
    case "set-layout-mode":
      return {
        ...state,
        layoutMode: action.layoutMode,
      };
    case "set-table-hidden":
      return withTableOptions(state, action.modelId, (options) => ({
        ...options,
        hidden: action.hidden,
      }));
    case "set-table-manual-position":
      return withTableOptions(state, action.modelId, (options) => ({
        ...options,
        manualPosition: action.manualPosition
          ? { ...action.manualPosition }
          : undefined,
      }));
    case "set-table-show-method-highlights":
      return sanitizeSelections(
        withTableOptions(state, action.modelId, (options) => ({
          ...options,
          showMethodHighlights: action.showMethodHighlights,
        })),
      );
    case "set-table-show-methods":
      return sanitizeSelections(
        withTableOptions(state, action.modelId, (options) => ({
          ...options,
          showMethods: action.showMethods,
        })),
      );
    case "set-table-show-properties":
      return withTableOptions(state, action.modelId, (options) => ({
        ...options,
        showProperties: action.showProperties,
      }));
    case "set-interaction-setting":
      return {
        ...state,
        settings: {
          ...state.settings,
          [action.key]: clampInteractionSetting(action.key, action.value),
        },
      };
    case "set-viewport-pan":
      return {
        ...state,
        viewport: {
          ...state.viewport,
          panX: action.panX,
          panY: action.panY,
        },
      };
    case "set-viewport-zoom":
      return {
        ...state,
        viewport: {
          ...state.viewport,
          zoom: clampZoom(action.zoom),
        },
      };
  }
}

export function cloneDiagramInteractionState(
  state: DiagramInteractionState,
): DiagramInteractionState {
  return {
    layoutMode: state.layoutMode,
    settings: { ...state.settings },
    selectedMethodContext: cloneSelectedMethodContext(state.selectedMethodContext),
    selectedModelId: state.selectedModelId,
    tableOptions: state.tableOptions.map(cloneTableViewOptions),
    viewport: { ...state.viewport },
  };
}

export function getTableViewOptions(
  state: DiagramInteractionState,
  modelId: ModelId,
): TableViewOptions {
  return (
    state.tableOptions.find((options) => options.modelId === modelId) ?? {
      hidden: false,
      modelId,
      showMethodHighlights: true,
      showMethods: true,
      showProperties: true,
    }
  );
}

function clampZoom(value: number): number {
  return Math.max(0.005, Math.min(2.2, value));
}

function cloneSelectedMethodContext(
  value: SelectedMethodContext | undefined,
): SelectedMethodContext | undefined {
  if (!value) {
    return undefined;
  }

  return {
    methodName: value.methodName,
    modelId: value.modelId,
  };
}

function cloneTableViewOptions(options: TableViewOptions): TableViewOptions {
  return {
    hidden: options.hidden,
    manualPosition: options.manualPosition ? { ...options.manualPosition } : undefined,
    modelId: options.modelId,
    showMethodHighlights: options.showMethodHighlights,
    showMethods: options.showMethods,
    showProperties: options.showProperties,
  };
}

function nextSelectedMethodContext(
  current: SelectedMethodContext | undefined,
  modelId: ModelId,
  methodName: string,
): SelectedMethodContext | undefined {
  if (
    current?.modelId === modelId &&
    current.methodName === methodName
  ) {
    return undefined;
  }

  return {
    methodName,
    modelId,
  };
}

function sanitizeSelections(
  state: DiagramInteractionState,
): DiagramInteractionState {
  const selectedMethod = state.selectedMethodContext;
  if (!selectedMethod) {
    return state;
  }

  const options = getTableViewOptions(state, selectedMethod.modelId);
  if (options.showMethods && options.showMethodHighlights) {
    return state;
  }

  return {
    ...state,
    selectedMethodContext: undefined,
  };
}

function withTableOptions(
  state: DiagramInteractionState,
  modelId: ModelId,
  transform: (options: TableViewOptions) => TableViewOptions,
): DiagramInteractionState {
  let updated = false;
  const tableOptions = state.tableOptions.map((options) => {
    if (options.modelId !== modelId) {
      return cloneTableViewOptions(options);
    }

    updated = true;
    return transform(cloneTableViewOptions(options));
  });

  if (!updated) {
    tableOptions.push(transform(getTableViewOptions(state, modelId)));
  }

  return {
    ...state,
    tableOptions,
  };
}
