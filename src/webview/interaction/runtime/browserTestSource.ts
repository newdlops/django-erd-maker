export function getBrowserTestSource(): string {
  return `
        function createTestSnapshot() {
          return {
            catalogCrossings: latestCatalogCrossings.map((crossing) => ({
              x: crossing.position.x,
              y: crossing.position.y,
            })),
            crossings: renderedCrossings.map((crossing) => ({
              hidden: false,
              id: crossing.id || "",
            })),
            edges: renderedEdges.map((edge) => ({
              edgeId: edge.edgeId,
              hidden: false,
              points: pointsToAttribute(edge.points),
              sourceModelId: edge.meta.sourceModelId,
              targetModelId: edge.meta.targetModelId,
            })),
            gpu: {
              initialized: Boolean(gpuRenderer),
              warningVisible: Boolean(gpuWarning && !gpuWarning.hidden),
            },
            hiddenModelIds: state.tableOptions
              .filter((options) => options.hidden)
              .map((options) => options.modelId),
            minimap: createMinimapTestSnapshot(),
            overlays: renderedOverlays.map((overlay) => ({
              active: overlay.active,
              hidden: !overlay.active,
              id: overlay.id || "",
              methodName: overlay.methodName,
              sourceModelId: overlay.sourceModelId,
              targetModelId: overlay.targetModelId,
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
              hidden: getTableOptions(state, modelId).hidden,
              isMethodTarget: isMethodTarget(modelId),
              modelId,
              selected: state.selectedModelId === modelId,
              showMethodHighlights: getTableOptions(state, modelId).showMethodHighlights,
              showMethods: getTableOptions(state, modelId).showMethods,
              showProperties: getTableOptions(state, modelId).showProperties,
              tableName: meta.tableName,
              transform:
                "translate(" +
                getCurrentPosition(modelId).x +
                " " +
                getCurrentPosition(modelId).y +
                ")",
            })),
          };
        }

        function createMinimapTestSnapshot() {
          const canvasRect = minimapCanvas
            ? minimapCanvas.getBoundingClientRect()
            : { height: 0, width: 0 };
          const viewportRect = readMinimapViewportRect();

          return {
            canvasRect: {
              height: canvasRect.height,
              width: canvasRect.width,
            },
            viewport: viewportRect,
            visible: Boolean(minimap && !minimap.hidden),
          };
        }

        function readMinimapViewportRect() {
          if (!minimapViewport) {
            return {
              height: 0,
              width: 0,
              x: 0,
              y: 0,
            };
          }

          const transform = minimapViewport.style.transform || "";
          const match = transform.match(/translate\\((-?[0-9.]+)px,\\s*(-?[0-9.]+)px\\)/);

          return {
            height: Number.parseFloat(minimapViewport.style.height || "0") || 0,
            width: Number.parseFloat(minimapViewport.style.width || "0") || 0,
            x: match ? Number(match[1]) : 0,
            y: match ? Number(match[2]) : 0,
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
            case "clickZoomAction":
              requireElement(
                zoomButtons.find((button) => button.dataset.zoomAction === action.zoomAction),
                "zoom button " + action.zoomAction,
              ).click();
              return;
            case "clickMethod":
              requireElement(
                Array.from(document.querySelectorAll("[data-method-button]")).find((button) =>
                  button.dataset.modelId === action.modelId &&
                  button.dataset.methodName === action.methodName,
                ),
                "method button " + action.modelId + "." + action.methodName,
              ).click();
              return;
            case "clickShowHiddenModel":
              requireElement(
                Array.from(document.querySelectorAll("[data-show-hidden-model]")).find((button) =>
                  button.dataset.modelId === action.modelId,
                ),
                "show hidden button " + action.modelId,
              ).click();
              return;
            case "clickTable":
              dispatch({
                modelId: action.modelId,
                type: "select-model",
              });
              return;
            case "clickTableToggle":
              requireElement(
                Array.from(document.querySelectorAll("[data-table-toggle]")).find((button) =>
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
