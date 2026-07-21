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

        function setSidebarSheet(sheetId, focusTab) {
          if (!sidebarSheets.some((sheet) => sheet.dataset.sidebarSheet === sheetId)) {
            return;
          }

          for (const button of sidebarTabButtons) {
            const active = button.dataset.sidebarTab === sheetId;
            button.classList.toggle("is-active", active);
            button.setAttribute("aria-selected", String(active));
            button.tabIndex = active ? 0 : -1;
            if (active && focusTab) {
              button.focus();
            }
          }
          for (const sheet of sidebarSheets) {
            sheet.hidden = sheet.dataset.sidebarSheet !== sheetId;
          }
        }

        function getSelectedPanelModelId() {
          return state.selectedModelId || "";
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

        function renderInspectorMethodButtons(table) {
          const methods = Array.isArray(table.methods) ? table.methods : [];
          const methodListHidden = methods.length === 0;

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

        function inspectorRelationshipKindLabel(kind) {
          switch (kind) {
            case "foreign_key": return "FK";
            case "one_to_one": return "O2O";
            case "many_to_many": return "M2M";
            case "inheritance": return "IS-A";
            case "reverse_foreign_key": return "REV FK";
            case "reverse_one_to_one": return "REV O2O";
            case "reverse_many_to_many": return "REV M2M";
            default: return String(kind || "REL").toUpperCase();
          }
        }

        function renderInspectorRelationships(table) {
          const relationships = Array.isArray(table.relationships) ? table.relationships : [];
          if (relationships.length === 0) {
            return '<p class="erd-panel__hint">No declared database relationships.</p>';
          }

          return (
            '<ul class="erd-list erd-relationship-list">' +
            relationships.map((relationship) => {
              const direction = relationship.direction === "incoming"
                ? "incoming"
                : relationship.direction === "self" ? "self" : "outgoing";
              const label = direction === "incoming"
                ? "← " + relationship.otherModelId + "." + relationship.fieldName
                : direction === "self"
                  ? relationship.fieldName + " ↻ " + table.modelId
                  : relationship.fieldName + " → " + relationship.otherModelId;
              return (
                '<li class="erd-list__item erd-relationship">' +
                '<span class="erd-relationship__header">' +
                '<span class="erd-badge erd-badge--relation-' +
                escapeInspectorHtml(String(relationship.kind || "relation").replaceAll("_", "-")) +
                '">' +
                escapeInspectorHtml(inspectorRelationshipKindLabel(relationship.kind)) +
                "</span>" +
                '<span class="erd-sidebar__meta">' + direction + "</span>" +
                "</span>" +
                '<button type="button" class="erd-relationship__target" data-focus-related-model data-model-id="' +
                escapeInspectorHtml(relationship.otherModelId) +
                '">' + escapeInspectorHtml(label) + "</button>" +
                "</li>"
              );
            }).join("") +
            "</ul>"
          );
        }

        function renderInspectorPanelMarkup(modelId) {
          if (!modelId) {
            return (
              '<section class="erd-panel erd-panel--empty" data-model-panel>' +
              '<header class="erd-panel__header">' +
              '<p class="erd-panel__eyebrow">No Selection</p>' +
              "<h2>Select a model</h2>" +
              '<p class="erd-panel__meta">Click a node in the diagram to inspect its model details.</p>' +
              "</header>" +
              "</section>"
            );
          }

          const table = inspectorModelById.get(modelId);
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
          const relationships = Array.isArray(table.relationships) ? table.relationships : [];
          const selectedClass = state.selectedModelId === modelId ? " is-selected" : "";
          const noDetails =
            fields.length === 0
            && properties.length === 0
            && methods.length === 0
            && relationships.length === 0;

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
            escapeInspectorHtml(table.databaseTableName) +
            "</p>" +
            '<p class="erd-panel__meta">' +
            escapeInspectorHtml(
              fields.length + " rows · " + relationships.length + " relationships · " +
              properties.length + " properties · " + methods.length + " methods",
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
            "<h3>Fields &amp; Choices</h3>" +
            '<p class="erd-panel__hint" ' +
            (fields.length > 0 ? "hidden" : "") +
            '>No model fields.</p>' +
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
            '<div class="erd-panel__section">' +
            "<h3>Relationships</h3>" +
            renderInspectorRelationships(table) +
            "</div>" +
            '<div class="erd-panel__section">' +
            "<h3>Properties</h3>" +
            '<p class="erd-panel__hint" data-empty-property-hint ' +
            (properties.length > 0 ? "hidden" : "") +
            '>No computed properties.</p>' +
            '<ul class="erd-list" data-property-list ' +
            (properties.length > 0 ? "" : "hidden") +
            ">" +
            properties
              .map((property) =>
                '<li class="erd-list__item"><span>@ ' + escapeInspectorHtml(property) + "</span></li>",
              )
              .join("") +
            "</ul>" +
            "</div>" +
            '<div class="erd-panel__section">' +
            "<h3>Methods</h3>" +
            '<p class="erd-panel__hint" data-empty-method-hint ' +
            (methods.length > 0 ? "hidden" : "") +
            '>No user-defined methods.</p>' +
            renderInspectorMethodButtons(table) +
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
