import { getBrowserCanvasDrawSource } from "./runtime/browserCanvasDrawSource";
import { getBrowserDomSource } from "./runtime/browserDomSource";
import { getBrowserEventSource } from "./runtime/browserEventSource";
import { getBrowserLayoutSource } from "./runtime/browserLayoutSource";
import { getBrowserRenderSource } from "./runtime/browserRenderSource";
import { getBrowserStateSource } from "./runtime/browserStateSource";
import { getBrowserTestSource } from "./runtime/browserTestSource";

export function getBrowserControllerScript(nonce: string): string {
  return `
    <script nonce="${nonce}">
      (() => {
        const root = document.querySelector("[data-erd-root]");
        const initialStateElement = document.getElementById("erd-initial-state");
        const renderModelElement = document.getElementById("erd-render-model");
        const viewport = document.querySelector("[data-erd-viewport]");
        const canvas = document.querySelector("[data-erd-canvas]");
        const drawingCanvas = document.querySelector("[data-erd-drawing-canvas]");
        const minimap = document.querySelector("[data-erd-minimap]");
        const minimapCanvas = document.querySelector("[data-erd-minimap-canvas]");
        const minimapViewport = document.querySelector("[data-erd-minimap-viewport]");
        const crossingsLayer = document.querySelector('[data-layer="crossings"]');
        const methodButtons = Array.from(document.querySelectorAll("[data-method-button]"));
        const modelPanels = Array.from(document.querySelectorAll("[data-model-panel]"));
        const overlays = Array.from(document.querySelectorAll(".erd-method-overlay"));
        const tables = Array.from(document.querySelectorAll("[data-model-id].erd-table"));
        const edges = Array.from(document.querySelectorAll("[data-edge-id]"));
        const layoutButtons = Array.from(document.querySelectorAll("[data-layout-mode]"));
        const resetViewButtons = Array.from(document.querySelectorAll("[data-reset-view]"));
        const hiddenModelItems = Array.from(document.querySelectorAll("[data-hidden-model-item]"));
        const showHiddenButtons = Array.from(document.querySelectorAll("[data-show-hidden-model]"));
        const tableToggleButtons = Array.from(document.querySelectorAll("[data-table-toggle]"));
        const panelRefreshButtons = Array.from(document.querySelectorAll("[data-panel-refresh]"));
        const setupControls = Array.from(document.querySelectorAll("[data-setup-control]"));
        const setupValueReadouts = Array.from(document.querySelectorAll("[data-setup-value]"));
        const zoomButtons = Array.from(document.querySelectorAll("[data-zoom-action]"));
        const layoutReadouts = Array.from(document.querySelectorAll("[data-layout-readout]"));
        const hiddenCountReadouts = Array.from(document.querySelectorAll("[data-hidden-count]"));
        const vscode = typeof acquireVsCodeApi === "function" ? acquireVsCodeApi() : null;

        if (!root || !initialStateElement || !renderModelElement || !viewport || !canvas || !drawingCanvas) {
          return;
        }

        const drawingContext = drawingCanvas.getContext("2d");
        if (!drawingContext) {
          return;
        }

${getBrowserStateSource()}
        const renderModel = JSON.parse(renderModelElement.textContent || "{}");
        const edgeMeta = edges.map((edge) => ({
          edgeId: edge.dataset.edgeId || "",
          element: edge,
          cssKind: edge.dataset.cssKind || "",
          provenance: edge.dataset.provenance || "",
          sourceModelId: edge.dataset.sourceModel || "",
          targetModelId: edge.dataset.targetModel || "",
        }));
        const tableMetaList = tables.map((table) => readTableMeta(table));
        const hiddenItemsById = new Map(
          hiddenModelItems.map((item) => [item.dataset.modelId || "", item]),
        );
        let layoutVariantCache = new Map();
        let layoutVariantCacheKey = "";
        const overlayMeta = overlays.map((overlay) => ({
          element: overlay,
          methodName: overlay.dataset.methodName || "",
          sourceModelId: overlay.dataset.sourceModel || "",
          targetModelId: overlay.dataset.targetModel || "",
        }));
        const panelMetaById = new Map(
          modelPanels.map((panel) => [panel.dataset.modelId || "", readPanelMeta(panel)]),
        );
        const tableMetaById = new Map(
          tableMetaList.map((meta) => [meta.modelId, meta]),
        );
        const tableRenderById = new Map(
          (renderModel.tables || []).map((table) => [table.modelId, table]),
        );
        const initialStateValue = JSON.parse(initialStateElement.textContent || "{}");
        let state = null;
        let appliedLayoutSettings = pickLayoutRoutingSettings(
          normalizeInteractionSettings(initialStateValue.settings),
        );
        const initialState = normalizeInitialState(
          initialStateValue,
          renderModel.tables?.[0]?.modelId || "",
          computeInitialViewport(initialStateValue),
        );
        state = cloneState(initialState);
        appliedLayoutSettings = pickLayoutRoutingSettings(initialState.settings);
        let drag = null;
        let renderedCrossings = [];
        let renderedEdges = [];
        let renderedOverlays = [];

${getBrowserDomSource()}
${getBrowserLayoutSource()}
${getBrowserCanvasDrawSource()}
${getBrowserRenderSource()}
${getBrowserEventSource()}
${getBrowserTestSource()}

        applyState();
        vscode?.postMessage({ type: "diagram.ready" });
      })();
    </script>
  `;
}
