import { getBrowserCanvasDrawSource } from "./runtime/browserCanvasDrawSource";
import { getBrowserDomSource } from "./runtime/browserDomSource";
import { getBrowserEventSource } from "./runtime/browserEventSource";
import { getBrowserLayoutSource } from "./runtime/browserLayoutSource";
import { getBrowserLeafCardSource } from "./runtime/browserLeafCardSource";
import { getBrowserIndependentViewSource } from "./runtime/browserIndependentViewSource";
import { getBrowserRelationshipSource } from "./runtime/browserRelationshipSource";
import { getBrowserRelatedDiagramSource } from "./runtime/browserRelatedDiagramSource";
import { getBrowserRenderSource } from "./runtime/browserRenderSource";
import { getBrowserStateSource } from "./runtime/browserStateSource";
import { getBrowserTestSource } from "./runtime/browserTestSource";
import { createSelectedRelationshipTools } from "../state/selectedRelationshipEdges";

export function getBrowserControllerScript(nonce: string): string {
  return `
    <script nonce="${nonce}">
      (async () => {
        const vscode = typeof acquireVsCodeApi === "function" ? acquireVsCodeApi() : null;

        function emitBootstrapLog(level, event, details) {
          const rootElement = document.querySelector("[data-erd-root]");
          const version = rootElement instanceof HTMLElement && rootElement.dataset.erdVersion
            ? rootElement.dataset.erdVersion
            : "0.0.0";
          const timestamp = new Date().toISOString();
          const message = "[" + timestamp + "][v" + version + "] " + event;

          if (level === "error") {
            console.error(message, details || {});
          } else if (level === "warn") {
            console.warn(message, details || {});
          } else {
            console.info(message, details || {});
          }

          vscode?.postMessage({
            details,
            event,
            level,
            message,
            timestamp,
            type: "diagram.log",
            version,
          });
        }

        try {
          const root = document.querySelector("[data-erd-root]");
          const initialStateElement = document.getElementById("erd-initial-state");
          const renderModelElement = document.getElementById("erd-render-model");
          const canvas = document.querySelector("[data-erd-canvas]");
          const drawingCanvas = document.querySelector("[data-erd-drawing-canvas]");
          const gpuWarning = document.querySelector("[data-erd-gpu-warning]");
          const minimap = document.querySelector("[data-erd-minimap]");
          const minimapCanvas = document.querySelector("[data-erd-minimap-canvas]");
          const minimapViewport = document.querySelector("[data-erd-minimap-viewport]");
          const panelHost = document.querySelector("[data-model-panel-host]");
          const sidebarTabButtons = Array.from(document.querySelectorAll("[data-sidebar-tab]"));
          const sidebarSheets = Array.from(document.querySelectorAll("[data-sidebar-sheet]"));
          const hiddenModelList = document.querySelector("[data-hidden-model-list]");
          const layoutButtons = Array.from(document.querySelectorAll("[data-layout-mode]"));
          const resetViewButtons = Array.from(document.querySelectorAll("[data-reset-view]"));
          const setupControls = Array.from(document.querySelectorAll("[data-setup-control]"));
          const setupValueReadouts = Array.from(document.querySelectorAll("[data-setup-value]"));
          const zoomButtons = Array.from(document.querySelectorAll("[data-zoom-action]"));
          const layoutReadouts = Array.from(document.querySelectorAll("[data-layout-readout]"));
          const hiddenCountReadouts = Array.from(document.querySelectorAll("[data-hidden-count]"));
          const bootstrapStartedAt = performance.now();

          emitBootstrapLog("info", "webview.script.loaded", {
            hasCanvas: Boolean(canvas),
            hasDrawingCanvas: Boolean(drawingCanvas),
            hasInitialState: Boolean(initialStateElement),
            hasRenderModel: Boolean(renderModelElement),
            hasRoot: Boolean(root),
          });

          if (!root || !initialStateElement || !renderModelElement || !canvas || !drawingCanvas) {
            emitBootstrapLog("error", "webview.dom.missing", {
              hasCanvas: Boolean(canvas),
              hasDrawingCanvas: Boolean(drawingCanvas),
              hasInitialState: Boolean(initialStateElement),
              hasRenderModel: Boolean(renderModelElement),
              hasRoot: Boolean(root),
            });
            vscode?.postMessage({ type: "diagram.ready" });
            return;
          }

          function readEmbeddedJson(element) {
            const raw = element instanceof HTMLTemplateElement
              ? element.content.textContent || ""
              : element.textContent || "";

            return JSON.parse(raw || "{}");
          }

${getBrowserStateSource()}
${getBrowserLayoutSource()}
        const renderModel = readEmbeddedJson(renderModelElement);
        const readEdgeMeta = (edge) => ({
          carrierFamily: edge.carrierFamily || "",
          carrierRole: edge.carrierRole || "",
          crossingIds: Array.isArray(edge.crossingIds) ? edge.crossingIds.slice() : [],
          cssKind: edge.cssKind || "",
          edgeId: edge.edgeId || "",
          logicalEndpointModelIds: Array.isArray(edge.logicalEndpointModelIds)
            ? edge.logicalEndpointModelIds.slice()
            : [],
          markerEndId: edge.markerEndId || "",
          markerStartId: edge.markerStartId || "",
          memberEdgeIds: Array.isArray(edge.memberEdgeIds)
            ? edge.memberEdgeIds.slice()
            : [],
          physicalEndpointModelIds: Array.isArray(edge.physicalEndpointModelIds)
            ? edge.physicalEndpointModelIds.slice()
            : null,
          leafCardEndpointIds: edge.leafCardEndpointIds || null,
          points: edge.points || "",
          preserveRouteEndpoints: edge.preserveRouteEndpoints === true,
          provenance: edge.provenance || "",
          sourceModelId: edge.sourceModelId || "",
          targetModelId: edge.targetModelId || "",
        });
        const edgeMeta = (renderModel.leafCardOverview?.baseEdges || renderModel.edges || []).map(readEdgeMeta);
        const individualEdgeMeta = (renderModel.leafCardOverview?.individualEdges || renderModel.relationshipOverview?.individualEdges || []).map(readEdgeMeta);
        const selectedRelationshipTools = (${createSelectedRelationshipTools.toString()})();
        let selectedEdgeMeta = null;
        let selectedEdgeModelId = "";
        let juneOverviewExpanded = false;
        function getActiveEdgeMeta() {
          if (typeof independentViewActive !== "undefined" && independentViewActive) return independentEdgeMeta;
          if (juneOverviewExpanded && individualEdgeMeta.length > 0) return individualEdgeMeta;
          if (!state.selectedModelId || !individualEdgeMeta.length) return edgeMeta;
          if (!selectedEdgeMeta || selectedEdgeModelId !== state.selectedModelId) {
            selectedEdgeModelId = state.selectedModelId;
            selectedEdgeMeta = selectedRelationshipTools.focus(edgeMeta, individualEdgeMeta, selectedEdgeModelId);
          }
          return selectedEdgeMeta;
        }
        function setJuneOverviewExpanded(expanded) {
          if (!renderModel.relationshipOverview) return;
          juneOverviewExpanded = Boolean(expanded);
          const overview = renderModel.relationshipOverview;
          for (const button of document.querySelectorAll("[data-june-bundles-toggle]")) {
            button.classList.toggle("is-active", !juneOverviewExpanded);
            button.setAttribute("aria-pressed", String(!juneOverviewExpanded));
          }
          if (typeof synchronizeIndependentView === "function") synchronizeIndependentView();
          updateRelationshipReadouts();
          invalidateSceneGraph();
        }
        function updateRelationshipReadouts() {
          const overview = renderModel.relationshipOverview;
          const leaf = renderModel.leafCardOverview;
          if (!overview && !leaf) return;
          const leafActive = Boolean(leaf && typeof leafCardsVisible !== "undefined" && leafCardsVisible);
          const value = leafActive
            ? (juneOverviewExpanded ? leaf.expandedVisualCrossings : leaf.visualCrossings)
            : juneOverviewExpanded ? (renderModel.individualView && typeof independentViewActive !== "undefined" && independentViewActive
              ? renderModel.individualView.visualCrossings : overview?.individualVisualCrossings)
              : leaf ? leaf.baseVisualCrossings : overview.visualCrossings;
          for (const readout of document.querySelectorAll("[data-visual-crossing-readout]")) {
            readout.textContent = "Initial visual conflicts: " + value.toLocaleString("en-US")
              + (leafActive ? " · Leaf card connections" : juneOverviewExpanded ? " · Individual relationships" : overview ? " · June bundle overview" : " · Individual relationships");
          }
          for (const readout of document.querySelectorAll("[data-relationship-overview-readout]")) {
            const count = leafActive ? (juneOverviewExpanded ? leaf.expandedEdgeCount : renderModel.edges.length)
              : juneOverviewExpanded && individualEdgeMeta.length ? individualEdgeMeta.length : edgeMeta.length;
            readout.textContent = "Overview: " + count.toLocaleString("en-US") + " lines · "
              + individualEdgeMeta.length.toLocaleString("en-US") + " relationships"
              + (leafActive ? ". One line per leaf card and external model; internal relationships remain in model details."
                : juneOverviewExpanded || !overview ? ". Every relationship is expanded."
                : " · " + overview.groupCount + " bundles. Turn off June bundles to show every relationship.")
              + " Selecting a model shows its direct connections.";
          }
        }
        const tableMetaList = (renderModel.tables || []).map((table) => readTableMeta(table));
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
        let panelMetaById = new Map(
          Array.from(document.querySelectorAll("[data-model-panel]"))
            .map((panel) => [panel.dataset.modelId || "", readPanelMeta(panel)]),
        );
        const tableMetaById = new Map(
          tableMetaList.map((meta) => [meta.modelId, meta]),
        );
        const tableRenderById = new Map(
          (renderModel.tables || []).map((table) => [table.modelId, table]),
        );
        const inspectorModelById = new Map(
          (renderModel.inspectorModels || renderModel.tables || [])
            .map((model) => [model.modelId, model]),
        );
        const relationshipByEdgeId = new Map();
        const relationshipsByModelId = new Map();
        for (const model of inspectorModelById.values()) {
          const relationships = Array.isArray(model.relationships) ? model.relationships : [];
          relationshipsByModelId.set(model.modelId, relationships);
          for (const relationship of relationships) {
            if (relationship && relationship.edgeId && !relationshipByEdgeId.has(relationship.edgeId)) {
              relationshipByEdgeId.set(relationship.edgeId, relationship);
            }
          }
        }
        const bundleLeafToFakeId = {};
        const bundleLeavesByFakeIdRaw = renderModel.bundleLeavesByFakeId || {};
        for (const fakeId of Object.keys(bundleLeavesByFakeIdRaw)) {
          const leaves = bundleLeavesByFakeIdRaw[fakeId] || [];
          for (const leafId of leaves) {
            bundleLeafToFakeId[leafId] = fakeId;
          }
        }
        const initialStateValue = readEmbeddedJson(initialStateElement);
        const initialState = normalizeInitialState(
          initialStateValue,
          renderModel.tables?.[0]?.modelId || "",
          computeInitialViewport(initialStateValue),
        );
        let state = cloneState(initialState);
        let drag = null;
        let revealedRelationshipEdgeId = "";
        let renderedCrossings = [];
        let renderedEdges = [];
        let renderedOverlays = [];

${getBrowserRelationshipSource()}
${getBrowserRelatedDiagramSource()}
${getBrowserDomSource()}
${getBrowserLeafCardSource()}
${getBrowserIndependentViewSource()}
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
        } catch (error) {
          emitBootstrapLog("error", "webview.bootstrap.failed", {
            message: error instanceof Error ? error.message : String(error),
            name: error instanceof Error ? error.name : "Error",
          });
          vscode?.postMessage({ type: "diagram.ready" });
        }
      })();
    </script>
  `;
}
