import {
  DEFAULT_INTERACTION_SETTINGS,
  INTERACTION_SETTING_DESCRIPTORS,
} from "../../state/interactionSettings";

export function getBrowserStateSource(): string {
  const defaultInteractionSettingsJson = JSON.stringify(DEFAULT_INTERACTION_SETTINGS);
  const interactionSettingDescriptorsJson = JSON.stringify(INTERACTION_SETTING_DESCRIPTORS);

  return `
        const defaultInteractionSettings = ${defaultInteractionSettingsJson};
        const interactionSettingDescriptors = ${interactionSettingDescriptorsJson};
        const MIN_VIEWPORT_ZOOM = 0.005;
        const ERD_LOG_SLOW_RENDER_MS = 16;
        const ERD_LOG_SLOW_EVENT_MS = 8;

        function createErdLogTimestamp() {
          const now = new Date();
          const pad = (value, size) => String(value).padStart(size, "0");

          return (
            now.getFullYear() +
            "-" +
            pad(now.getMonth() + 1, 2) +
            "-" +
            pad(now.getDate(), 2) +
            " " +
            pad(now.getHours(), 2) +
            ":" +
            pad(now.getMinutes(), 2) +
            ":" +
            pad(now.getSeconds(), 2) +
            "." +
            pad(now.getMilliseconds(), 3)
          );
        }

        function getErdLogVersion() {
          return renderModel.appVersion || "0.0.0";
        }

        function logErd(level, event, details) {
          const timestamp = createErdLogTimestamp();
          const version = getErdLogVersion();
          const detailText = details ? " " + JSON.stringify(details) : "";
          const message = "[" + timestamp + "][v" + version + "] " + event;
          const line = message + detailText;

          if (level === "error") {
            console.error(line);
          } else if (level === "warn") {
            console.warn(line);
          } else {
            console.info(line);
          }

          vscode?.postMessage({
            details,
            event,
            level,
            message,
            timestamp,
            type: "diagram.log",
            version,
          });
        }

        function logErdDuration(level, event, startedAt, details) {
          logErd(level, event, {
            ...(details || {}),
            durationMs: round2(performance.now() - startedAt),
          });
        }

        function cloneState(source) {
          return JSON.parse(JSON.stringify(source));
        }

        function getViewportScreenRect() {
          const drawingRect = drawingCanvas
            ? drawingCanvas.getBoundingClientRect()
            : undefined;
          const rect =
            drawingRect && drawingRect.width > 1 && drawingRect.height > 1
              ? drawingRect
              : canvas.getBoundingClientRect();

          return {
            height: Math.max(1, rect.height),
            width: Math.max(1, rect.width),
          };
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
            settings: normalizeInteractionSettings(value.settings),
            selectedMethodContext: value.selectedMethodContext,
            selectedModelId: value.selectedModelId || fallbackModelId,
            tableOptions: Array.isArray(value.tableOptions) ? value.tableOptions : [],
            viewport: isPlaceholderViewport(value.viewport)
              ? fallbackViewport
              : normalizeViewport(value.viewport, fallbackViewport),
          };
        }

        function computeInitialViewport(initialValue) {
          return computeViewportForLayout(
            initialValue.layoutMode || "hierarchical",
            Array.isArray(initialValue.tableOptions) ? initialValue.tableOptions : [],
            { keepCatalogReadable: true },
          );
        }

        function computeViewportForLayout(layoutMode, tableOptions, options) {
          const viewportRect = getViewportScreenRect();
          const canvasWidth = viewportRect.width;
          const canvasHeight = viewportRect.height;
          if (canvasWidth <= 1 || canvasHeight <= 1) {
            return {
              panX: 32,
              panY: 24,
              zoom: 1,
            };
          }

          const optionsByModelId = new Map(
            (Array.isArray(tableOptions) ? tableOptions : []).map((options) => [
              options.modelId,
              options,
            ]),
          );
          const layout = layoutVariants[layoutMode] || layoutVariants.hierarchical || {};
          let minX = Number.POSITIVE_INFINITY;
          let minY = Number.POSITIVE_INFINITY;
          let maxX = Number.NEGATIVE_INFINITY;
          let maxY = Number.NEGATIVE_INFINITY;
          let visibleCount = 0;

          for (const table of tableMetaById.values()) {
            const options = optionsByModelId.get(table.modelId);
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
          const fittedZoom = Math.min(
            Math.max(MIN_VIEWPORT_ZOOM, (canvasWidth - screenPadding * 2) / worldWidth),
            Math.max(MIN_VIEWPORT_ZOOM, (canvasHeight - screenPadding * 2) / worldHeight),
          );
          const catalogTableWidth = renderModel.modelCatalogMode
            ? Math.max(
                1,
                ...Array.from(tableMetaById.values()).map((table) => table.width || 0),
              )
            : 0;
          const minimumCatalogZoom = renderModel.modelCatalogMode
            ? Math.max(0.18, Math.min(0.28, 72 / catalogTableWidth))
            : MIN_VIEWPORT_ZOOM;
          const zoom = clampZoom(
            renderModel.modelCatalogMode && options?.keepCatalogReadable
              ? Math.max(fittedZoom, minimumCatalogZoom)
              : fittedZoom,
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

        function computeCenteredViewportForLayout(layoutMode, tableOptions, zoom) {
          const bounds = computeLayoutBounds(layoutMode, tableOptions);
          if (!bounds) {
            return {
              ...state.viewport,
            };
          }

          const viewportRect = getViewportScreenRect();
          const canvasWidth = viewportRect.width;
          const canvasHeight = viewportRect.height;
          const centerX = (bounds.minX + bounds.maxX) / 2;
          const centerY = (bounds.minY + bounds.maxY) / 2;

          return {
            panX: Math.round((canvasWidth / 2 - centerX * zoom) * 100) / 100,
            panY: Math.round((canvasHeight / 2 - centerY * zoom) * 100) / 100,
            zoom,
          };
        }

        function computeLayoutBounds(layoutMode, tableOptions) {
          const optionsByModelId = new Map(
            (Array.isArray(tableOptions) ? tableOptions : []).map((options) => [
              options.modelId,
              options,
            ]),
          );
          const layout = layoutVariants[layoutMode] || layoutVariants.hierarchical || {};
          let minX = Number.POSITIVE_INFINITY;
          let minY = Number.POSITIVE_INFINITY;
          let maxX = Number.NEGATIVE_INFINITY;
          let maxY = Number.NEGATIVE_INFINITY;
          let visibleCount = 0;

          for (const table of tableMetaById.values()) {
            const options = optionsByModelId.get(table.modelId);
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
            return undefined;
          }

          return {
            maxX,
            maxY,
            minX,
            minY,
            visibleCount,
          };
        }

        function createViewportPanToWorldPointAction(worldPoint) {
          const viewportRect = getViewportScreenRect();
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);

          return {
            panX: Math.round((viewportRect.width / 2 - worldPoint.x * zoom) * 100) / 100,
            panY: Math.round((viewportRect.height / 2 - worldPoint.y * zoom) * 100) / 100,
            type: "set-viewport-pan",
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

        function normalizeInteractionSettings(settings) {
          const source = settings || {};
          return {
            panSpeed: clampInteractionSetting("panSpeed", source.panSpeed),
            zoomSpeed: clampInteractionSetting("zoomSpeed", source.zoomSpeed),
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
          return Math.max(MIN_VIEWPORT_ZOOM, Math.min(2.2, value));
        }

        function clampInteractionSetting(key, value) {
          const descriptor = interactionSettingDescriptors.find((item) => item.key === key);
          const fallback = defaultInteractionSettings[key];
          if (!descriptor || !Number.isFinite(value)) {
            return fallback;
          }

          return roundToStep(
            Math.max(descriptor.min, Math.min(descriptor.max, value)),
            descriptor.step,
          );
        }

        function formatInteractionSettingValue(key) {
          return Math.round(getInteractionSetting(state, key) * 100) + "%";
        }

        function getInteractionSetting(currentState, key) {
          if (!currentState || !currentState.settings) {
            return defaultInteractionSettings[key];
          }

          const value = currentState.settings[key];
          return Number.isFinite(value) ? value : defaultInteractionSettings[key];
        }

        function roundToStep(value, step) {
          return Math.round(Math.round(value / step) * step * 100) / 100;
        }

        function reduceState(currentState, action) {
          switch (action.type) {
            case "reset-view":
              return {
                ...cloneState(action.initialState),
                settings: {
                  ...currentState.settings,
                },
              };
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
                viewport: computeViewportForLayout(
                  action.layoutMode,
                  currentState.tableOptions,
                  { keepCatalogReadable: true },
                ),
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
            case "set-interaction-setting":
              return {
                ...currentState,
                settings: {
                  ...currentState.settings,
                  [action.key]: clampInteractionSetting(action.key, action.value),
                },
              };
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
            case "fit-viewport":
              return {
                ...currentState,
                viewport: computeViewportForLayout(
                  currentState.layoutMode,
                  currentState.tableOptions,
                  { keepCatalogReadable: false },
                ),
              };
            case "center-viewport":
              return {
                ...currentState,
                viewport: computeCenteredViewportForLayout(
                  currentState.layoutMode,
                  currentState.tableOptions,
                  currentState.viewport.zoom,
                ),
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
