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

        function normalizeInitialState(value, fallbackModelId) {
          return {
            layoutMode: value.layoutMode || "hierarchical",
            selectedMethodContext: value.selectedMethodContext,
            selectedModelId: value.selectedModelId || fallbackModelId,
            tableOptions: Array.isArray(value.tableOptions) ? value.tableOptions : [],
            viewport: value.viewport || {
              panX: 32,
              panY: 24,
              zoom: 1,
            },
          };
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
                  zoom: Math.max(0.45, Math.min(2.2, action.zoom)),
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
