export function getBrowserRenderSource(): string {
  return `
        const MAX_RUNTIME_CROSSING_EDGE_COUNT = 120;
        let viewportRenderFrame = 0;
        let dragPreviewFrame = 0;
        let cachedMinimapMetrics = null;

        function applyState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          renderSummary();
          renderSetupControls();
          renderRefreshButtons();
          renderTables();
          renderEdgesAndCrossings();
          renderOverlays();
          renderPanels();
          renderHiddenTableList();
          drawCanvas("full");
          renderMinimap("full");
        }

        function applyViewportState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          drawCanvas("viewport");
          renderMinimap("viewport");
        }

        function applyGeometryState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          renderTables();
          renderEdgesAndCrossings();
          renderOverlays();
          drawCanvas("full");
          renderMinimap("full");
        }

        function applyDragPreviewState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          drawCanvas("drag-preview");
        }

        function cancelViewportRender() {
          if (!viewportRenderFrame) {
            return;
          }

          window.cancelAnimationFrame(viewportRenderFrame);
          viewportRenderFrame = 0;
        }

        function cancelDragPreviewRender() {
          if (!dragPreviewFrame) {
            return;
          }

          window.cancelAnimationFrame(dragPreviewFrame);
          dragPreviewFrame = 0;
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

        function scheduleDragPreviewRender() {
          if (dragPreviewFrame) {
            return;
          }

          dragPreviewFrame = window.requestAnimationFrame(() => {
            dragPreviewFrame = 0;
            applyDragPreviewState();
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
            action.type === "set-table-manual-position" &&
            drag &&
            drag.kind === "table"
          ) {
            cancelViewportRender();
            scheduleDragPreviewRender();
            return;
          }

          if (action.type === "set-table-manual-position") {
            if (renderModel.modelCatalogMode) {
              invalidateCatalogSceneCache();
            }
            cancelDragPreviewRender();
            cancelViewportRender();
            applyGeometryState();
            return;
          }

          if (action.type === "set-interaction-setting") {
            renderSetupControls();
            renderRefreshButtons();
            return;
          }

          if (
            renderModel.modelCatalogMode &&
            isCatalogSceneAction(action)
          ) {
            invalidateCatalogSceneCache();
          }

          cancelDragPreviewRender();
          cancelViewportRender();
          applyState();
        }

        function isCatalogSceneAction(action) {
          switch (action.type) {
            case "reset-view":
            case "set-layout-mode":
            case "set-table-hidden":
            case "set-table-manual-position":
              return true;
            default:
              return false;
          }
        }

        function renderMinimap(renderMode) {
          if (!minimap || !minimapCanvas || !minimapViewport) {
            return;
          }

          if (renderMode === "viewport" && cachedMinimapMetrics) {
            updateMinimapViewportCursor(cachedMinimapMetrics);
            return;
          }

          const bounds = computeLayoutBounds(
            state.layoutMode,
            state.tableOptions,
            getAppliedLayoutSettings(),
          );
          const metrics = createMinimapMetrics(bounds);
          if (!metrics) {
            cachedMinimapMetrics = null;
            minimap.toggleAttribute("hidden", true);
            return;
          }

          cachedMinimapMetrics = metrics;
          minimap.toggleAttribute("hidden", false);
          const context = minimapCanvas.getContext("2d");
          if (!context) {
            return;
          }

          context.clearRect(0, 0, metrics.width, metrics.height);
          drawMinimapTables(context, metrics);
          updateMinimapViewportCursor(metrics);
        }

        function createMinimapMetrics(bounds) {
          if (!bounds || !hasFiniteLayoutBounds(bounds) || !minimapCanvas) {
            return undefined;
          }

          const rect = minimapCanvas.getBoundingClientRect();
          const width = Math.max(1, rect.width || minimapCanvas.clientWidth || 1);
          const height = Math.max(1, rect.height || minimapCanvas.clientHeight || 1);
          const worldWidth = Math.max(1, bounds.maxX - bounds.minX);
          const worldHeight = Math.max(1, bounds.maxY - bounds.minY);
          const padding = 10;
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

            context.fillStyle = isSelected
              ? "rgba(255, 191, 105, 0.58)"
              : "rgba(182, 231, 217, 0.22)";
            context.strokeStyle = isSelected
              ? "rgba(255, 191, 105, 0.86)"
              : "rgba(182, 231, 217, 0.38)";
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
          const width = Math.min(metrics.width, Math.max(8, rect.width));
          const height = Math.min(metrics.height, Math.max(8, rect.height));
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
          if (!minimapCanvas) {
            return undefined;
          }

          const bounds = computeLayoutBounds(
            state.layoutMode,
            state.tableOptions,
            getAppliedLayoutSettings(),
          );
          const metrics = createMinimapMetrics(bounds);
          if (!metrics) {
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

        function renderCrossingMarkup(crossing) {
          return '<div class="erd-crossing erd-crossing--bridge" data-crossing-id="' +
            crossing.id +
            '" data-x="' +
            crossing.position.x +
            '" data-y="' +
            crossing.position.y +
            '"></div>';
        }

        function renderEdgesAndCrossings() {
          const visibleEdges = [];
          const visibleEdgeEntries = [];
          renderedEdges = [];

          for (const meta of edgeMeta) {
            const sourceTable = tableMetaById.get(meta.sourceModelId);
            const targetTable = tableMetaById.get(meta.targetModelId);
            if (!sourceTable || !targetTable) {
              continue;
            }

            const sourceHidden = !isVisibleModel(meta.sourceModelId);
            const targetHidden = !isVisibleModel(meta.targetModelId);
            meta.element.toggleAttribute("hidden", sourceHidden || targetHidden);
            if (sourceHidden || targetHidden) {
              continue;
            }

            visibleEdgeEntries.push({
              meta,
              sourcePosition: getCurrentPosition(meta.sourceModelId),
              sourceTable,
              targetPosition: getCurrentPosition(meta.targetModelId),
              targetTable,
            });
          }

          if (renderModel.modelCatalogMode) {
            for (const routed of routeCatalogEdgesWithPorts(visibleEdgeEntries)) {
              const pointsAttribute = pointsToAttribute(routed.points);
              routed.entry.meta.element.setAttribute("points", pointsAttribute);
              routed.entry.meta.element.dataset.points = pointsAttribute;
              renderedEdges.push({
                edgeId: routed.entry.meta.edgeId,
                meta: routed.entry.meta,
                points: routed.points,
              });
            }

            renderedCrossings = [];
            if (crossingsLayer) {
              crossingsLayer.innerHTML = "";
            }
            return;
          }

          for (const routed of routeVisibleEdgesWithPorts(visibleEdgeEntries)) {
            const points = routed.points;
            const pointsAttribute = pointsToAttribute(points);
            routed.entry.meta.element.setAttribute("points", pointsAttribute);
            routed.entry.meta.element.dataset.points = pointsAttribute;
            visibleEdges.push({
              edgeId: routed.entry.meta.edgeId,
              points,
            });
            renderedEdges.push({
              edgeId: routed.entry.meta.edgeId,
              meta: routed.entry.meta,
              points,
            });
          }

          if (!crossingsLayer) {
            renderedCrossings = [];
            if (crossingsLayer) {
              crossingsLayer.innerHTML = "";
            }
            return;
          }

          if (visibleEdges.length > MAX_RUNTIME_CROSSING_EDGE_COUNT) {
            renderedCrossings = [];
            crossingsLayer.innerHTML = "";
            return;
          }

          const crossings = [];
          let crossingIndex = 1;

          for (let left = 0; left < visibleEdges.length; left += 1) {
            for (let right = left + 1; right < visibleEdges.length; right += 1) {
              for (const leftSegment of findSegments(visibleEdges[left].points)) {
                for (const rightSegment of findSegments(visibleEdges[right].points)) {
                  const intersection = segmentIntersection(leftSegment, rightSegment);
                  if (!intersection) {
                    continue;
                  }

                  if (
                    isPointAtSegmentEndpoint(intersection, leftSegment) ||
                    isPointAtSegmentEndpoint(intersection, rightSegment)
                  ) {
                    continue;
                  }

                  crossings.push({
                    id: "runtime-crossing-" + crossingIndex,
                    position: intersection,
                  });
                  crossingIndex += 1;
                }
              }
            }
          }

          renderedCrossings = crossings;
          crossingsLayer.innerHTML = crossings.map(renderCrossingMarkup).join("");
        }

        function renderHiddenTableList() {
          for (const [modelId, item] of hiddenItemsById.entries()) {
            item.toggleAttribute("hidden", !getTableOptions(state, modelId).hidden);
          }
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

            meta.element.dataset.x1 = String(sourceCenter.x);
            meta.element.dataset.y1 = String(sourceCenter.y);
            meta.element.dataset.x2 = String(targetCenter.x);
            meta.element.dataset.y2 = String(targetCenter.y);
            meta.element.classList.toggle("is-active", Boolean(active));
            meta.element.toggleAttribute("hidden", !active);
            renderedOverlays.push({
              active: Boolean(active),
              x1: sourceCenter.x,
              x2: targetCenter.x,
              y1: sourceCenter.y,
              y2: targetCenter.y,
            });
          }

          for (const [modelId, meta] of tableMetaById.entries()) {
            meta.element.classList.toggle("is-method-target", isMethodTarget(modelId));
          }
        }

        function renderPanels() {
          for (const [modelId, meta] of panelMetaById.entries()) {
            const selected = state.selectedModelId === modelId;
            const options = getTableOptions(state, modelId);

            meta.element.classList.toggle("is-selected", selected);
            meta.element.toggleAttribute("hidden", !selected);
            if (meta.methodHiddenHint) {
              meta.methodHiddenHint.toggleAttribute("hidden", options.showMethods);
            }
            if (meta.methodList) {
              const hasMethods = meta.methodList.children.length > 0;
              meta.methodList.toggleAttribute("hidden", !options.showMethods || !hasMethods);
            }
            if (meta.emptyMethodHint) {
              const hasMethods = meta.methodList && meta.methodList.children.length > 0;
              meta.emptyMethodHint.toggleAttribute("hidden", Boolean(hasMethods));
            }
            if (meta.propertyHiddenHint) {
              meta.propertyHiddenHint.toggleAttribute("hidden", options.showProperties);
            }
            if (meta.propertyList) {
              const hasProperties = meta.propertyList.children.length > 0;
              meta.propertyList.toggleAttribute("hidden", !options.showProperties || !hasProperties);
            }
            if (meta.emptyPropertyHint) {
              const hasProperties = meta.propertyList && meta.propertyList.children.length > 0;
              meta.emptyPropertyHint.toggleAttribute("hidden", Boolean(hasProperties));
            }

            for (const button of meta.toggleButtons) {
              updateToggleButton(button, options);
            }
          }

          for (const button of methodButtons) {
            const active =
              state.selectedMethodContext &&
              state.selectedMethodContext.modelId === button.dataset.modelId &&
              state.selectedMethodContext.methodName === button.dataset.methodName;
            button.classList.toggle("is-active", Boolean(active));
          }
        }

        function renderSummary() {
          for (const element of layoutReadouts) {
            element.textContent = state.layoutMode;
          }

          for (const element of hiddenCountReadouts) {
            element.textContent = String(
              state.tableOptions.filter((options) => options.hidden).length,
            );
          }

          for (const button of layoutButtons) {
            button.classList.toggle("is-active", button.dataset.layoutMode === state.layoutMode);
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

        function renderRefreshButtons() {
          if (typeof panelRefreshButtons === "undefined") {
            return;
          }

          const pendingLayoutSettings = hasPendingLayoutSettings(state);
          for (const button of panelRefreshButtons) {
            button.classList.toggle("is-pending", pendingLayoutSettings);
            button.textContent = pendingLayoutSettings ? "Refresh To Apply" : "Refresh";
            button.title = pendingLayoutSettings
              ? "Layout and routing settings are pending. Refresh to redraw the diagram."
              : "Refresh and redraw the diagram.";
          }
        }

        function renderTables() {
          for (const [modelId, meta] of tableMetaById.entries()) {
            const selected = state.selectedModelId === modelId;
            const options = getTableOptions(state, modelId);
            const position = getCurrentPosition(modelId);
            const isDraggingTable = drag && drag.kind === "table" && drag.modelId === modelId;

            meta.element.classList.toggle("is-selected", selected);
            meta.element.classList.toggle("is-dragging", Boolean(isDraggingTable));
            meta.element.setAttribute(
              "transform",
              "translate(" + position.x + " " + position.y + ")",
            );
            meta.element.dataset.hidden = String(options.hidden);
            meta.element.dataset.methodHighlights = String(options.showMethodHighlights);
            meta.element.dataset.showMethods = String(options.showMethods);
            meta.element.dataset.showProperties = String(options.showProperties);
            meta.element.toggleAttribute("hidden", options.hidden);

            if (meta.methodsSection) {
              meta.methodsSection.toggleAttribute("hidden", !options.showMethods);
            }
            if (meta.propertiesSection) {
              meta.propertiesSection.toggleAttribute("hidden", !options.showProperties);
            }
            if (meta.dividers.methods) {
              meta.dividers.methods.toggleAttribute(
                "hidden",
                !options.showMethods || !meta.methodsSection || meta.methodsSection.children.length === 0,
              );
            }
            if (meta.dividers.properties) {
              meta.dividers.properties.toggleAttribute(
                "hidden",
                !options.showProperties || !meta.propertiesSection || meta.propertiesSection.children.length === 0,
              );
            }
          }
        }

        function isMethodTarget(modelId) {
          return overlayMeta.some((meta) =>
            meta.element.classList.contains("is-active") &&
            meta.targetModelId === modelId,
          );
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
