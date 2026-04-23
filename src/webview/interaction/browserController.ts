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
      (async () => {
        const root = document.querySelector("[data-erd-root]");
        const initialStateElement = document.getElementById("erd-initial-state");
        const renderModelElement = document.getElementById("erd-render-model");
        const canvas = document.querySelector("[data-erd-canvas]");
        const drawingCanvas = document.querySelector("[data-erd-drawing-canvas]");
        const gpuWarning = document.querySelector("[data-erd-gpu-warning]");
        const minimap = document.querySelector("[data-erd-minimap]");
        const minimapCanvas = document.querySelector("[data-erd-minimap-canvas]");
        const minimapViewport = document.querySelector("[data-erd-minimap-viewport]");
        const methodButtons = Array.from(document.querySelectorAll("[data-method-button]"));
        const modelPanels = Array.from(document.querySelectorAll("[data-model-panel]"));
        const layoutButtons = Array.from(document.querySelectorAll("[data-layout-mode]"));
        const resetViewButtons = Array.from(document.querySelectorAll("[data-reset-view]"));
        const hiddenModelItems = Array.from(document.querySelectorAll("[data-hidden-model-item]"));
        const showHiddenButtons = Array.from(document.querySelectorAll("[data-show-hidden-model]"));
        const tableToggleButtons = Array.from(document.querySelectorAll("[data-table-toggle]"));
        const setupControls = Array.from(document.querySelectorAll("[data-setup-control]"));
        const setupValueReadouts = Array.from(document.querySelectorAll("[data-setup-value]"));
        const zoomButtons = Array.from(document.querySelectorAll("[data-zoom-action]"));
        const layoutReadouts = Array.from(document.querySelectorAll("[data-layout-readout]"));
        const hiddenCountReadouts = Array.from(document.querySelectorAll("[data-hidden-count]"));
        const vscode = typeof acquireVsCodeApi === "function" ? acquireVsCodeApi() : null;
        const bootstrapStartedAt = performance.now();

        if (!root || !initialStateElement || !renderModelElement || !canvas || !drawingCanvas) {
          return;
        }

${getBrowserStateSource()}
        const renderModel = JSON.parse(renderModelElement.textContent || "{}");
        const edgeMeta = (renderModel.edges || []).map((edge) => ({
          crossingIds: Array.isArray(edge.crossingIds) ? edge.crossingIds.slice() : [],
          cssKind: edge.cssKind || "",
          edgeId: edge.edgeId || "",
          markerEndId: edge.markerEndId || "",
          markerStartId: edge.markerStartId || "",
          points: edge.points || "",
          provenance: edge.provenance || "",
          sourceModelId: edge.sourceModelId || "",
          targetModelId: edge.targetModelId || "",
        }));
        const tableMetaList = (renderModel.tables || []).map((table) => readTableMeta(table));
        const hiddenItemsById = new Map(
          hiddenModelItems.map((item) => [item.dataset.modelId || "", item]),
        );
        const layoutVariants = createLayoutVariants(tableMetaList);
        const overlayMeta = (renderModel.overlays || []).map((overlay) => ({
          confidence: overlay.confidence || "medium",
          id: overlay.id || "",
          methodName: overlay.methodName || "",
          sourceModelId: overlay.sourceModelId || "",
          targetModelId: overlay.targetModelId || "",
          x1: Number(overlay.x1 || 0),
          x2: Number(overlay.x2 || 0),
          y1: Number(overlay.y1 || 0),
          y2: Number(overlay.y2 || 0),
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
        const initialState = normalizeInitialState(
          initialStateValue,
          renderModel.tables?.[0]?.modelId || "",
          computeInitialViewport(initialStateValue),
        );
        let state = cloneState(initialState);
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

        logErd("info", "webview.bootstrap", {
          edges: edgeMeta.length,
          layoutMode: state.layoutMode,
          renderer: "detecting",
          tables: tableMetaList.length,
        });
        const gpuSupport = detectGpuSupport();
        if (!gpuSupport.supported) {
          logErd("warn", "renderer.selected", {
            reason: "gpu-unavailable",
            renderer: "DOM",
            status: "blocked",
          });
          showGpuUnsupportedWarning(gpuSupport.reason);
          vscode?.postMessage({ type: "diagram.ready" });
          return;
        }

        const rendererStartedAt = performance.now();
        gpuRenderer = await createGpuRenderer(gpuSupport);
        if (!gpuRenderer) {
          logErdDuration("error", "renderer.init.failed", rendererStartedAt, {
            webgl2: gpuSupport.hasWebgl2,
            webgpu: gpuSupport.hasWebgpu,
          });
          showGpuUnsupportedWarning(
            "The GPU renderer could not be initialized in this webview.",
          );
          vscode?.postMessage({ type: "diagram.ready" });
          return;
        }

        logErdDuration("info", "renderer.selected", rendererStartedAt, {
          renderer: gpuRenderer.backend,
          webgl2: gpuSupport.hasWebgl2,
          webgpu: gpuSupport.hasWebgpu,
        });
        applyState();
        logErdDuration("info", "webview.ready", bootstrapStartedAt, {
          renderer: gpuRenderer.backend,
        });
        vscode?.postMessage({ type: "diagram.ready" });
      })();
    </script>
  `;
}
