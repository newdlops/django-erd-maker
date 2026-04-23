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
            {
              keepCatalogReadable: true,
              preferReadableZoom: true,
              settings: normalizeInteractionSettings(initialValue.settings),
            },
          );
        }

        function computeViewportForLayout(layoutMode, tableOptions, options) {
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

          const bounds = computeLayoutBounds(layoutMode, tableOptions, options?.settings);
          if (!bounds) {
            return {
              panX: 32,
              panY: 24,
              zoom: 1,
            };
          }

          const worldPadding = bounds.visibleCount > 500 ? 24 : 44;
          const screenPadding = bounds.visibleCount > 500 ? 18 : 32;
          const worldWidth = Math.max(1, bounds.maxX - bounds.minX + worldPadding * 2);
          const worldHeight = Math.max(1, bounds.maxY - bounds.minY + worldPadding * 2);
          const fittedZoom = Math.min(
            Math.max(MIN_VIEWPORT_ZOOM, (canvasWidth - screenPadding * 2) / worldWidth),
            Math.max(MIN_VIEWPORT_ZOOM, (canvasHeight - screenPadding * 2) / worldHeight),
          );
          const widestTableWidth = Math.max(
            1,
            ...Array.from(tableMetaById.values()).map((table) => table.width || 0),
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
          const minimumReadableZoom =
            !renderModel.modelCatalogMode && options?.preferReadableZoom
              ? estimateReadableViewportZoom(bounds.visibleCount, widestTableWidth)
              : MIN_VIEWPORT_ZOOM;
          const zoom = clampZoom(
            renderModel.modelCatalogMode && options?.keepCatalogReadable
              ? Math.max(fittedZoom, minimumCatalogZoom)
              : Math.max(fittedZoom, minimumReadableZoom),
          );
          const centeredPanX =
            (canvasWidth - worldWidth * zoom) / 2 - (bounds.minX - worldPadding) * zoom;
          const centeredPanY =
            (canvasHeight - worldHeight * zoom) / 2 - (bounds.minY - worldPadding) * zoom;

          return {
            panX: Math.round(centeredPanX * 100) / 100,
            panY: Math.round(centeredPanY * 100) / 100,
            zoom,
          };
        }

        function estimateReadableViewportZoom(visibleCount, widestTableWidth) {
          const widthBasedZoom = Math.max(
            0.24,
            Math.min(0.82, 176 / Math.max(1, widestTableWidth)),
          );

          if (visibleCount <= 24) {
            return Math.max(0.58, widthBasedZoom);
          }

          if (visibleCount <= 60) {
            return Math.max(0.46, widthBasedZoom - 0.06);
          }

          if (visibleCount <= 120) {
            return Math.max(0.36, widthBasedZoom - 0.12);
          }

          return Math.max(0.28, widthBasedZoom - 0.18);
        }

        function computeCenteredViewportForLayout(layoutMode, tableOptions, zoom, settingsOverride) {
          const bounds = computeLayoutBounds(layoutMode, tableOptions, settingsOverride);
          if (!bounds) {
            return {
              ...state.viewport,
            };
          }

          const canvasRect = canvas.getBoundingClientRect();
          const canvasWidth = Math.max(1, canvasRect.width);
          const canvasHeight = Math.max(1, canvasRect.height);
          const centerX = (bounds.minX + bounds.maxX) / 2;
          const centerY = (bounds.minY + bounds.maxY) / 2;

          return {
            panX: Math.round((canvasWidth / 2 - centerX * zoom) * 100) / 100,
            panY: Math.round((canvasHeight / 2 - centerY * zoom) * 100) / 100,
            zoom,
          };
        }

        function createCenteredViewportZoomAction(nextZoom) {
          const canvasRect = canvas.getBoundingClientRect();
          const anchorX = canvasRect.width / 2;
          const anchorY = canvasRect.height / 2;
          const previousZoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const zoom = clampZoom(nextZoom);
          const worldX = (anchorX - state.viewport.panX) / previousZoom;
          const worldY = (anchorY - state.viewport.panY) / previousZoom;

          return {
            panX: Math.round((anchorX - worldX * zoom) * 100) / 100,
            panY: Math.round((anchorY - worldY * zoom) * 100) / 100,
            type: "set-viewport-zoom",
            zoom,
          };
        }

        function createViewportPanToWorldPointAction(worldPoint) {
          const canvasRect = canvas.getBoundingClientRect();
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);

          return {
            panX: Math.round((canvasRect.width / 2 - worldPoint.x * zoom) * 100) / 100,
            panY: Math.round((canvasRect.height / 2 - worldPoint.y * zoom) * 100) / 100,
            type: "set-viewport-pan",
          };
        }

        function computeLayoutBounds(layoutMode, tableOptions, settingsOverride) {
          const optionsByModelId = new Map(
            (Array.isArray(tableOptions) ? tableOptions : []).map((options) => [
              options.modelId,
              options,
            ]),
          );
          const layout = getLayoutForMode(layoutMode, settingsOverride);
          const bounds = createEmptyLayoutBounds();

          for (const table of tableMetaById.values()) {
            const options = optionsByModelId.get(table.modelId);
            if (options && options.hidden) {
              continue;
            }

            const position =
              (options && options.manualPosition) ||
              layout[table.modelId] ||
              table.basePosition || { x: 0, y: 0 };
            const tableBounds = {
              maxX: round2(position.x + table.width),
              maxY: round2(position.y + table.height),
              minX: round2(position.x),
              minY: round2(position.y),
              modelId: table.modelId,
            };

            expandLayoutBoundsWithRect(bounds, tableBounds);
            bounds.visibleCount += 1;
          }

          if (!hasFiniteLayoutBounds(bounds) || bounds.visibleCount === 0) {
            return undefined;
          }

          return bounds;
        }

        function createEmptyLayoutBounds() {
          return {
            maxX: Number.NEGATIVE_INFINITY,
            maxY: Number.NEGATIVE_INFINITY,
            minX: Number.POSITIVE_INFINITY,
            minY: Number.POSITIVE_INFINITY,
            visibleCount: 0,
          };
        }

        function hasFiniteLayoutBounds(bounds) {
          return (
            Number.isFinite(bounds.minX) &&
            Number.isFinite(bounds.minY) &&
            Number.isFinite(bounds.maxX) &&
            Number.isFinite(bounds.maxY)
          );
        }

        function expandLayoutBoundsWithRect(bounds, rect) {
          bounds.minX = Math.min(bounds.minX, rect.minX);
          bounds.minY = Math.min(bounds.minY, rect.minY);
          bounds.maxX = Math.max(bounds.maxX, rect.maxX);
          bounds.maxY = Math.max(bounds.maxY, rect.maxY);
        }

        function expandLayoutBoundsWithPoint(bounds, point) {
          if (!point || !Number.isFinite(point.x) || !Number.isFinite(point.y)) {
            return;
          }

          bounds.minX = Math.min(bounds.minX, point.x);
          bounds.minY = Math.min(bounds.minY, point.y);
          bounds.maxX = Math.max(bounds.maxX, point.x);
          bounds.maxY = Math.max(bounds.maxY, point.y);
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
            edgeDetour: clampInteractionSetting("edgeDetour", source.edgeDetour),
            nodeSpacing: clampInteractionSetting("nodeSpacing", source.nodeSpacing),
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
          const value = getInteractionSetting(state, key);

          if (key === "nodeSpacing" || key === "edgeDetour") {
            return value.toFixed(2) + "x";
          }

          return Math.round(value * 100) + "%";
        }

        function pickLayoutRoutingSettings(settings) {
          const source = settings || {};

          return {
            edgeDetour: clampInteractionSetting("edgeDetour", source.edgeDetour),
            nodeSpacing: clampInteractionSetting("nodeSpacing", source.nodeSpacing),
          };
        }

        function getAppliedLayoutSettings() {
          if (typeof appliedLayoutSettings !== "undefined" && appliedLayoutSettings) {
            return appliedLayoutSettings;
          }

          return pickLayoutRoutingSettings(state && state.settings);
        }

        function hasPendingLayoutSettings(currentState) {
          const pendingSettings = pickLayoutRoutingSettings(currentState && currentState.settings);
          const appliedSettings = getAppliedLayoutSettings();

          return (
            pendingSettings.edgeDetour !== appliedSettings.edgeDetour ||
            pendingSettings.nodeSpacing !== appliedSettings.nodeSpacing
          );
        }

        function getInteractionSetting(currentState, key) {
          if (!currentState || !currentState.settings) {
            return defaultInteractionSettings[key];
          }

          const value = currentState.settings[key];
          return Number.isFinite(value) ? value : defaultInteractionSettings[key];
        }

        function isLayoutInteractionSetting(key) {
          return key === "nodeSpacing";
        }

        function isRoutingInteractionSetting(key) {
          return key === "edgeDetour";
        }

        function getLayoutForMode(layoutMode, settingsOverride) {
          return getLayoutVariant(
            layoutMode,
            settingsOverride || getAppliedLayoutSettings(),
          );
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
                  {
                    keepCatalogReadable: true,
                    preferReadableZoom: true,
                    settings: getAppliedLayoutSettings(),
                  },
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
            case "set-interaction-setting": {
              const nextSettings = {
                ...currentState.settings,
                [action.key]: clampInteractionSetting(action.key, action.value),
              };
              return {
                ...currentState,
                settings: nextSettings,
                viewport: currentState.viewport,
              };
            }
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
                  panX: Number.isFinite(action.panX) ? action.panX : currentState.viewport.panX,
                  panY: Number.isFinite(action.panY) ? action.panY : currentState.viewport.panY,
                  zoom: clampZoom(action.zoom),
                },
              };
            case "fit-viewport":
              return {
                ...currentState,
                viewport: computeViewportForLayout(
                  currentState.layoutMode,
                  currentState.tableOptions,
                  {
                    keepCatalogReadable: false,
                    settings: getAppliedLayoutSettings(),
                  },
                ),
              };
            case "center-viewport":
              return {
                ...currentState,
                viewport: computeCenteredViewportForLayout(
                  currentState.layoutMode,
                  currentState.tableOptions,
                  currentState.viewport.zoom,
                  getAppliedLayoutSettings(),
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
          const tableOptions = currentState.tableOptions.slice();

          for (let index = 0; index < tableOptions.length; index += 1) {
            const options = tableOptions[index];
            if (options.modelId !== modelId) {
              continue;
            }

            updated = true;
            tableOptions[index] = transform({
              ...options,
              manualPosition: options.manualPosition ? { ...options.manualPosition } : undefined,
            });
            break;
          }

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
