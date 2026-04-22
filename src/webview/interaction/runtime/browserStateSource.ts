export function getBrowserStateSource(): string {
  return `
        function cloneState(source) {
          return JSON.parse(JSON.stringify(source));
        }

        function getTableOptions(currentState, modelId) {
          return (
            currentState.tableOptions.find((options) => options.modelId === modelId) || {
              hidden: false,
              modelId,
              showMethodHighlights: true,
              showMethods: true,
              showProperties: true,
            }
          );
        }

        function normalizeInitialState(value, fallbackModelId, defaultViewport) {
          const fallbackViewport = normalizeViewport(defaultViewport, {
            panX: 32,
            panY: 24,
            zoom: 1,
          });

          return {
            layoutMode: value.layoutMode || "hierarchical",
            selectedMethodContext: value.selectedMethodContext,
            selectedModelId: value.selectedModelId || fallbackModelId,
            tableOptions: Array.isArray(value.tableOptions) ? value.tableOptions : [],
            viewport: isPlaceholderViewport(value.viewport)
              ? fallbackViewport
              : normalizeViewport(value.viewport, fallbackViewport),
          };
        }

        function computeInitialViewport(initialValue) {
          const canvasRect = canvas.getBoundingClientRect();
          const canvasWidth = Math.max(1, canvasRect.width);
          const canvasHeight = Math.max(1, canvasRect.height);
          if (canvasWidth <= 1 || canvasHeight <= 1) {
            return {
              panX: 32,
              panY: 24,
              zoom: 1,
            };
          }

          const initialOptions = new Map(
            (Array.isArray(initialValue.tableOptions) ? initialValue.tableOptions : []).map((options) => [
              options.modelId,
              options,
            ]),
          );
          const layoutMode = initialValue.layoutMode || "hierarchical";
          const layout = layoutVariants[layoutMode] || layoutVariants.hierarchical || {};
          let minX = Number.POSITIVE_INFINITY;
          let minY = Number.POSITIVE_INFINITY;
          let maxX = Number.NEGATIVE_INFINITY;
          let maxY = Number.NEGATIVE_INFINITY;
          let visibleCount = 0;

          for (const table of tableMetaById.values()) {
            const options = initialOptions.get(table.modelId);
            if (options && options.hidden) {
              continue;
            }

            const position =
              (options && options.manualPosition) ||
              layout[table.modelId] ||
              table.basePosition || { x: 0, y: 0 };
            minX = Math.min(minX, position.x);
            minY = Math.min(minY, position.y);
            maxX = Math.max(maxX, position.x + table.width);
            maxY = Math.max(maxY, position.y + table.height);
            visibleCount += 1;
          }

          if (!Number.isFinite(minX) || !Number.isFinite(minY) || visibleCount === 0) {
            return {
              panX: 32,
              panY: 24,
              zoom: 1,
            };
          }

          const worldPadding = visibleCount > 500 ? 24 : 40;
          const screenPadding = visibleCount > 500 ? 18 : 28;
          const worldWidth = Math.max(1, maxX - minX + worldPadding * 2);
          const worldHeight = Math.max(1, maxY - minY + worldPadding * 2);
          const zoom = clampZoom(
            Math.min(
              Math.max(0.05, (canvasWidth - screenPadding * 2) / worldWidth),
              Math.max(0.05, (canvasHeight - screenPadding * 2) / worldHeight),
            ),
          );
          const centeredPanX =
            (canvasWidth - worldWidth * zoom) / 2 - (minX - worldPadding) * zoom;
          const centeredPanY =
            (canvasHeight - worldHeight * zoom) / 2 - (minY - worldPadding) * zoom;

          return {
            panX: Math.round(centeredPanX * 100) / 100,
            panY: Math.round(centeredPanY * 100) / 100,
            zoom,
          };
        }

        function normalizeViewport(viewport, fallbackViewport) {
          const source = viewport || fallbackViewport;
          return {
            panX: Number.isFinite(source.panX) ? source.panX : fallbackViewport.panX,
            panY: Number.isFinite(source.panY) ? source.panY : fallbackViewport.panY,
            zoom: clampZoom(Number.isFinite(source.zoom) ? source.zoom : fallbackViewport.zoom),
          };
        }

        function isPlaceholderViewport(viewport) {
          return (
            !viewport ||
            (
              viewport.panX === 32 &&
              viewport.panY === 24 &&
              viewport.zoom === 1
            )
          );
        }

        function clampZoom(value) {
          return Math.max(0.05, Math.min(2.2, value));
        }

        function reduceState(currentState, action) {
          switch (action.type) {
            case "reset-view":
              return cloneState(action.initialState);
            case "select-model":
              return {
                ...currentState,
                selectedMethodContext:
                  currentState.selectedMethodContext &&
                  currentState.selectedMethodContext.modelId === action.modelId
                    ? currentState.selectedMethodContext
                    : undefined,
                selectedModelId: action.modelId,
              };
            case "toggle-method":
              return {
                ...currentState,
                selectedMethodContext:
                  currentState.selectedMethodContext &&
                  currentState.selectedMethodContext.modelId === action.modelId &&
                  currentState.selectedMethodContext.methodName === action.methodName
                    ? undefined
                    : {
                        methodName: action.methodName,
                        modelId: action.modelId,
                      },
                selectedModelId: action.modelId,
              };
            case "set-layout-mode":
              return {
                ...currentState,
                layoutMode: action.layoutMode,
              };
            case "set-table-hidden":
              return withTableOptions(currentState, action.modelId, (options) => ({
                ...options,
                hidden: action.hidden,
              }));
            case "set-table-manual-position":
              return withTableOptions(currentState, action.modelId, (options) => ({
                ...options,
                manualPosition: action.manualPosition ? { ...action.manualPosition } : undefined,
              }));
            case "set-table-show-method-highlights":
              return sanitizeMethodSelection(
                withTableOptions(currentState, action.modelId, (options) => ({
                  ...options,
                  showMethodHighlights: action.showMethodHighlights,
                })),
              );
            case "set-table-show-methods":
              return sanitizeMethodSelection(
                withTableOptions(currentState, action.modelId, (options) => ({
                  ...options,
                  showMethods: action.showMethods,
                })),
              );
            case "set-table-show-properties":
              return withTableOptions(currentState, action.modelId, (options) => ({
                ...options,
                showProperties: action.showProperties,
              }));
            case "set-viewport-pan":
              return {
                ...currentState,
                viewport: {
                  ...currentState.viewport,
                  panX: action.panX,
                  panY: action.panY,
                },
              };
            case "set-viewport-zoom":
              return {
                ...currentState,
                viewport: {
                  ...currentState.viewport,
                  zoom: clampZoom(action.zoom),
                },
              };
            default:
              return currentState;
          }
        }

        function sanitizeMethodSelection(currentState) {
          const selectedMethod = currentState.selectedMethodContext;
          if (!selectedMethod) {
            return currentState;
          }

          const options = getTableOptions(currentState, selectedMethod.modelId);
          if (options.showMethods && options.showMethodHighlights) {
            return currentState;
          }

          return {
            ...currentState,
            selectedMethodContext: undefined,
          };
        }

        function withTableOptions(currentState, modelId, transform) {
          let updated = false;
          const tableOptions = currentState.tableOptions.map((options) => {
            if (options.modelId !== modelId) {
              return {
                ...options,
                manualPosition: options.manualPosition ? { ...options.manualPosition } : undefined,
              };
            }

            updated = true;
            return transform({
              ...options,
              manualPosition: options.manualPosition ? { ...options.manualPosition } : undefined,
            });
          });

          if (!updated) {
            tableOptions.push(
              transform({
                hidden: false,
                modelId,
                showMethodHighlights: true,
                showMethods: true,
                showProperties: true,
              }),
            );
          }

          return {
            ...currentState,
            tableOptions,
          };
        }
  `;
}
