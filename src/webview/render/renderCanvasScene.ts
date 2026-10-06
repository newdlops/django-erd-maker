import {
  normalizeLayoutMode,
  OGDF_LAYOUT_TOOLBAR_DEFINITIONS,
} from "../../shared/graph/layoutContract";
import type { DiagramRenderModel } from "../state/createDiagramRenderModel";
import { createSceneTransportTools } from "../state/sceneTransport";
import { escapeHtml, serializeSceneForScriptTag } from "./escapeHtml";
import { renderCircularDiagram } from "./renderCircularDiagram";

export function renderCanvasScene(viewModel: DiagramRenderModel, appVersion: string): string {
  const layoutFailureByMode = new Map(
    viewModel.layoutFailures.map((failure) => [failure.mode, failure.reason] as const),
  );
  const transport = createSceneTransportTools().prepare({
    appVersion,
    bundleLeafTiles: viewModel.bundleLeafTiles,
    bundleLeavesByFakeId: viewModel.bundleLeavesByFakeId,
    clusterOutlines: viewModel.clusterOutlines,
    crossings: viewModel.crossings,
    edges: viewModel.edges,
    inspectorModels: viewModel.inspector.models,
    individualView: viewModel.individualView,
    layoutMode: viewModel.layoutMode,
    leafBundles: viewModel.leafBundles,
    leafCards: viewModel.leafCards,
    leafCardOverview: viewModel.leafCardOverview,
    modelCatalogMode: viewModel.modelCatalogMode,
    overlays: viewModel.overlays,
    relationshipOverview: viewModel.relationshipOverview,
    tables: viewModel.tables,
  });
  const renderModelJson = serializeSceneForScriptTag(transport.scene, transport.replacer);

  return `
    <section class="erd-stage">
      <div class="erd-stage__toolbar">
        <div class="erd-toolbar-group erd-search">
          <input type="search" name="model-query" class="erd-search__input" data-erd-search placeholder="Find a model · Enter to go" autocomplete="off" autocorrect="off" spellcheck="false" aria-label="Search tables by name, app, or table name" aria-describedby="erd-search-help" />
          <span id="erd-search-help" class="erd-visually-hidden">Enter goes to the next result. Shift and Enter goes to the previous result. Command or Control and F opens search.</span>
          <span class="erd-search__count" data-erd-search-count aria-live="polite"></span>
        </div>
        <button type="button" class="erd-tool erd-related-open" data-related-diagram-open ${viewModel.inspector.selectedModelId ? '' : 'disabled'}>Related diagram</button>
        <button type="button" class="erd-tool" data-circular-open aria-pressed="false" aria-controls="erd-circular-diagram">Circular view</button>
        <div class="erd-toolbar-group erd-full-diagram-tools">
          <button type="button" class="erd-tool erd-tool--zoom" data-zoom-action="in" aria-label="Zoom in" title="Zoom in (+)">+</button>
          <button type="button" class="erd-tool erd-tool--zoom" data-zoom-action="out" aria-label="Zoom out" title="Zoom out (−)">−</button>
          <button type="button" class="erd-tool" data-zoom-action="fit">Fit diagram</button>
        </div>
        <details class="erd-layout-options">
          <summary>Layout &amp; view</summary>
          <div class="erd-layout-options__body">
        <div class="erd-toolbar-group" role="group" aria-label="Layout algorithm">
          ${OGDF_LAYOUT_TOOLBAR_DEFINITIONS.map((layout) =>
            renderLayoutButton(
              layout.id,
              layout.shortLabel,
              layout.label,
              viewModel.layoutMode,
              layoutFailureByMode.get(layout.id),
            ),
          ).join("")}
        </div>
        <div class="erd-toolbar-group">
          ${viewModel.relationshipOverview ? `<button type="button" class="erd-tool is-active" data-june-bundles-toggle aria-pressed="true" title="Group related straight lines. Leaf cards keep one connection per external model while enabled.">June bundles</button>` : ""}
          <button type="button" class="erd-tool${viewModel.leafCards.length ? " is-active" : ""}" data-leaf-cards-toggle aria-pressed="${viewModel.leafCards.length > 0}" ${viewModel.leafCards.length ? "" : "disabled"} title="${viewModel.leafCards.length ? "Connect each external model to a leaf card with one straight line. Drag its header to move the group; turn off to restore connections to individual models." : "This layout has no packed leaf groups. Use a layout containing leaf card memberships."}">Leaf cards (${viewModel.leafCards.length})</button>
          <select class="erd-leaf-card-select" data-leaf-card-select aria-label="Find a leaf card" title="Find a leaf card. Shift + arrow keys move the selected group." ${viewModel.leafCards.length ? "" : "hidden disabled"}>
            <option value="">Find leaf card</option>
            ${viewModel.leafCards.slice().sort((a, b) => b.memberModelIds.length - a.memberModelIds.length || a.label.localeCompare(b.label)).map((card) => `<option value="${escapeHtml(card.id)}">${escapeHtml(card.label)} · ${card.memberModelIds.length} leaves</option>`).join("")}
          </select>
          <span class="erd-visually-hidden" data-leaf-card-status role="status" aria-live="polite"></span>
          <button type="button" class="erd-tool" data-cluster-collapse-toggle title="Collapse clusters into super-nodes; aggregate edges between clusters">Collapse</button>
          <button type="button" class="erd-tool" data-cluster-graph-toggle title="Cluster-graph layout: per-cluster radial inner (root + leaf/internal/bridge rings) with super-graph cross-min. Connectors are pulled out of clusters and placed on inter-cluster edges.">ClusterGraph</button>
          <button type="button" class="erd-tool" data-bubble-toggle title="Bubble layout: each cluster forms a circular bubble with root at centre and ALL members (leaves, wings, internals, bridges) packed in concentric rings (full 360°). Bubbles may overlap based on inter-cluster connectivity.">Bubble</button>
          <button type="button" class="erd-tool" data-optimized-toggle title="Compute a fresh model-based layout from current source, then refine and verify the original straight relationships and table spacing.">ML Optimized</button>
          <button type="button" class="erd-tool" data-panel-refresh>Refresh</button>
          <button type="button" class="erd-tool" data-zoom-action="center">Move To Center</button>
          <button type="button" class="erd-tool" data-reset-view>Reset View</button>
        </div>
          </div>
        </details>
      </div>
      <section class="erd-connection-context" data-connection-context aria-label="Selected connection" hidden></section>
      <section class="erd-related-diagram" data-related-diagram aria-label="Related models diagram" hidden></section>
      ${renderCircularDiagram()}
      <div class="erd-canvas" data-erd-canvas>
        <canvas
          class="erd-scene erd-drawing-canvas"
          data-erd-drawing-canvas
          width="1"
          height="1"
          aria-label="Django ERD diagram"
        ></canvas>
        <div class="erd-connection-anchors" data-connection-anchors aria-label="Connection endpoints" hidden></div>
        <section class="erd-gpu-warning" data-erd-gpu-warning hidden>
          <p class="erd-gpu-warning__eyebrow">GPU Renderer Required</p>
          <h2 class="erd-gpu-warning__title">WebGL2 or WebGPU support is required.</h2>
          <p class="erd-gpu-warning__body" data-erd-gpu-warning-message>
            This ERD view now requires a GPU-capable webview renderer.
          </p>
        </section>
        <div
          class="erd-minimap"
          data-erd-minimap
          aria-label="Diagram minimap"
          role="application"
        >
          <canvas
            class="erd-minimap__canvas"
            data-erd-minimap-canvas
            width="1"
            height="1"
          ></canvas>
        <div class="erd-minimap__viewport" data-erd-minimap-viewport></div>
        </div>
        <template id="erd-render-model">${renderModelJson}</template>
      </div>
    </section>
  `;
}

function renderLayoutButton(
  layoutMode: DiagramRenderModel["layoutMode"],
  shortLabel: string,
  label: string,
  activeMode: DiagramRenderModel["layoutMode"],
  failureReason?: string,
): string {
  const title = failureReason
    ? `${label} unavailable: ${failureReason}`
    : label;
  return `
    <button
      type="button"
      class="erd-tool erd-tool--layout${normalizeLayoutMode(layoutMode) === normalizeLayoutMode(activeMode) ? " is-active" : ""}"
      data-layout-mode="${layoutMode}"
      title="${escapeHtml(title)}"
      aria-label="${escapeHtml(title)}"
      ${failureReason ? "disabled" : ""}
    >${escapeHtml(shortLabel)}</button>
  `;
}
