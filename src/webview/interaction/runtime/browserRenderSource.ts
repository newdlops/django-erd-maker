import {
  getOgdfLayoutDefinition,
  OGDF_LAYOUT_MODES,
} from "../../../shared/graph/layoutContract";

export function getBrowserRenderSource(): string {
  const layoutLabelsJson = JSON.stringify(
    Object.fromEntries(
      OGDF_LAYOUT_MODES.map((layoutMode) => [
        layoutMode,
        getOgdfLayoutDefinition(layoutMode).label,
      ]),
    ),
  );

  return `
        let viewportRenderFrame = 0;
        let cachedMinimapMetrics = null;
        const layoutLabels = ${layoutLabelsJson};

        function normalizeLayoutModeId(layoutMode) {
          if (layoutMode === "clustered") return "fast_multipole_multilevel";
          if (layoutMode === "hierarchical") return "hierarchical_barycenter";
          return layoutMode;
        }

        function getLayoutLabel(layoutMode) {
          const normalized = normalizeLayoutModeId(layoutMode);
          return layoutLabels[normalized] || layoutLabels[layoutMode] || normalized;
        }

        function applyState() {
          renderSummary();
          renderSetupControls();
          renderOverlays();
          renderPanels();
          renderHiddenTableList();
          drawCanvas("full");
          renderMinimap("full");
        }

        function applyViewportState() {
          drawCanvas("viewport");
          renderMinimap("viewport");
        }

        function cancelViewportRender() {
          if (!viewportRenderFrame) {
            return;
          }

          window.cancelAnimationFrame(viewportRenderFrame);
          viewportRenderFrame = 0;
        }

        function scheduleViewportRender() {
          if (viewportRenderFrame) {
            return;
          }

          viewportRenderFrame = window.requestAnimationFrame(() => {
            viewportRenderFrame = 0;
            applyViewportState();
          });
        }

        function dispatch(action) {
          state = reduceState(state, action);
          if (
            action.type === "set-viewport-pan" ||
            action.type === "set-viewport-zoom" ||
            action.type === "fit-viewport" ||
            action.type === "center-viewport"
          ) {
            scheduleViewportRender();
            return;
          }

          if (
            action.type === "set-layout-mode" ||
            action.type === "set-table-hidden" ||
            action.type === "set-table-manual-position" ||
            action.type === "reset-view"
          ) {
            invalidateSceneGraph();
          }

          if (action.type === "set-interaction-setting") {
            renderSetupControls();
            return;
          }

          cancelViewportRender();
          applyState();
        }

        function renderMinimap(renderMode) {
          if (!minimap || !minimapCanvas || !minimapViewport) {
            return;
          }

          if (renderMode === "viewport" && cachedMinimapMetrics) {
            updateMinimapViewportCursor(cachedMinimapMetrics);
            return;
          }

          const bounds = computeLayoutBounds(state.layoutMode, state.tableOptions);
          const metrics = createMinimapMetrics(bounds);
          if (!metrics) {
            cachedMinimapMetrics = null;
            minimap.hidden = true;
            return;
          }

          cachedMinimapMetrics = metrics;
          minimap.hidden = false;
          const context = minimapCanvas.getContext("2d");
          if (!context) {
            return;
          }

          context.clearRect(0, 0, metrics.width, metrics.height);
          drawMinimapTables(context, metrics);
          updateMinimapViewportCursor(metrics);
        }

        function createMinimapMetrics(bounds) {
          if (!bounds || !minimapCanvas) {
            return undefined;
          }

          const viewportRect = getViewportScreenRect();
          const worldWidth = Math.max(1, bounds.maxX - bounds.minX);
          const worldHeight = Math.max(1, bounds.maxY - bounds.minY);
          const maxWidth = Math.max(150, Math.min(300, viewportRect.width * 0.24));
          const maxHeight = Math.max(104, Math.min(228, viewportRect.height * 0.28));
          const worldAspect = worldWidth / worldHeight;
          let cssWidth = maxWidth;
          let cssHeight = Math.max(88, Math.min(maxHeight, cssWidth / Math.max(0.2, worldAspect)));

          if (cssHeight > maxHeight) {
            cssHeight = maxHeight;
            cssWidth = Math.max(132, Math.min(maxWidth, cssHeight * worldAspect));
          }

          minimap.style.width = round2(cssWidth) + "px";
          minimap.style.height = round2(cssHeight) + "px";

          const rect = minimapCanvas.getBoundingClientRect();
          const width = Math.max(1, rect.width || minimapCanvas.clientWidth || cssWidth);
          const height = Math.max(1, rect.height || minimapCanvas.clientHeight || cssHeight);
          const padding = 8;
          const scale = Math.max(
            0.0001,
            Math.min((width - padding * 2) / worldWidth, (height - padding * 2) / worldHeight),
          );
          const deviceScale = getDeviceScale();
          const pixelWidth = Math.max(1, Math.round(width * deviceScale));
          const pixelHeight = Math.max(1, Math.round(height * deviceScale));

          if (minimapCanvas.width !== pixelWidth || minimapCanvas.height !== pixelHeight) {
            minimapCanvas.width = pixelWidth;
            minimapCanvas.height = pixelHeight;
          }

          const context = minimapCanvas.getContext("2d");
          if (context) {
            context.setTransform(deviceScale, 0, 0, deviceScale, 0, 0);
          }

          return {
            bounds,
            height,
            offsetX: round2((width - worldWidth * scale) / 2),
            offsetY: round2((height - worldHeight * scale) / 2),
            scale,
            width,
          };
        }

        function drawMinimapTables(context, metrics) {
          context.save();
          context.fillStyle = "rgba(182, 231, 217, 0.18)";
          context.strokeStyle = "rgba(182, 231, 217, 0.34)";
          context.lineWidth = 1;

          for (const [modelId, meta] of tableMetaById.entries()) {
            if (!isVisibleModel(modelId)) {
              continue;
            }

            const position = getCurrentPosition(modelId);
            const rect = worldRectToMinimapRect(
              {
                maxX: position.x + meta.width,
                maxY: position.y + meta.height,
                minX: position.x,
                minY: position.y,
              },
              metrics,
            );
            const isSelected = state.selectedModelId === modelId;

            context.fillStyle = isSelected ? "rgba(255, 191, 105, 0.56)" : "rgba(182, 231, 217, 0.22)";
            context.strokeStyle = isSelected ? "rgba(255, 191, 105, 0.88)" : "rgba(182, 231, 217, 0.38)";
            context.fillRect(rect.x, rect.y, rect.width, rect.height);
            context.strokeRect(rect.x, rect.y, rect.width, rect.height);
          }

          context.restore();
        }

        function updateMinimapViewportCursor(metrics) {
          const minimapRect = fitMinimapCursorRect(
            worldRectToMinimapRect(getViewportWorldRect(), metrics),
            metrics,
          );

          minimapViewport.style.transform =
            "translate(" + round2(minimapRect.x) + "px, " + round2(minimapRect.y) + "px)";
          minimapViewport.style.width = round2(minimapRect.width) + "px";
          minimapViewport.style.height = round2(minimapRect.height) + "px";
        }

        function getViewportWorldRect() {
          const rect = getViewportScreenRect();
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);

          return {
            maxX: (rect.width - state.viewport.panX) / zoom,
            maxY: (rect.height - state.viewport.panY) / zoom,
            minX: -state.viewport.panX / zoom,
            minY: -state.viewport.panY / zoom,
          };
        }

        function fitMinimapCursorRect(rect, metrics) {
          const width = Math.min(metrics.width, Math.max(10, rect.width));
          const height = Math.min(metrics.height, Math.max(10, rect.height));
          const x = rect.x - Math.max(0, width - rect.width) / 2;
          const y = rect.y - Math.max(0, height - rect.height) / 2;

          return {
            height,
            width,
            x: Math.max(0, Math.min(Math.max(0, metrics.width - width), x)),
            y: Math.max(0, Math.min(Math.max(0, metrics.height - height), y)),
          };
        }

        function worldRectToMinimapRect(rect, metrics) {
          return {
            height: Math.max(2, (rect.maxY - rect.minY) * metrics.scale),
            width: Math.max(2, (rect.maxX - rect.minX) * metrics.scale),
            x: metrics.offsetX + (rect.minX - metrics.bounds.minX) * metrics.scale,
            y: metrics.offsetY + (rect.minY - metrics.bounds.minY) * metrics.scale,
          };
        }

        function getMinimapWorldPoint(event) {
          const metrics = cachedMinimapMetrics || createMinimapMetrics(computeLayoutBounds(state.layoutMode, state.tableOptions));
          if (!metrics || !minimapCanvas) {
            return undefined;
          }

          const rect = minimapCanvas.getBoundingClientRect();
          const localX = event.clientX - rect.left;
          const localY = event.clientY - rect.top;
          const worldX = metrics.bounds.minX + (localX - metrics.offsetX) / metrics.scale;
          const worldY = metrics.bounds.minY + (localY - metrics.offsetY) / metrics.scale;

          return {
            x: round2(Math.max(metrics.bounds.minX, Math.min(metrics.bounds.maxX, worldX))),
            y: round2(Math.max(metrics.bounds.minY, Math.min(metrics.bounds.maxY, worldY))),
          };
        }

        function isVisibleModel(modelId) {
          return !getTableOptions(state, modelId).hidden;
        }

        function renderHiddenTableList() {
          if (!hiddenModelList) {
            return;
          }

          hiddenModelList.innerHTML = renderHiddenModelItemsMarkup();
        }

        function renderOverlays() {
          renderedOverlays = [];

          for (const meta of overlayMeta) {
            const sourceTable = tableMetaById.get(meta.sourceModelId);
            const targetTable = tableMetaById.get(meta.targetModelId);
            if (!sourceTable || !targetTable) {
              continue;
            }

            const sourcePosition = getCurrentPosition(meta.sourceModelId);
            const targetPosition = getCurrentPosition(meta.targetModelId);
            const sourceCenter = getCenter(sourcePosition, sourceTable);
            const targetCenter = getCenter(targetPosition, targetTable);
            const active =
              state.selectedMethodContext &&
              state.selectedMethodContext.modelId === meta.sourceModelId &&
              state.selectedMethodContext.methodName === meta.methodName &&
              getTableOptions(state, meta.sourceModelId).showMethodHighlights &&
              isVisibleModel(meta.sourceModelId) &&
              isVisibleModel(meta.targetModelId);

            renderedOverlays.push({
              active: Boolean(active),
              id: meta.id,
              methodName: meta.methodName,
              sourceModelId: meta.sourceModelId,
              targetModelId: meta.targetModelId,
              x1: sourceCenter.x,
              x2: targetCenter.x,
              y1: sourceCenter.y,
              y2: targetCenter.y,
            });
          }
        }

        function renderPanels() {
          if (!panelHost) {
            return;
          }

          panelHost.innerHTML = renderInspectorPanelMarkup(getSelectedPanelModelId());
          syncPanelMeta();
        }

        function renderSummary() {
          for (const element of layoutReadouts) {
            element.textContent = getLayoutLabel(state.layoutMode);
          }

          for (const element of hiddenCountReadouts) {
            element.textContent = String(
              state.tableOptions.filter((options) => options.hidden).length,
            );
          }

          for (const button of layoutButtons) {
            button.classList.toggle(
              "is-active",
              normalizeLayoutModeId(button.dataset.layoutMode) === normalizeLayoutModeId(state.layoutMode),
            );
          }
        }

        function renderSetupControls() {
          for (const control of setupControls) {
            const key = control.dataset.setupControl;
            if (!key) {
              continue;
            }

            const nextValue = getInteractionSetting(state, key);
            if (Number(control.value) !== nextValue) {
              control.value = String(nextValue);
            }
          }

          for (const element of setupValueReadouts) {
            const key = element.dataset.setupValue;
            if (!key) {
              continue;
            }

            element.textContent = formatInteractionSettingValue(key);
          }
        }

        function isMethodTarget(modelId) {
          return renderedOverlays.some((overlay) => overlay.active && overlay.targetModelId === modelId);
        }

        function updateToggleButton(button, options) {
          const toggle = button.dataset.tableToggle;
          const label = button.children[0];
          const status = button.querySelector("[data-control-status]");
          let active = false;
          let labelText = label ? label.textContent || "" : "";
          let statusText = "Off";

          switch (toggle) {
            case "hidden":
              active = !options.hidden;
              labelText = options.hidden ? "Show Table" : "Hide Table";
              statusText = options.hidden ? "Hidden" : "Visible";
              break;
            case "showMethods":
              active = options.showMethods;
              statusText = options.showMethods ? "On" : "Off";
              break;
            case "showProperties":
              active = options.showProperties;
              statusText = options.showProperties ? "On" : "Off";
              break;
            case "showMethodHighlights":
              active = options.showMethodHighlights;
              statusText = options.showMethodHighlights ? "On" : "Off";
              break;
          }

          button.classList.toggle("is-active", active);
          if (label) {
            label.textContent = labelText;
          }
          if (status) {
            status.textContent = statusText;
          }
        }
  `;
}
