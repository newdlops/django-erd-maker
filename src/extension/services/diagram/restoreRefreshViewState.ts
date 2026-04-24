import type { ModelId } from "../../../shared/domain/modelIdentity";
import { normalizeLayoutMode, type LayoutSnapshot } from "../../../shared/graph/layoutContract";
import type {
  DiagramViewportSnapshot,
  RefreshViewStateSnapshot,
  TableViewOptions,
} from "../../../shared/protocol/webviewContract";
import type { LiveDiagramResult } from "./loadLiveDiagram";

interface Bounds {
  maxX: number;
  maxY: number;
  minX: number;
  minY: number;
}

export function restoreRefreshViewState(
  nextResult: LiveDiagramResult,
  previousResult: LiveDiagramResult | undefined,
  viewState: RefreshViewStateSnapshot,
  refreshKind: "full" | "layout",
): LiveDiagramResult {
  const availableModelIds = new Set(
    nextResult.payload.layout.nodes.map((node) => node.modelId),
  );
  nextResult.payload.view.layoutMode = normalizeLayoutMode(nextResult.payload.layout.mode);
  nextResult.payload.view.selectedModelId = viewState.selectedModelId &&
  availableModelIds.has(viewState.selectedModelId)
    ? viewState.selectedModelId
    : undefined;
  nextResult.payload.view.selectedMethodContext = isValidSelectedMethodContext(
    nextResult,
    viewState,
  )
    ? {
        methodName: viewState.selectedMethodContext!.methodName,
        modelId: viewState.selectedMethodContext!.modelId,
      }
    : undefined;
  nextResult.payload.view.tableOptions = viewState.tableOptions
    .filter((options) => availableModelIds.has(options.modelId))
    .map(cloneTableViewOptions);
  nextResult.payload.view.viewport = restoreViewport(
    previousResult?.payload.layout,
    nextResult.payload.layout,
    viewState,
    nextResult.payload.view.selectedModelId,
    refreshKind,
  );

  return nextResult;
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

function computeBounds(
  layout: LayoutSnapshot,
  tableOptions: TableViewOptions[],
): Bounds | undefined {
  const tableOptionsById = new Map(
    tableOptions.map((options) => [options.modelId, options] as const),
  );
  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  let visibleCount = 0;

  for (const node of layout.nodes) {
    const tableOptions = tableOptionsById.get(node.modelId);
    if (tableOptions?.hidden) {
      continue;
    }

    const position = tableOptions?.manualPosition ?? node.position;
    minX = Math.min(minX, position.x);
    minY = Math.min(minY, position.y);
    maxX = Math.max(maxX, position.x + node.size.width);
    maxY = Math.max(maxY, position.y + node.size.height);
    visibleCount += 1;
  }

  if (visibleCount === 0) {
    return undefined;
  }

  return { maxX, maxY, minX, minY };
}

function computeNodeCenter(
  layout: LayoutSnapshot,
  tableOptionsList: TableViewOptions[],
  modelId: ModelId,
): { x: number; y: number } | undefined {
  const node = layout.nodes.find((entry) => entry.modelId === modelId);
  if (!node) {
    return undefined;
  }

  const tableOptionsById = new Map(
    tableOptionsList.map((options) => [options.modelId, options] as const),
  );
  const tableOptions = tableOptionsById.get(modelId);
  if (tableOptions?.hidden) {
    return undefined;
  }

  const position = tableOptions?.manualPosition ?? node.position;
  return {
    x: position.x + node.size.width / 2,
    y: position.y + node.size.height / 2,
  };
}

function isValidSelectedMethodContext(
  result: LiveDiagramResult,
  viewState: RefreshViewStateSnapshot,
): boolean {
  const selectedMethodContext = viewState.selectedMethodContext;
  if (!selectedMethodContext) {
    return false;
  }

  return result.payload.analyzer.models.some(
    (model) =>
      model.identity.id === selectedMethodContext.modelId &&
      model.methods.some((method) => method.name === selectedMethodContext.methodName),
  );
}

function restoreViewport(
  previousLayout: LayoutSnapshot | undefined,
  nextLayout: LayoutSnapshot,
  viewState: RefreshViewStateSnapshot,
  selectedModelId: ModelId | undefined,
  refreshKind: "full" | "layout",
): DiagramViewportSnapshot {
  const width = Math.max(1, viewState.viewportRect.width);
  const height = Math.max(1, viewState.viewportRect.height);
  const zoom = clampZoom(viewState.viewport.zoom);

  if (refreshKind === "layout" && selectedModelId) {
    const selectedCenter = computeNodeCenter(
      nextLayout,
      viewState.tableOptions,
      selectedModelId,
    );
    if (selectedCenter) {
      return createViewportForCenter(selectedCenter, zoom, width, height);
    }
  }

  const previousBounds = previousLayout
    ? computeBounds(previousLayout, viewState.tableOptions)
    : undefined;
  const nextBounds = computeBounds(nextLayout, viewState.tableOptions);

  if (!previousBounds || !nextBounds) {
    return {
      panX: viewState.viewport.panX,
      panY: viewState.viewport.panY,
      zoom,
    };
  }

  const currentCenter = clampPointToBounds(
    {
      x: (width / 2 - viewState.viewport.panX) / zoom,
      y: (height / 2 - viewState.viewport.panY) / zoom,
    },
    previousBounds,
  );
  const mappedCenter = {
    x: mapRelativeCoordinate(
      currentCenter.x,
      previousBounds.minX,
      previousBounds.maxX,
      nextBounds.minX,
      nextBounds.maxX,
    ),
    y: mapRelativeCoordinate(
      currentCenter.y,
      previousBounds.minY,
      previousBounds.maxY,
      nextBounds.minY,
      nextBounds.maxY,
    ),
  };

  return createViewportForCenter(mappedCenter, zoom, width, height);
}

function clampPointToBounds(
  point: { x: number; y: number },
  bounds: Bounds,
): { x: number; y: number } {
  return {
    x: Math.max(bounds.minX, Math.min(bounds.maxX, point.x)),
    y: Math.max(bounds.minY, Math.min(bounds.maxY, point.y)),
  };
}

function createViewportForCenter(
  center: { x: number; y: number },
  zoom: number,
  width: number,
  height: number,
): DiagramViewportSnapshot {
  return {
    panX: Math.round((width / 2 - center.x * zoom) * 100) / 100,
    panY: Math.round((height / 2 - center.y * zoom) * 100) / 100,
    zoom,
  };
}

function clampZoom(value: number): number {
  return Math.max(0.005, Math.min(2.2, value));
}

function mapRelativeCoordinate(
  value: number,
  previousMin: number,
  previousMax: number,
  nextMin: number,
  nextMax: number,
): number {
  const previousSpan = Math.max(1, previousMax - previousMin);
  const nextSpan = Math.max(1, nextMax - nextMin);
  const ratio = (value - previousMin) / previousSpan;
  return nextMin + ratio * nextSpan;
}
