export function getBrowserTestSource(): string {
  return `
        function createTestSnapshot() {
          const crossings = crossingsLayer
            ? Array.from(crossingsLayer.querySelectorAll("[data-crossing-id]")).map((element) => ({
                hidden: element.hasAttribute("hidden"),
                id: element.dataset.crossingId || "",
              }))
            : [];

          return {
            crossings,
            edges: edgeMeta.map((meta) => ({
              edgeId: meta.edgeId,
              hidden: meta.element.hasAttribute("hidden"),
              points: meta.element.getAttribute("points") || "",
              sourceModelId: meta.sourceModelId,
              targetModelId: meta.targetModelId,
            })),
            hiddenModelIds: state.tableOptions
              .filter((options) => options.hidden)
              .map((options) => options.modelId),
            overlays: overlayMeta.map((meta) => ({
              active: meta.element.classList.contains("is-active"),
              hidden: meta.element.hasAttribute("hidden"),
              id: meta.element.id || "",
              methodName: meta.methodName,
              sourceModelId: meta.sourceModelId,
              targetModelId: meta.targetModelId,
            })),
            panels: Array.from(panelMetaById.entries()).map(([modelId, meta]) => ({
              activeMethodNames: Array.from(
                meta.element.querySelectorAll("[data-method-button].is-active"),
              ).map((button) => button.dataset.methodName || ""),
              hidden: meta.element.hasAttribute("hidden"),
              methodListHidden: meta.methodList ? meta.methodList.hasAttribute("hidden") : true,
              modelId,
              propertyListHidden: meta.propertyList ? meta.propertyList.hasAttribute("hidden") : true,
              selected: meta.element.classList.contains("is-selected"),
            })),
            state: cloneState(state),
            tables: Array.from(tableMetaById.entries()).map(([modelId, meta]) => ({
              hidden: meta.element.hasAttribute("hidden"),
              isMethodTarget: meta.element.classList.contains("is-method-target"),
              modelId,
              selected: meta.element.classList.contains("is-selected"),
              showMethodHighlights: meta.element.dataset.methodHighlights === "true",
              showMethods: meta.element.dataset.showMethods === "true",
              showProperties: meta.element.dataset.showProperties === "true",
              tableName: meta.tableName,
              transform: meta.element.getAttribute("transform") || "",
            })),
          };
        }

        function requireElement(element, label) {
          if (!element) {
            throw new Error("Missing test target: " + label);
          }

          return element;
        }

        function runTestAction(action) {
          switch (action.type) {
            case "snapshot":
              return;
            case "clickLayoutMode":
              requireElement(
                layoutButtons.find((button) => button.dataset.layoutMode === action.layoutMode),
                "layout button " + action.layoutMode,
              ).click();
              return;
            case "clickMethod":
              requireElement(
                methodButtons.find((button) =>
                  button.dataset.modelId === action.modelId &&
                  button.dataset.methodName === action.methodName,
                ),
                "method button " + action.modelId + "." + action.methodName,
              ).click();
              return;
            case "clickShowHiddenModel":
              requireElement(
                showHiddenButtons.find((button) => button.dataset.modelId === action.modelId),
                "show hidden button " + action.modelId,
              ).click();
              return;
            case "clickTable":
              requireElement(
                tableMetaById.get(action.modelId) && tableMetaById.get(action.modelId).element,
                "table " + action.modelId,
              ).click();
              return;
            case "clickTableToggle":
              requireElement(
                tableToggleButtons.find((button) =>
                  button.dataset.modelId === action.modelId &&
                  button.dataset.tableToggle === action.toggle,
                ),
                "table toggle " + action.modelId + "." + action.toggle,
              ).click();
              return;
            case "dragTableTo":
              dispatch({
                manualPosition: {
                  x: action.position.x,
                  y: action.position.y,
                },
                modelId: action.modelId,
                type: "set-table-manual-position",
              });
              return;
            case "resetView":
              dispatch({
                initialState,
                type: "reset-view",
              });
              return;
            default:
              throw new Error("Unsupported test action " + action.type);
          }
        }

        window.addEventListener("message", (event) => {
          const message = event.data;
          if (!message || message.type !== "diagram.test.run") {
            return;
          }

          try {
            runTestAction(message.action);
            vscode?.postMessage({
              requestId: message.requestId,
              snapshot: createTestSnapshot(),
              type: "diagram.test.snapshot",
            });
          } catch (error) {
            vscode?.postMessage({
              message: error instanceof Error ? error.message : String(error),
              requestId: message.requestId,
              type: "diagram.test.error",
            });
          }
        });
  `;
}
