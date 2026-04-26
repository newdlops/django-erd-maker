export function getBrowserDomSource(): string {
  return `
        function escapeInspectorHtml(value) {
          return String(value ?? "")
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#39;");
        }

        function readPanelMeta(panel) {
          return {
            element: panel,
            emptyMethodHint: panel.querySelector("[data-empty-method-hint]"),
            emptyPropertyHint: panel.querySelector("[data-empty-property-hint]"),
            methodHiddenHint: panel.querySelector("[data-method-hidden-hint]"),
            methodList: panel.querySelector("[data-method-list]"),
            propertyHiddenHint: panel.querySelector("[data-property-hidden-hint]"),
            propertyList: panel.querySelector("[data-property-list]"),
            toggleButtons: Array.from(panel.querySelectorAll("[data-table-toggle]")),
          };
        }

        function readTableMeta(table) {
          return {
            appLabel: table.appLabel || "",
            clusterId: table.clusterId || "",
            basePosition: {
              x: Number(table.position?.x || 0),
              y: Number(table.position?.y || 0),
            },
            fieldRows: Array.isArray(table.fieldRows) ? table.fieldRows.slice() : [],
            hasExplicitDatabaseTableName: table.hasExplicitDatabaseTableName === true,
            height: Number(table.size?.height || 0),
            methods: Array.isArray(table.methods) ? table.methods.slice() : [],
            modelId: table.modelId || "",
            modelName: table.modelName || "",
            properties: Array.isArray(table.properties) ? table.properties.slice() : [],
            tableName: table.databaseTableName || table.modelName || "",
            width: Number(table.size?.width || 0),
          };
        }

        function getSelectedPanelModelId() {
          return state.selectedModelId || (tableMetaList[0] && tableMetaList[0].modelId) || "";
        }

        function renderInspectorToggleButton(table, toggle, label, options) {
          const active = toggle === "hidden" ? !options.hidden : Boolean(options[toggle]);
          const statusText =
            toggle === "hidden"
              ? options.hidden ? "Hidden" : "Visible"
              : active ? "On" : "Off";

          return (
            '<button type="button" class="erd-control-pill' +
            (active ? " is-active" : "") +
            '" data-table-toggle="' +
            escapeInspectorHtml(toggle) +
            '" data-model-id="' +
            escapeInspectorHtml(table.modelId) +
            '">' +
            "<span>" +
            escapeInspectorHtml(label) +
            "</span>" +
            '<span class="erd-control-pill__status" data-control-status>' +
            escapeInspectorHtml(statusText) +
            "</span>" +
            "</button>"
          );
        }

        function renderInspectorMethodRelations(method) {
          if (!Array.isArray(method.relatedModels) || method.relatedModels.length === 0) {
            return '<p class="erd-panel__hint">No related models inferred for this method.</p>';
          }

          return (
            '<div class="erd-method-links">' +
            method.relatedModels
              .map((reference) => {
                const label = reference.targetModelId
                  ? reference.targetModelId
                  : reference.rawReference
                    ? reference.rawReference + " (unresolved)"
                    : "unresolved model";
                return (
                  '<span class="erd-relation-chip erd-relation-chip--' +
                  escapeInspectorHtml(reference.confidence || "medium") +
                  '">' +
                  escapeInspectorHtml(label) +
                  "</span>"
                );
              })
              .join("") +
            "</div>"
          );
        }

        function renderInspectorMethodButtons(table, options) {
          const methods = Array.isArray(table.methods) ? table.methods : [];
          const methodListHidden = !options.showMethods || methods.length === 0;

          return (
            '<div class="erd-method-buttons" data-method-list ' +
            (methodListHidden ? "hidden" : "") +
            ">" +
            methods
              .map((method) => {
                const active =
                  state.selectedMethodContext &&
                  state.selectedMethodContext.modelId === table.modelId &&
                  state.selectedMethodContext.methodName === method.name;

                return (
                  '<article class="erd-method-card">' +
                  '<button type="button" class="erd-method-button' +
                  (active ? " is-active" : "") +
                  '" data-method-button data-method-name="' +
                  escapeInspectorHtml(method.name) +
                  '" data-model-id="' +
                  escapeInspectorHtml(table.modelId) +
                  '">' +
                  "<span>fn " +
                  escapeInspectorHtml(method.name) +
                  "</span>" +
                  "<span>" +
                  String(Array.isArray(method.relatedModels) ? method.relatedModels.length : 0) +
                  " links</span>" +
                  "</button>" +
                  renderInspectorMethodRelations(method) +
                  "</article>"
                );
              })
              .join("") +
            "</div>"
          );
        }

        function renderInspectorPanelMarkup(modelId) {
          const table = tableRenderById.get(modelId);
          if (!table) {
            return (
              '<section class="erd-panel" data-model-panel hidden>' +
              '<header class="erd-panel__header">' +
              '<p class="erd-panel__eyebrow">No Selection</p>' +
              "<h2>No models available</h2>" +
              '<p class="erd-panel__meta">The current diagram has no visible models.</p>' +
              "</header>" +
              "</section>"
            );
          }

          const options = getTableOptions(state, modelId);
          const fields = Array.isArray(table.fieldRows) ? table.fieldRows : [];
          const properties = Array.isArray(table.properties) ? table.properties : [];
          const methods = Array.isArray(table.methods) ? table.methods : [];
          const selectedClass = state.selectedModelId === modelId ? " is-selected" : "";
          const noDetails = fields.length === 0 && properties.length === 0 && methods.length === 0;

          if (noDetails) {
            return (
              '<section class="erd-panel' +
              selectedClass +
              '" data-model-panel data-model-id="' +
              escapeInspectorHtml(modelId) +
              '">' +
              '<header class="erd-panel__header">' +
              '<p class="erd-panel__eyebrow">' +
              escapeInspectorHtml(table.appLabel) +
              "</p>" +
              "<h2>" +
              escapeInspectorHtml(table.modelName) +
              "</h2>" +
              '<p class="erd-panel__meta">' +
              escapeInspectorHtml(table.databaseTableName) +
              "</p>" +
              "</header>" +
              '<div class="erd-panel__controls">' +
              renderInspectorToggleButton(table, "hidden", options.hidden ? "Show Table" : "Hide Table", options) +
              "</div>" +
              "</section>"
            );
          }

          return (
            '<section class="erd-panel' +
            selectedClass +
            '" data-model-panel data-model-id="' +
            escapeInspectorHtml(modelId) +
            '">' +
            '<header class="erd-panel__header">' +
            '<p class="erd-panel__eyebrow">' +
            escapeInspectorHtml(table.appLabel) +
            "</p>" +
            "<h2>" +
            escapeInspectorHtml(table.modelName) +
            "</h2>" +
            '<p class="erd-panel__meta">' +
            escapeInspectorHtml(
              fields.length + " rows · " + properties.length + " properties · " + methods.length + " methods",
            ) +
            "</p>" +
            "</header>" +
            '<div class="erd-panel__controls">' +
            renderInspectorToggleButton(table, "hidden", options.hidden ? "Show Table" : "Hide Table", options) +
            renderInspectorToggleButton(table, "showMethods", "Methods", options) +
            renderInspectorToggleButton(table, "showProperties", "Properties", options) +
            renderInspectorToggleButton(table, "showMethodHighlights", "Method Links", options) +
            "</div>" +
            '<div class="erd-panel__section">' +
            "<h3>Methods</h3>" +
            '<p class="erd-panel__hint" data-method-hidden-hint ' +
            (options.showMethods ? "hidden" : "") +
            '>Methods are hidden by the current table view state.</p>' +
            '<p class="erd-panel__hint" data-empty-method-hint ' +
            (methods.length > 0 ? "hidden" : "") +
            '>No user-defined methods.</p>' +
            renderInspectorMethodButtons(table, options) +
            "</div>" +
            '<div class="erd-panel__section">' +
            "<h3>Properties</h3>" +
            '<p class="erd-panel__hint" data-property-hidden-hint ' +
            (options.showProperties ? "hidden" : "") +
            '>Properties are hidden by the current table view state.</p>' +
            '<p class="erd-panel__hint" data-empty-property-hint ' +
            (properties.length > 0 ? "hidden" : "") +
            '>No computed properties.</p>' +
            '<ul class="erd-list" data-property-list ' +
            (options.showProperties && properties.length > 0 ? "" : "hidden") +
            ">" +
            properties
              .map((property) =>
                '<li class="erd-list__item"><span>@ ' + escapeInspectorHtml(property) + "</span></li>",
              )
              .join("") +
            "</ul>" +
            "</div>" +
            '<div class="erd-panel__section">' +
            "<h3>Field Summary</h3>" +
            '<ul class="erd-list">' +
            fields
              .map((row) =>
                '<li class="erd-list__item erd-list__item--' +
                escapeInspectorHtml(row.tone) +
                '"><span>' +
                escapeInspectorHtml(row.text) +
                "</span></li>",
              )
              .join("") +
            "</ul>" +
            "</div>" +
            "</section>"
          );
        }

        function renderHiddenModelItemsMarkup() {
          const hiddenIds = state.tableOptions
            .filter((options) => options.hidden)
            .map((options) => options.modelId)
            .sort((left, right) => left.localeCompare(right));

          if (hiddenIds.length === 0) {
            return '<li class="erd-list__item"><span>No hidden tables.</span></li>';
          }

          return hiddenIds
            .map((modelId) => (
              '<li class="erd-list__item erd-hidden-table" data-hidden-model-item data-model-id="' +
              escapeInspectorHtml(modelId) +
              '">' +
              "<span>" +
              escapeInspectorHtml(modelId) +
              "</span>" +
              '<button type="button" class="erd-inline-button" data-show-hidden-model data-model-id="' +
              escapeInspectorHtml(modelId) +
              '">Show</button>' +
              "</li>"
            ))
            .join("");
        }

        function syncPanelMeta() {
          const panelElements = panelHost
            ? Array.from(panelHost.querySelectorAll("[data-model-panel]"))
            : [];
          panelMetaById = new Map(
            panelElements.map((panel) => [panel.dataset.modelId || "", readPanelMeta(panel)]),
          );
        }
  `;
}
