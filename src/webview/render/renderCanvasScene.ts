import {
  normalizeLayoutMode,
  OGDF_LAYOUT_TOOLBAR_DEFINITIONS,
} from "../../shared/graph/layoutContract";
import type { DiagramRenderModel } from "../state/createDiagramRenderModel";
import { serializeJsonForScriptTag } from "./escapeHtml";

export function renderCanvasScene(viewModel: DiagramRenderModel, appVersion: string): string {
  const renderModelJson = serializeJsonForScriptTag({
    appVersion,
    crossings: viewModel.crossings,
    edges: viewModel.edges,
    layoutMode: viewModel.layoutMode,
    modelCatalogMode: viewModel.modelCatalogMode,
    overlays: viewModel.overlays,
    tables: viewModel.tables,
  });

  return `
    <section class="erd-stage">
      <div class="erd-stage__toolbar">
        <div class="erd-toolbar-group">
          <button type="button" class="erd-tool" data-zoom-action="in">Zoom In</button>
          <button type="button" class="erd-tool" data-zoom-action="out">Zoom Out</button>
          <button type="button" class="erd-tool" data-zoom-action="fit">Auto Fit</button>
          <button type="button" class="erd-tool" data-zoom-action="center">Move To Center</button>
        </div>
        <div class="erd-toolbar-group">
          ${OGDF_LAYOUT_TOOLBAR_DEFINITIONS.map((layout) =>
            renderLayoutButton(layout.id, layout.shortLabel, layout.label, viewModel.layoutMode),
          ).join("")}
        </div>
        <div class="erd-toolbar-group">
          <button type="button" class="erd-tool" data-panel-refresh>Refresh</button>
          <button type="button" class="erd-tool" data-reset-view>Reset View</button>
        </div>
      </div>
      <div class="erd-canvas" data-erd-canvas>
        <canvas
          class="erd-scene erd-drawing-canvas"
          data-erd-drawing-canvas
          width="1"
          height="1"
          aria-label="Django ERD diagram"
        ></canvas>
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
): string {
  return `
    <button
      type="button"
      class="erd-tool erd-tool--layout${normalizeLayoutMode(layoutMode) === normalizeLayoutMode(activeMode) ? " is-active" : ""}"
      data-layout-mode="${layoutMode}"
      title="${label}"
      aria-label="${label}"
    >${shortLabel}</button>
  `;
}
