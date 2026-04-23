import type { DiagramRenderModel } from "../state/createDiagramRenderModel";
import type { DiagramInteractionState } from "../state/diagramInteractionState";
import {
  formatInteractionSettingValue,
  INTERACTION_SETTING_DESCRIPTORS,
} from "../state/interactionSettings";
import { escapeHtml } from "./escapeHtml";

export function renderInspector(
  viewModel: DiagramRenderModel,
  initialState: DiagramInteractionState,
): string {
  return `
    <aside class="erd-sidebar">
      <section class="erd-summary">
        <p class="erd-summary__eyebrow">Mock Diagram</p>
        <h1 class="erd-summary__title">Django ERD</h1>
        <p class="erd-summary__meta">${viewModel.tables.length} tables · ${viewModel.edges.length} structural edges · ${viewModel.overlays.length} method links</p>
        <p class="erd-summary__meta">
          Layout: <span data-layout-readout>${escapeHtml(viewModel.layoutMode)}</span>
          · <span data-hidden-count>${viewModel.tables.filter((table) => table.hidden).length}</span> hidden
          · ${viewModel.crossings.length} crossings
        </p>
        ${viewModel.modelCatalogMode ? "<p class=\"erd-summary__meta\">Model catalog mode: model and DB table names only.</p>" : ""}
        ${renderTimingSummary(viewModel)}
      </section>
      ${renderSetupSection(initialState)}
      <section class="erd-inspector">
        ${viewModel.tables.map(renderModelPanel).join("")}
      </section>
      ${renderHiddenTables(viewModel)}
      ${renderDiscovery(viewModel)}
      ${renderDiagnostics(viewModel)}
    </aside>
  `;
}

function renderSetupSection(initialState: DiagramInteractionState): string {
  return `
    <section class="erd-sidebar__section">
      <h2>Setup</h2>
      <div class="erd-settings">
        ${INTERACTION_SETTING_DESCRIPTORS.map((descriptor) => {
          const value = initialState.settings[descriptor.key];

          return `
            <label class="erd-setting">
              <span class="erd-setting__header">
                <span class="erd-setting__label">${escapeHtml(descriptor.label)}</span>
                <span class="erd-setting__value" data-setup-value="${escapeHtml(descriptor.key)}">${escapeHtml(formatInteractionSettingValue(value))}</span>
              </span>
              <input
                type="range"
                class="erd-setting__range"
                data-setup-control="${escapeHtml(descriptor.key)}"
                min="${descriptor.min}"
                max="${descriptor.max}"
                step="${descriptor.step}"
                value="${value}"
              />
              <span class="erd-sidebar__meta">${escapeHtml(descriptor.hint)}</span>
            </label>
          `;
        }).join("")}
      </div>
    </section>
  `;
}

function renderTimingSummary(viewModel: DiagramRenderModel): string {
  const timingParts = [
    formatTiming("discovery", viewModel.timings?.discoveryMs),
    formatTiming("parse", viewModel.timings?.parseMs),
    formatTiming("extract", viewModel.timings?.extractMs),
    formatTiming("graph", viewModel.timings?.graphMs),
    formatTiming("layout", viewModel.timings?.layoutMs),
    formatTiming("ogdf", viewModel.timings?.ogdfLayoutMs),
    formatTiming("render", viewModel.timings?.renderDocumentMs),
  ].filter((value): value is string => Boolean(value));

  if (timingParts.length === 0) {
    return "";
  }

  return `<p class="erd-summary__meta">${escapeHtml(timingParts.join(" · "))}</p>`;
}

function renderDiagnostics(viewModel: DiagramRenderModel): string {
  return `
    <section class="erd-sidebar__section">
      <h2>Diagnostics</h2>
      <ul class="erd-list">
        ${
          viewModel.inspector.diagnostics.length > 0
            ? viewModel.inspector.diagnostics
                .map(
                  (diagnostic) => `
                    <li class="erd-list__item">
                      <span class="erd-badge erd-badge--${escapeHtml(diagnostic.severity)}">${escapeHtml(diagnostic.code)}</span>
                      <span>${escapeHtml(diagnostic.message)}</span>
                    </li>
                  `,
                )
                .join("")
            : "<li class=\"erd-list__item\"><span>No analyzer diagnostics.</span></li>"
        }
      </ul>
    </section>
  `;
}

function renderDiscovery(viewModel: DiagramRenderModel): string {
  if (!viewModel.inspector.discovery) {
    return "";
  }

  return `
    <section class="erd-sidebar__section">
      <h2>Discovery</h2>
      <p class="erd-sidebar__meta">${escapeHtml(viewModel.inspector.discovery.strategy)} · ${viewModel.inspector.discovery.appCount} apps</p>
      <p class="erd-sidebar__meta">${escapeHtml(viewModel.inspector.discovery.selectedRoot)}</p>
      <ul class="erd-list">
        ${viewModel.inspector.discovery.apps
          .map(
            (app) => `
              <li class="erd-list__item">
                <strong>${escapeHtml(app.appLabel)}</strong>
                <span>${escapeHtml(app.flags.join(" · "))}</span>
              </li>
            `,
          )
          .join("")}
      </ul>
    </section>
  `;
}

function renderHiddenTables(viewModel: DiagramRenderModel): string {
  return `
    <section class="erd-sidebar__section">
      <h2>Hidden Tables</h2>
      <ul class="erd-list">
        ${viewModel.tables
          .map(
            (table) => `
              <li
                class="erd-list__item erd-hidden-table"
                data-hidden-model-item
                data-model-id="${escapeHtml(table.modelId)}"
                ${table.hidden ? "" : "hidden"}
              >
                <span>${escapeHtml(table.modelId)}</span>
                <button
                  type="button"
                  class="erd-inline-button"
                  data-show-hidden-model
                  data-model-id="${escapeHtml(table.modelId)}"
                >Show</button>
              </li>
            `,
          )
          .join("")}
      </ul>
    </section>
  `;
}

function renderMethodButtons(table: DiagramRenderModel["tables"][number]): string {
  return `
    <div class="erd-method-buttons" data-method-list ${table.showMethods && table.methods.length > 0 ? "" : "hidden"}>
      ${table.methods
        .map((method) => {
          const active =
            viewMethodSelection(table, method.name) ? " is-active" : "";

          return `
            <article class="erd-method-card">
              <button
                type="button"
                class="erd-method-button${active}"
                data-method-button
                data-method-name="${escapeHtml(method.name)}"
                data-model-id="${escapeHtml(table.modelId)}"
              >
                <span>fn ${escapeHtml(method.name)}</span>
                <span>${method.relatedModels.length} links</span>
              </button>
              ${renderMethodRelations(method)}
            </article>
          `;
        })
        .join("")}
    </div>
  `;
}

function renderModelPanel(table: DiagramRenderModel["tables"][number]): string {
  const selectedClass = table.selected ? " is-selected" : "";
  if (table.fieldRows.length === 0 && table.properties.length === 0 && table.methods.length === 0) {
    return `
      <section
        class="erd-panel${selectedClass}"
        data-model-panel
        data-model-id="${escapeHtml(table.modelId)}"
        ${table.selected ? "" : "hidden"}
      >
        <header class="erd-panel__header">
          <p class="erd-panel__eyebrow">${escapeHtml(table.appLabel)}</p>
          <h2>${escapeHtml(table.modelName)}</h2>
          <p class="erd-panel__meta">${escapeHtml(table.databaseTableName)}</p>
        </header>
        <div class="erd-panel__controls">
          ${renderToggleButton(table, "hidden", table.hidden ? "Show Table" : "Hide Table")}
        </div>
      </section>
    `;
  }

  return `
    <section
      class="erd-panel${selectedClass}"
      data-model-panel
      data-model-id="${escapeHtml(table.modelId)}"
      ${table.selected ? "" : "hidden"}
    >
      <header class="erd-panel__header">
        <p class="erd-panel__eyebrow">${escapeHtml(table.appLabel)}</p>
        <h2>${escapeHtml(table.modelName)}</h2>
        <p class="erd-panel__meta">${table.fieldRows.length} rows · ${table.properties.length} properties · ${table.methods.length} methods</p>
      </header>
      <div class="erd-panel__controls">
        ${renderToggleButton(table, "hidden", table.hidden ? "Show Table" : "Hide Table")}
        ${renderToggleButton(table, "showMethods", "Methods")}
        ${renderToggleButton(table, "showProperties", "Properties")}
        ${renderToggleButton(table, "showMethodHighlights", "Method Links")}
      </div>
      <div class="erd-panel__section">
        <h3>Methods</h3>
        <p class="erd-panel__hint" data-method-hidden-hint ${table.showMethods ? "hidden" : ""}>Methods are hidden by the current table view state.</p>
        <p class="erd-panel__hint" data-empty-method-hint ${table.methods.length > 0 ? "hidden" : ""}>No user-defined methods.</p>
        ${renderMethodButtons(table)}
      </div>
      <div class="erd-panel__section">
        <h3>Properties</h3>
        <p class="erd-panel__hint" data-property-hidden-hint ${table.showProperties ? "hidden" : ""}>Properties are hidden by the current table view state.</p>
        <p class="erd-panel__hint" data-empty-property-hint ${table.properties.length > 0 ? "hidden" : ""}>No computed properties.</p>
        <ul class="erd-list" data-property-list ${table.showProperties && table.properties.length > 0 ? "" : "hidden"}>
          ${table.properties
            .map((property) => `<li class="erd-list__item"><span>@ ${escapeHtml(property)}</span></li>`)
            .join("")}
        </ul>
      </div>
      <div class="erd-panel__section">
        <h3>Field Summary</h3>
        <ul class="erd-list">
          ${table.fieldRows
            .map(
              (row) => `
                <li class="erd-list__item erd-list__item--${escapeHtml(row.tone)}">
                  <span>${escapeHtml(row.text)}</span>
                </li>
              `,
            )
            .join("")}
        </ul>
      </div>
    </section>
  `;
}

function renderToggleButton(
  table: DiagramRenderModel["tables"][number],
  toggle: "hidden" | "showMethodHighlights" | "showMethods" | "showProperties",
  label: string,
): string {
  const active = toggle === "hidden" ? !table.hidden : Boolean(table[toggle]);
  const statusText = toggle === "hidden" ? (table.hidden ? "Hidden" : "Visible") : active ? "On" : "Off";

  return `
    <button
      type="button"
      class="erd-control-pill${active ? " is-active" : ""}"
      data-table-toggle="${toggle}"
      data-model-id="${escapeHtml(table.modelId)}"
    >
      <span>${escapeHtml(label)}</span>
      <span class="erd-control-pill__status" data-control-status>${statusText}</span>
    </button>
  `;
}

function viewMethodSelection(
  table: DiagramRenderModel["tables"][number],
  methodName: string,
): boolean {
  return table.selected && table.activeMethodName === methodName;
}

function renderMethodRelations(
  method: DiagramRenderModel["tables"][number]["methods"][number],
): string {
  if (method.relatedModels.length === 0) {
    return "<p class=\"erd-panel__hint\">No related models inferred for this method.</p>";
  }

  return `
    <div class="erd-method-links">
      ${method.relatedModels
        .map(
          (reference) => `
            <span class="erd-relation-chip erd-relation-chip--${escapeHtml(reference.confidence)}">
              ${escapeHtml(methodRelationLabel(reference))}
            </span>
          `,
        )
        .join("")}
    </div>
  `;
}

function methodRelationLabel(
  reference: DiagramRenderModel["tables"][number]["methods"][number]["relatedModels"][number],
): string {
  if (reference.targetModelId) {
    return reference.targetModelId;
  }

  if (reference.rawReference) {
    return `${reference.rawReference} (unresolved)`;
  }

  return "unresolved model";
}

function formatTiming(label: string, value: number | undefined): string | undefined {
  if (value === undefined) {
    return undefined;
  }

  return `${label} ${Math.round(value * 10) / 10}ms`;
}
