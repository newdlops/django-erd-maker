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
            appLabel: table.appLabel || "",
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
  `;
}
