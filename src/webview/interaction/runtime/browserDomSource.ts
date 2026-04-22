export function getBrowserDomSource(): string {
  return `
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
            appLabel: table.dataset.appLabel || "",
            basePosition: {
              x: Number(table.dataset.baseX || 0),
              y: Number(table.dataset.baseY || 0),
            },
            dividers: {
              methods: table.querySelector('[data-table-divider="methods"]'),
              properties: table.querySelector('[data-table-divider="properties"]'),
            },
            element: table,
            height: Number(table.dataset.height || 0),
            hasExplicitDatabaseTableName: table.dataset.explicitDbTable === "true",
            modelId: table.dataset.modelId || "",
            methodsSection: table.querySelector('[data-table-section="methods"]'),
            propertiesSection: table.querySelector('[data-table-section="properties"]'),
            tableName: table.dataset.tableName || table.dataset.modelName || "",
            width: Number(table.dataset.width || 0),
          };
        }
  `;
}
