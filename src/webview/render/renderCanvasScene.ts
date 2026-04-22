import type { DiagramRenderModel } from "../state/createDiagramRenderModel";
import { escapeHtml, serializeJsonForScriptTag } from "./escapeHtml";

export function renderCanvasScene(viewModel: DiagramRenderModel): string {
  const renderModelJson = serializeJsonForScriptTag(viewModel);

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
          ${renderLayoutButton("hierarchical", viewModel.layoutMode)}
          ${renderLayoutButton("graph", viewModel.layoutMode)}
          ${renderLayoutButton("radial", viewModel.layoutMode)}
          ${renderLayoutButton("neural", viewModel.layoutMode)}
          ${renderLayoutButton("flow", viewModel.layoutMode)}
          ${renderLayoutButton("circular", viewModel.layoutMode)}
          ${renderLayoutButton("clustered", viewModel.layoutMode)}
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
          width="${viewModel.canvas.width}"
          height="${viewModel.canvas.height}"
          aria-label="Django ERD diagram"
        ></canvas>
        <script id="erd-render-model" type="application/json">${renderModelJson}</script>
        <div class="erd-scene-metadata" data-erd-scene-metadata hidden>
          <div class="erd-viewport" data-erd-viewport></div>
          <div class="erd-edges" data-layer="structural">
            ${viewModel.edges.map(renderEdgeMetadata).join("")}
          </div>
          <div class="erd-method-overlays" data-layer="method">
            ${viewModel.overlays.map(renderMethodOverlayMetadata).join("")}
          </div>
          <div class="erd-crossings" data-layer="crossings">
            ${viewModel.crossings.map(renderCrossingMetadata).join("")}
          </div>
          <div class="erd-tables" data-layer="tables">
            ${viewModel.tables.map(renderTableMetadata).join("")}
          </div>
        </div>
      </div>
    </section>
  `;
}

function renderCrossingMetadata(crossing: DiagramRenderModel["crossings"][number]): string {
  return `
    <div
      class="erd-crossing erd-crossing--${escapeHtml(crossing.markerStyle)}"
      data-crossing-id="${escapeHtml(crossing.id)}"
      data-x="${crossing.position.x}"
      data-y="${crossing.position.y}"
    ></div>
  `;
}

function renderEdgeMetadata(edge: DiagramRenderModel["edges"][number]): string {
  return `
    <div
      class="erd-edge erd-edge--${escapeHtml(edge.cssKind)} erd-edge--${escapeHtml(edge.provenance)}"
      data-css-kind="${escapeHtml(edge.cssKind)}"
      data-edge-id="${escapeHtml(edge.edgeId)}"
      data-provenance="${escapeHtml(edge.provenance)}"
      data-source-model="${escapeHtml(edge.sourceModelId)}"
      data-target-model="${escapeHtml(edge.targetModelId)}"
      data-points="${escapeHtml(edge.points)}"
    ></div>
  `;
}

function renderLayoutButton(
  layoutMode: DiagramRenderModel["layoutMode"],
  activeMode: DiagramRenderModel["layoutMode"],
): string {
  return `
    <button
      type="button"
      class="erd-tool${layoutMode === activeMode ? " is-active" : ""}"
      data-layout-mode="${layoutMode}"
    >${layoutMode}</button>
  `;
}

function renderMethodOverlayMetadata(
  overlay: DiagramRenderModel["overlays"][number],
): string {
  return `
    <div
      class="erd-method-overlay"
      data-confidence="${escapeHtml(overlay.confidence)}"
      data-method-name="${escapeHtml(overlay.methodName)}"
      data-source-model="${escapeHtml(overlay.sourceModelId)}"
      data-target-model="${escapeHtml(overlay.targetModelId)}"
      id="${escapeHtml(overlay.id)}"
    ></div>
  `;
}

function renderTableMetadata(table: DiagramRenderModel["tables"][number]): string {
  const detailedSections =
    table.fieldRows.length > 0 || table.properties.length > 0 || table.methods.length > 0
      ? `
        <div data-table-section="fields">
          ${table.fieldRows.map((row) => `<span>${escapeHtml(row.text)}</span>`).join("")}
        </div>
        <div data-table-divider="properties" ${table.showProperties && table.properties.length > 0 ? "" : "hidden"}></div>
        <div data-table-section="properties" ${table.showProperties ? "" : "hidden"}>
          ${table.properties.map((property) => `<span>${escapeHtml(property)}</span>`).join("")}
        </div>
        <div data-table-divider="methods" ${table.showMethods && table.methods.length > 0 ? "" : "hidden"}></div>
        <div data-table-section="methods" ${table.showMethods ? "" : "hidden"}>
          ${table.methods.map((method) => `<span>${escapeHtml(method.name)}</span>`).join("")}
        </div>
      `
      : "";

  return `
    <div
      class="erd-table${table.selected ? " is-selected" : ""}"
      data-app-label="${escapeHtml(table.appLabel)}"
      data-base-x="${table.position.x}"
      data-base-y="${table.position.y}"
      data-explicit-db-table="${String(table.hasExplicitDatabaseTableName)}"
      data-hidden="${String(table.hidden)}"
      data-method-highlights="${String(table.showMethodHighlights)}"
      data-model-id="${escapeHtml(table.modelId)}"
      data-model-name="${escapeHtml(table.modelName)}"
      data-show-methods="${String(table.showMethods)}"
      data-show-properties="${String(table.showProperties)}"
      data-width="${table.size.width}"
      data-height="${table.size.height}"
      data-table-name="${escapeHtml(table.databaseTableName)}"
      transform="translate(${table.position.x} ${table.position.y})"
      tabindex="0"
      ${table.hidden ? "hidden" : ""}
    >
      ${detailedSections}
    </div>
  `;
}
