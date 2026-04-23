import { OGDF_LAYOUT_MODES } from "../../shared/graph/layoutContract";
import type { DiagramRenderModel } from "../state/createDiagramRenderModel";
import { escapeHtml } from "./escapeHtml";

export function renderSvgScene(viewModel: DiagramRenderModel): string {
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
          ${OGDF_LAYOUT_MODES.map((layoutMode) =>
            renderLayoutButton(layoutMode, viewModel.layoutMode),
          ).join("")}
        </div>
        <div class="erd-toolbar-group">
          <button type="button" class="erd-tool" data-panel-refresh>Refresh</button>
          <button type="button" class="erd-tool" data-reset-view>Reset View</button>
        </div>
      </div>
      <div class="erd-canvas" data-erd-canvas>
        <svg
          class="erd-scene"
          viewBox="0 0 ${viewModel.canvas.width} ${viewModel.canvas.height}"
          xmlns="http://www.w3.org/2000/svg"
          aria-label="Django ERD diagram"
        >
          <defs>
            ${renderMarkerDefinitions()}
          </defs>
          <rect
            class="erd-scene__backdrop"
            x="0"
            y="0"
            width="${viewModel.canvas.width}"
            height="${viewModel.canvas.height}"
            rx="36"
          />
          <g class="erd-viewport" data-erd-viewport>
            <g class="erd-edges" data-layer="structural">
              ${viewModel.edges.map(renderEdge).join("")}
            </g>
            <g class="erd-method-overlays" data-layer="method">
              ${viewModel.overlays.map(renderMethodOverlay).join("")}
            </g>
            <g class="erd-crossings" data-layer="crossings">
              ${viewModel.crossings.map(renderCrossing).join("")}
            </g>
            <g class="erd-tables" data-layer="tables">
              ${viewModel.tables.map(renderTable).join("")}
            </g>
          </g>
        </svg>
      </div>
    </section>
  `;
}

function renderCrossing(crossing: DiagramRenderModel["crossings"][number]): string {
  return `
    <g class="erd-crossing erd-crossing--${escapeHtml(crossing.markerStyle)}" data-crossing-id="${escapeHtml(crossing.id)}">
      <circle cx="${crossing.position.x}" cy="${crossing.position.y}" r="6.5" />
      <path d="M ${crossing.position.x - 7} ${crossing.position.y} Q ${crossing.position.x} ${crossing.position.y - 7} ${crossing.position.x + 7} ${crossing.position.y}" />
    </g>
  `;
}

function renderEdge(edge: DiagramRenderModel["edges"][number]): string {
  return `
    <polyline
      class="erd-edge erd-edge--${escapeHtml(edge.cssKind)} erd-edge--${escapeHtml(edge.provenance)}"
      data-edge-id="${escapeHtml(edge.edgeId)}"
      data-source-model="${escapeHtml(edge.sourceModelId)}"
      data-target-model="${escapeHtml(edge.targetModelId)}"
      points="${escapeHtml(edge.points)}"
      marker-start="url(#${escapeHtml(edge.markerStartId)})"
      marker-end="url(#${escapeHtml(edge.markerEndId)})"
    />
  `;
}

function renderFieldRows(
  rows: DiagramRenderModel["tables"][number]["fieldRows"],
  startY: number,
): string {
  let cursorY = startY;

  return rows
    .map((row) => {
      const markup = `
        <text
          class="erd-table__row erd-table__row--${escapeHtml(row.tone)}"
          x="18"
          y="${cursorY}"
        >${escapeHtml(row.text)}</text>
      `;
      cursorY += row.tone === "enum-option" ? 18 : 24;
      return markup;
    })
    .join("");
}

function renderLayoutButton(
  layoutMode: DiagramRenderModel["layoutMode"],
  activeMode: DiagramRenderModel["layoutMode"],
): string {
  const label = layoutMode.charAt(0).toUpperCase() + layoutMode.slice(1);
  return `
    <button
      type="button"
      class="erd-tool${layoutMode === activeMode ? " is-active" : ""}"
      data-layout-mode="${layoutMode}"
    >${label}</button>
  `;
}

function renderMarkerDefinitions(): string {
  return `
    <marker id="erd-marker-one" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto-start-reverse" markerUnits="strokeWidth">
      <path d="M 10 0 L 10 12" class="erd-marker erd-marker--one" />
    </marker>
    <marker id="erd-marker-many" markerWidth="14" markerHeight="14" refX="10" refY="7" orient="auto-start-reverse" markerUnits="strokeWidth">
      <path d="M 1 1 L 11 7 L 1 13 M 5 1 L 11 7 L 5 13" class="erd-marker erd-marker--many" />
    </marker>
  `;
}

function renderMethodOverlay(overlay: DiagramRenderModel["overlays"][number]): string {
  return `
    <line
      class="erd-method-overlay"
      data-method-name="${escapeHtml(overlay.methodName)}"
      data-source-model="${escapeHtml(overlay.sourceModelId)}"
      data-target-model="${escapeHtml(overlay.targetModelId)}"
      data-confidence="${escapeHtml(overlay.confidence)}"
      id="${escapeHtml(overlay.id)}"
      x1="${overlay.x1}"
      x2="${overlay.x2}"
      y1="${overlay.y1}"
      y2="${overlay.y2}"
    />
  `;
}

function renderPropertyRows(
  properties: DiagramRenderModel["tables"][number]["properties"],
  startY: number,
): string {
  let cursorY = startY;

  return properties
    .map((property) => {
      const markup = `<text class="erd-table__row erd-table__row--property" x="18" y="${cursorY}">@ ${escapeHtml(property)}</text>`;
      cursorY += 22;
      return markup;
    })
    .join("");
}

function renderMethodRows(
  methods: DiagramRenderModel["tables"][number]["methods"],
  startY: number,
): string {
  let cursorY = startY;

  return methods
    .map((method) => {
      const badge = method.relatedModels.length > 0 ? ` [${method.relatedModels.length}]` : "";
      const markup = `<text class="erd-table__row erd-table__row--method" x="18" y="${cursorY}">fn ${escapeHtml(method.name + badge)}</text>`;
      cursorY += 22;
      return markup;
    })
    .join("");
}

function renderSectionDivider(
  width: number,
  y: number,
  section: "methods" | "properties",
  visible: boolean,
): string {
  return `
    <line
      class="erd-table__divider erd-table__divider--section"
      data-table-divider="${section}"
      x1="18"
      y1="${y}"
      x2="${width - 18}"
      y2="${y}"
      ${visible ? "" : "hidden"}
    />
  `;
}

function renderTable(table: DiagramRenderModel["tables"][number]): string {
  const fieldStartY = 72;
  const fieldHeight = table.fieldRows.reduce(
    (sum, row) => sum + (row.tone === "enum-option" ? 18 : 24),
    0,
  );
  const propertyStartY = fieldStartY + fieldHeight + 14;
  const methodStartY = propertyStartY + table.properties.length * 22 + 18;

  return `
    <g
      class="erd-table${table.selected ? " is-selected" : ""}"
      data-app-label="${escapeHtml(table.appLabel)}"
      data-base-x="${table.position.x}"
      data-base-y="${table.position.y}"
      data-hidden="${String(table.hidden)}"
      data-method-highlights="${String(table.showMethodHighlights)}"
      data-model-id="${escapeHtml(table.modelId)}"
      data-model-name="${escapeHtml(table.modelName)}"
      data-show-methods="${String(table.showMethods)}"
      data-show-properties="${String(table.showProperties)}"
      data-width="${table.size.width}"
      data-height="${table.size.height}"
      transform="translate(${table.position.x} ${table.position.y})"
      tabindex="0"
      ${table.hidden ? "hidden" : ""}
    >
      <rect class="erd-table__frame" width="${table.size.width}" height="${table.size.height}" rx="22" />
      <rect class="erd-table__header" width="${table.size.width}" height="44" rx="22" />
      <text class="erd-table__app" x="18" y="18">${escapeHtml(table.appLabel)}</text>
      <text class="erd-table__title" x="18" y="34">${escapeHtml(table.modelName)}</text>
      <line class="erd-table__divider" x1="0" y1="46" x2="${table.size.width}" y2="46" />
      <g data-table-section="fields">
        ${renderFieldRows(table.fieldRows, fieldStartY)}
      </g>
      ${renderSectionDivider(table.size.width, propertyStartY - 8, "properties", table.showProperties && table.properties.length > 0)}
      <g data-table-section="properties" ${table.showProperties ? "" : "hidden"}>
        ${renderPropertyRows(table.properties, propertyStartY)}
      </g>
      ${renderSectionDivider(table.size.width, methodStartY - 8, "methods", table.showMethods && table.methods.length > 0)}
      <g data-table-section="methods" ${table.showMethods ? "" : "hidden"}>
        ${renderMethodRows(table.methods, methodStartY)}
      </g>
    </g>
  `;
}
