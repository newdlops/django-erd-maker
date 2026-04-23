export function getBrowserRenderSource(): string {
  return `
        const MAX_RUNTIME_CROSSING_EDGE_COUNT = 120;
        let viewportRenderFrame = 0;
        let dragPreviewFrame = 0;
        let cachedMinimapMetrics = null;
        let pendingDragPreviewRects = [];
        const overlayMetaByMethodKey = new Map();

        for (const meta of overlayMeta) {
          const key = getMethodContextKey({
            methodName: meta.methodName,
            modelId: meta.sourceModelId,
          });
          const bucket = overlayMetaByMethodKey.get(key);
          if (bucket) {
            bucket.push(meta);
            continue;
          }

          overlayMetaByMethodKey.set(key, [meta]);
        }

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

        function applySelectionState(previousState, action) {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          renderTablesForModelIds(collectSelectionAffectedModelIds(previousState, action));
          renderSelectionOverlays(previousState);
          renderPanelsForModelIds(collectSelectionAffectedPanelIds(previousState, action));
          renderMethodButtonsForContexts([
            previousState.selectedMethodContext,
            state.selectedMethodContext,
          ]);

          if (selectionStateChanged(previousState)) {
            redrawCanvasRegions(collectSelectionDirtyScreenRects(previousState, action));
          }

          if (previousState.selectedModelId !== state.selectedModelId) {
            renderMinimap("selection");
          }
        }

        function applyManualPositionState(modelId) {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          renderTablesForModelIds([modelId]);
          rerouteModelEdges(modelId);
          renderOverlays();
          drawCanvas("full");
          renderMinimap("full");
        }

        function applyDragPreviewState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          if (pendingDragPreviewRects.length > 0) {
            redrawCanvasRegions(pendingDragPreviewRects, {
              skipCrossings: true,
              skipMethodOverlays: true,
            });
            pendingDragPreviewRects = [];
            return;
          }

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
          pendingDragPreviewRects = [];
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
          const previousState = state;
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
            queueDragPreviewRects(previousState, action.modelId);
            cancelViewportRender();
            scheduleDragPreviewRender();
            return;
          }

          if (action.type === "set-table-manual-position") {
            if (renderModel.modelCatalogMode) {
              invalidateCatalogSceneCache();
              cancelDragPreviewRender();
              cancelViewportRender();
              applyGeometryState();
              return;
            }
            cancelDragPreviewRender();
            cancelViewportRender();
            applyManualPositionState(action.modelId);
            return;
          }

          if (action.type === "set-interaction-setting") {
            renderSetupControls();
            renderRefreshButtons();
            return;
          }

          if (isSelectionOnlyAction(action)) {
            cancelDragPreviewRender();
            cancelViewportRender();
            applySelectionState(previousState, action);
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

        function isSelectionOnlyAction(action) {
          switch (action.type) {
            case "select-model":
            case "toggle-method":
            case "set-table-show-method-highlights":
            case "set-table-show-methods":
            case "set-table-show-properties":
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

          let metrics = renderMode === "full" ? null : cachedMinimapMetrics;
          if (!metrics) {
            const bounds = computeLayoutBounds(
              state.layoutMode,
              state.tableOptions,
              getAppliedLayoutSettings(),
            );
            metrics = createMinimapMetrics(bounds);
          }

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

          if (renderMode !== "viewport") {
            context.clearRect(0, 0, metrics.width, metrics.height);
            drawMinimapTables(context, metrics);
          }

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

          const metrics =
            cachedMinimapMetrics ||
            createMinimapMetrics(
              computeLayoutBounds(
                state.layoutMode,
                state.tableOptions,
                getAppliedLayoutSettings(),
              ),
            );
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

        function getPositionForRenderState(targetState, modelId) {
          const options = getTableOptions(targetState, modelId);
          return options.manualPosition || getBasePosition(modelId);
        }

        function getMethodContextKey(context) {
          return context && context.modelId && context.methodName
            ? context.modelId + "::" + context.methodName
            : "";
        }

        function getMethodButtonKey(modelId, methodName) {
          return modelId + "::" + methodName;
        }

        function getOverlayMetaKey(meta) {
          return meta.sourceModelId + "::" + meta.methodName + "::" + meta.targetModelId;
        }

        function selectionStateChanged(previousState) {
          return (
            previousState.selectedModelId !== state.selectedModelId ||
            getMethodContextKey(previousState.selectedMethodContext) !==
              getMethodContextKey(state.selectedMethodContext)
          );
        }

        function queueDragPreviewRects(previousState, modelId) {
          const dirtyRects = [
            getModelScreenRect(modelId, 72, previousState),
            getModelScreenRect(modelId, 72, state),
          ].filter(Boolean);

          if (dirtyRects.length === 0) {
            return;
          }

          pendingDragPreviewRects = mergeScreenRects(
            pendingDragPreviewRects.concat(dirtyRects),
          );
        }

        function collectSelectionAffectedModelIds(previousState, action) {
          const modelIds = new Set([
            action.modelId,
            previousState.selectedModelId,
            state.selectedModelId,
          ]);

          for (const modelId of collectMethodTargetModelIdsForState(previousState)) {
            modelIds.add(modelId);
          }

          for (const modelId of collectMethodTargetModelIdsForState(state)) {
            modelIds.add(modelId);
          }

          return Array.from(modelIds).filter(Boolean);
        }

        function collectSelectionAffectedPanelIds(previousState, action) {
          return Array.from(new Set([
            action.modelId,
            previousState.selectedModelId,
            state.selectedModelId,
          ])).filter(Boolean);
        }

        function collectSelectionDirtyScreenRects(previousState, action) {
          const dirtyRects = [];

          for (const modelId of collectSelectionAffectedModelIds(previousState, action)) {
            const previousRect = getModelScreenRect(modelId, 72, previousState);
            const nextRect = getModelScreenRect(modelId, 72, state);
            if (previousRect) {
              dirtyRects.push(previousRect);
            }
            if (nextRect) {
              dirtyRects.push(nextRect);
            }
          }

          for (const overlay of collectActiveOverlayEntriesForState(previousState)) {
            const rect = getOverlayScreenRect(overlay, 28);
            if (rect) {
              dirtyRects.push(rect);
            }
          }

          for (const overlay of collectActiveOverlayEntriesForState(state)) {
            const rect = getOverlayScreenRect(overlay, 28);
            if (rect) {
              dirtyRects.push(rect);
            }
          }

          return dirtyRects;
        }

        function isOverlayActiveForState(meta, targetState) {
          return Boolean(
            targetState.selectedMethodContext &&
            targetState.selectedMethodContext.modelId === meta.sourceModelId &&
            targetState.selectedMethodContext.methodName === meta.methodName &&
            getTableOptions(targetState, meta.sourceModelId).showMethodHighlights &&
            !getTableOptions(targetState, meta.sourceModelId).hidden &&
            !getTableOptions(targetState, meta.targetModelId).hidden,
          );
        }

        function createOverlayRenderState(meta, targetState) {
          const sourceTable = tableMetaById.get(meta.sourceModelId);
          const targetTable = tableMetaById.get(meta.targetModelId);
          if (!sourceTable || !targetTable) {
            return undefined;
          }

          const sourcePosition = getPositionForRenderState(targetState, meta.sourceModelId);
          const targetPosition = getPositionForRenderState(targetState, meta.targetModelId);
          const sourceCenter = getCenter(sourcePosition, sourceTable);
          const targetCenter = getCenter(targetPosition, targetTable);

          return {
            active: isOverlayActiveForState(meta, targetState),
            key: getOverlayMetaKey(meta),
            sourceModelId: meta.sourceModelId,
            targetModelId: meta.targetModelId,
            x1: sourceCenter.x,
            x2: targetCenter.x,
            y1: sourceCenter.y,
            y2: targetCenter.y,
          };
        }

        function collectActiveOverlayEntriesForState(targetState) {
          const methodKey = getMethodContextKey(targetState.selectedMethodContext);
          if (!methodKey) {
            return [];
          }

          return (overlayMetaByMethodKey.get(methodKey) || [])
            .map((meta) => createOverlayRenderState(meta, targetState))
            .filter((overlay) => overlay && overlay.active);
        }

        function collectMethodTargetModelIdsForState(targetState) {
          return collectActiveOverlayEntriesForState(targetState).map((overlay) => overlay.targetModelId);
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

        function collectVisibleEdgeEntries() {
          const visibleEdgeEntries = [];
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

          return visibleEdgeEntries;
        }

        function parseEdgePointsAttribute(pointsAttribute) {
          return String(pointsAttribute || "")
            .trim()
            .split(/\s+/)
            .map((pair) => {
              const [x, y] = pair.split(",");
              const point = {
                x: Number(x),
                y: Number(y),
              };

              return Number.isFinite(point.x) && Number.isFinite(point.y)
                ? point
                : undefined;
            })
            .filter(Boolean);
        }

        function collectPrecomputedVisibleRoutes(edgeEntries) {
          const routes = [];

          for (const entry of edgeEntries) {
            if (
              getTableOptions(state, entry.meta.sourceModelId).manualPosition ||
              getTableOptions(state, entry.meta.targetModelId).manualPosition
            ) {
              return undefined;
            }

            const points = parseEdgePointsAttribute(entry.meta.element.dataset.points);
            if (points.length < 2) {
              return undefined;
            }

            routes.push({
              entry,
              points,
            });
          }

          return routes;
        }

        function updateRenderedEdge(route) {
          const pointsAttribute = pointsToAttribute(route.points);
          route.entry.meta.element.setAttribute("points", pointsAttribute);
          route.entry.meta.element.dataset.points = pointsAttribute;

          const existingIndex = renderedEdges.findIndex((candidate) => candidate.edgeId === route.entry.meta.edgeId);
          const nextEdge = {
            edgeId: route.entry.meta.edgeId,
            meta: route.entry.meta,
            points: route.points,
          };

          if (existingIndex >= 0) {
            renderedEdges[existingIndex] = nextEdge;
            return;
          }

          renderedEdges.push(nextEdge);
        }

        function renderEdgesAndCrossings() {
          const visibleEdgeEntries = collectVisibleEdgeEntries();
          renderedEdges = [];

          if (renderModel.modelCatalogMode) {
            for (const routed of routeCatalogEdgesWithPorts(visibleEdgeEntries)) {
              updateRenderedEdge(routed);
            }

            renderedCrossings = [];
            if (crossingsLayer) {
              crossingsLayer.innerHTML = "";
            }
            return;
          }

          const precomputedRoutes = collectPrecomputedVisibleRoutes(visibleEdgeEntries);
          if (precomputedRoutes) {
            for (const routed of precomputedRoutes) {
              updateRenderedEdge(routed);
            }

            recomputeRenderedCrossings();
            return;
          }

          for (const routed of routeVisibleEdgesWithPorts(visibleEdgeEntries)) {
            updateRenderedEdge(routed);
          }

          recomputeRenderedCrossings();
        }

        function routeDirtyModelEdgesWithPorts(edgeEntries, preservedSegments) {
          const endpointRefsByKey = new Map();
          const routes = [];

          edgeEntries.forEach((entry, edgeIndex) => {
            const sourceCenter = getCenter(entry.sourcePosition, entry.sourceTable);
            const targetCenter = getCenter(entry.targetPosition, entry.targetTable);
            const sourceSide = getPreferredConnectionSide(sourceCenter, targetCenter);
            const targetSide = getPreferredConnectionSide(targetCenter, sourceCenter);
            const sourceRef = {
              edgeIndex,
              endpoint: "source",
              peerCenter: targetCenter,
              side: sourceSide,
            };
            const targetRef = {
              edgeIndex,
              endpoint: "target",
              peerCenter: sourceCenter,
              side: targetSide,
            };

            addCatalogEndpointRef(endpointRefsByKey, entry.meta.sourceModelId, sourceSide, sourceRef);
            addCatalogEndpointRef(endpointRefsByKey, entry.meta.targetModelId, targetSide, targetRef);
            routes.push({
              entry,
              sourceRef,
              sourceSide,
              targetRef,
              targetSide,
            });
          });

          for (const refs of endpointRefsByKey.values()) {
            refs.sort(compareCatalogEndpointRefs);
            refs.forEach((ref, index) => {
              ref.portIndex = index;
              ref.portCount = refs.length;
            });
          }

          const occupiedRects = collectVisibleRoutingRects(edgeEntries);
          const routedSegments = preservedSegments.slice();

          return routes
            .slice()
            .sort(compareVisibleRoutesForRouting)
            .map((route) => {
              const start = getCatalogPortPoint(
                route.entry.sourcePosition,
                route.entry.sourceTable,
                route.sourceSide,
                route.sourceRef.portIndex || 0,
                route.sourceRef.portCount || 1,
              );
              const end = getCatalogPortPoint(
                route.entry.targetPosition,
                route.entry.targetTable,
                route.targetSide,
                route.targetRef.portIndex || 0,
                route.targetRef.portCount || 1,
              );
              const points = buildObstacleAwarePathFromPorts(
                start,
                route.sourceSide,
                end,
                route.targetSide,
                undefined,
                route.entry.meta,
                occupiedRects.filter((rect) =>
                  rect.modelId !== route.entry.meta.sourceModelId &&
                  rect.modelId !== route.entry.meta.targetModelId,
                ),
                routedSegments,
              );

              routedSegments.push(...findSegments(points));
              return {
                entry: route.entry,
                points,
              };
            });
        }

        function rerouteModelEdges(modelId) {
          if (!modelId || renderModel.modelCatalogMode) {
            renderEdgesAndCrossings();
            return;
          }

          const visibleEdgeEntries = collectVisibleEdgeEntries();
          const dirtyEntries = visibleEdgeEntries.filter((entry) =>
            entry.meta.sourceModelId === modelId || entry.meta.targetModelId === modelId,
          );

          if (dirtyEntries.length === 0) {
            renderedEdges = renderedEdges.filter((edge) => edge.meta.sourceModelId !== modelId && edge.meta.targetModelId !== modelId);
            recomputeRenderedCrossings();
            return;
          }

          const dirtyEdgeIds = new Set(dirtyEntries.map((entry) => entry.meta.edgeId));
          const preservedEdges = renderedEdges.filter((edge) => !dirtyEdgeIds.has(edge.edgeId));
          const preservedSegments = preservedEdges.flatMap((edge) => findSegments(edge.points));

          renderedEdges = preservedEdges.slice();
          for (const routed of routeDirtyModelEdgesWithPorts(dirtyEntries, preservedSegments)) {
            updateRenderedEdge(routed);
          }

          recomputeRenderedCrossings();
        }

        function recomputeRenderedCrossings() {
          if (!crossingsLayer) {
            renderedCrossings = [];
            return;
          }

          const crossings = [];
          let crossingIndex = 1;
          const visibleEdges = renderedEdges.slice();

          if (visibleEdges.length > MAX_RUNTIME_CROSSING_EDGE_COUNT) {
            renderedCrossings = [];
            crossingsLayer.innerHTML = "";
            return;
          }

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

          renderedCrossings = filterNearbyCrossings(crossings);
          crossingsLayer.innerHTML = renderedCrossings.map(renderCrossingMarkup).join("");
        }

        function filterNearbyCrossings(crossings) {
          if (!Array.isArray(crossings) || crossings.length <= 1) {
            return crossings || [];
          }

          const minDistance = Math.max(14, 20 * getAppliedLayoutSettings().edgeDetour);
          const kept = [];

          crossings
            .slice()
            .sort((left, right) =>
              left.position.x - right.position.x ||
              left.position.y - right.position.y ||
              left.id.localeCompare(right.id),
            )
            .forEach((crossing) => {
              const tooClose = kept.some((candidate) =>
                Math.hypot(
                  candidate.position.x - crossing.position.x,
                  candidate.position.y - crossing.position.y,
                ) < minDistance,
              );

              if (!tooClose) {
                kept.push(crossing);
              }
            });

          return kept.map((crossing, index) => ({
            ...crossing,
            id: "runtime-crossing-" + (index + 1),
          }));
        }

        function renderHiddenTableList() {
          for (const [modelId, item] of hiddenItemsById.entries()) {
            item.toggleAttribute("hidden", !getTableOptions(state, modelId).hidden);
          }
        }

        function renderOverlay(meta) {
          const overlay = createOverlayRenderState(meta, state);
          if (!overlay) {
            return undefined;
          }

          meta.element.dataset.x1 = String(overlay.x1);
          meta.element.dataset.y1 = String(overlay.y1);
          meta.element.dataset.x2 = String(overlay.x2);
          meta.element.dataset.y2 = String(overlay.y2);
          meta.element.classList.toggle("is-active", overlay.active);
          meta.element.toggleAttribute("hidden", !overlay.active);

          return overlay;
        }

        function upsertRenderedOverlay(overlay) {
          if (!overlay) {
            return;
          }

          const existingIndex = renderedOverlays.findIndex((candidate) => candidate.key === overlay.key);
          if (!overlay.active) {
            if (existingIndex >= 0) {
              renderedOverlays.splice(existingIndex, 1);
            }
            return;
          }

          if (existingIndex >= 0) {
            renderedOverlays[existingIndex] = overlay;
            return;
          }

          renderedOverlays.push(overlay);
        }

        function renderSelectionOverlays(previousState) {
          const methodKeys = new Set([
            getMethodContextKey(previousState.selectedMethodContext),
            getMethodContextKey(state.selectedMethodContext),
          ].filter(Boolean));

          for (const methodKey of methodKeys) {
            for (const meta of overlayMetaByMethodKey.get(methodKey) || []) {
              upsertRenderedOverlay(renderOverlay(meta));
            }
          }

          const affectedTargetModelIds = new Set([
            ...collectMethodTargetModelIdsForState(previousState),
            ...collectMethodTargetModelIdsForState(state),
          ]);

          for (const modelId of affectedTargetModelIds) {
            const meta = tableMetaById.get(modelId);
            if (!meta) {
              continue;
            }

            meta.element.classList.toggle("is-method-target", isMethodTarget(modelId));
          }
        }

        function renderOverlays() {
          renderedOverlays = [];
          const methodTargetModelIds = new Set();

          for (const meta of overlayMeta) {
            const overlay = renderOverlay(meta);
            if (!overlay || !overlay.active) {
              continue;
            }

            renderedOverlays.push(overlay);
            methodTargetModelIds.add(overlay.targetModelId);
          }

          for (const [modelId, meta] of tableMetaById.entries()) {
            meta.element.classList.toggle("is-method-target", methodTargetModelIds.has(modelId));
          }
        }

        function syncMethodButtonCollection() {
          methodButtons = Array.from(document.querySelectorAll("[data-method-button]"));
        }

        function hasRenderablePanelDetails(table) {
          return Boolean(
            table &&
            (
              (Array.isArray(table.fieldRows) && table.fieldRows.length > 0) ||
              (Array.isArray(table.properties) && table.properties.length > 0) ||
              (Array.isArray(table.methods) && table.methods.length > 0)
            )
          );
        }

        function computePanelDetailRenderKey(table, options) {
          return [
            Array.isArray(table.fieldRows) ? table.fieldRows.length : 0,
            Array.isArray(table.methods) ? table.methods.length : 0,
            Array.isArray(table.properties) ? table.properties.length : 0,
            options.showMethods ? 1 : 0,
            options.showProperties ? 1 : 0,
          ].join(":");
        }

        function ensurePanelDetailBody(modelId, meta, options) {
          const table = tableRenderById.get(modelId);
          if (!meta.detailBody || !table || !hasRenderablePanelDetails(table)) {
            return meta;
          }

          const nextRenderKey = computePanelDetailRenderKey(table, options);
          if (
            meta.detailBody.dataset.panelRenderKey === nextRenderKey &&
            meta.detailBody.childElementCount > 0
          ) {
            return meta;
          }

          meta.detailBody.replaceChildren(createPanelDetailFragment(table, options));
          meta.detailBody.dataset.panelRenderKey = nextRenderKey;
          const refreshedMeta = readPanelMeta(meta.element);
          panelMetaById.set(modelId, refreshedMeta);
          syncMethodButtonCollection();
          return refreshedMeta;
        }

        function createPanelDetailFragment(table, options) {
          const fragment = document.createDocumentFragment();
          fragment.appendChild(createMethodsPanelSection(table, options));
          fragment.appendChild(createPropertiesPanelSection(table, options));
          fragment.appendChild(createFieldSummaryPanelSection(table));
          return fragment;
        }

        function createMethodsPanelSection(table, options) {
          const section = createPanelSection("Methods");
          const methods = Array.isArray(table.methods) ? table.methods : [];
          section.appendChild(
            createHintParagraph(
              "Methods are hidden by the current table view state.",
              "method-hidden-hint",
              options.showMethods,
            ),
          );
          section.appendChild(
            createHintParagraph(
              "No user-defined methods.",
              "empty-method-hint",
              methods.length > 0,
            ),
          );

          const methodList = document.createElement("div");
          methodList.className = "erd-method-buttons";
          methodList.dataset.methodList = "";
          methodList.toggleAttribute("hidden", !options.showMethods || methods.length === 0);

          for (const method of methods) {
            methodList.appendChild(createMethodCard(table, method));
          }

          section.appendChild(methodList);
          return section;
        }

        function createPropertiesPanelSection(table, options) {
          const section = createPanelSection("Properties");
          const properties = Array.isArray(table.properties) ? table.properties : [];
          section.appendChild(
            createHintParagraph(
              "Properties are hidden by the current table view state.",
              "property-hidden-hint",
              options.showProperties,
            ),
          );
          section.appendChild(
            createHintParagraph(
              "No computed properties.",
              "empty-property-hint",
              properties.length > 0,
            ),
          );

          const propertyList = document.createElement("ul");
          propertyList.className = "erd-list";
          propertyList.dataset.propertyList = "";
          propertyList.toggleAttribute("hidden", !options.showProperties || properties.length === 0);

          for (const property of properties) {
            const item = document.createElement("li");
            item.className = "erd-list__item";
            const label = document.createElement("span");
            label.textContent = "@ " + String(property);
            item.appendChild(label);
            propertyList.appendChild(item);
          }

          section.appendChild(propertyList);
          return section;
        }

        function createFieldSummaryPanelSection(table) {
          const section = createPanelSection("Field Summary");
          const fieldList = document.createElement("ul");
          fieldList.className = "erd-list";

          for (const row of Array.isArray(table.fieldRows) ? table.fieldRows : []) {
            const item = document.createElement("li");
            item.className = "erd-list__item erd-list__item--" + String(row.tone || "field");
            const label = document.createElement("span");
            label.textContent = String(row.text || "");
            item.appendChild(label);
            fieldList.appendChild(item);
          }

          section.appendChild(fieldList);
          return section;
        }

        function createPanelSection(title) {
          const section = document.createElement("div");
          section.className = "erd-panel__section";
          const heading = document.createElement("h3");
          heading.textContent = title;
          section.appendChild(heading);
          return section;
        }

        function createHintParagraph(text, datasetKey, hidden) {
          const hint = document.createElement("p");
          hint.className = "erd-panel__hint";
          hint.setAttribute("data-" + datasetKey, "");
          hint.textContent = text;
          hint.toggleAttribute("hidden", Boolean(hidden));
          return hint;
        }

        function createMethodCard(table, method) {
          const article = document.createElement("article");
          article.className = "erd-method-card";

          const button = document.createElement("button");
          button.type = "button";
          button.className = "erd-method-button";
          button.dataset.methodButton = "";
          button.dataset.methodName = String(method.name || "");
          button.dataset.modelId = String(table.modelId || "");

          const label = document.createElement("span");
          label.textContent = "fn " + String(method.name || "");
          const count = document.createElement("span");
          const relatedModels = Array.isArray(method.relatedModels) ? method.relatedModels : [];
          count.textContent = String(relatedModels.length) + " links";
          button.appendChild(label);
          button.appendChild(count);
          article.appendChild(button);
          article.appendChild(createMethodRelationsNode(method));
          return article;
        }

        function createMethodRelationsNode(method) {
          const relatedModels = Array.isArray(method.relatedModels) ? method.relatedModels : [];
          if (relatedModels.length === 0) {
            const hint = document.createElement("p");
            hint.className = "erd-panel__hint";
            hint.textContent = "No related models inferred for this method.";
            return hint;
          }

          const container = document.createElement("div");
          container.className = "erd-method-links";
          for (const reference of relatedModels) {
            const chip = document.createElement("span");
            chip.className = "erd-relation-chip erd-relation-chip--" + String(reference.confidence || "low");
            chip.textContent = formatMethodRelationLabel(reference);
            container.appendChild(chip);
          }
          return container;
        }

        function formatMethodRelationLabel(reference) {
          if (reference && reference.targetModelId) {
            return String(reference.targetModelId);
          }

          if (reference && reference.rawReference) {
            return String(reference.rawReference) + " (unresolved)";
          }

          return "unresolved model";
        }

        function renderPanel(modelId, meta) {
          const selected = state.selectedModelId === modelId;
          const options = getTableOptions(state, modelId);
          const table = tableRenderById.get(modelId);

          meta.element.classList.toggle("is-selected", selected);
          meta.element.toggleAttribute("hidden", !selected);
          if (selected && table && hasRenderablePanelDetails(table)) {
            meta = ensurePanelDetailBody(modelId, meta, options);
          }
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

        function renderPanels() {
          for (const [modelId, meta] of panelMetaById.entries()) {
            renderPanel(modelId, meta);
          }

          renderMethodButtons();
        }

        function renderPanelsForModelIds(modelIds) {
          const seen = new Set();

          for (const modelId of modelIds) {
            if (!modelId || seen.has(modelId)) {
              continue;
            }

            const meta = panelMetaById.get(modelId);
            if (!meta) {
              continue;
            }

            seen.add(modelId);
            renderPanel(modelId, meta);
          }
        }

        function renderMethodButtons() {
          syncMethodButtonCollection();
          for (const button of methodButtons) {
            updateMethodButtonState(button);
          }
        }

        function renderMethodButtonsForContexts(contexts) {
          const seen = new Set();

          for (const context of Array.isArray(contexts) ? contexts : []) {
            const methodKey = getMethodContextKey(context);
            if (!methodKey || seen.has(methodKey)) {
              continue;
            }

            seen.add(methodKey);
            syncMethodButtonCollection();
            for (const button of methodButtons.filter((candidate) =>
              getMethodButtonKey(candidate.dataset.modelId || "", candidate.dataset.methodName || "") === methodKey
            )) {
              updateMethodButtonState(button);
            }
          }
        }

        function updateMethodButtonState(button) {
          const active =
            state.selectedMethodContext &&
            state.selectedMethodContext.modelId === button.dataset.modelId &&
            state.selectedMethodContext.methodName === button.dataset.methodName;
          button.classList.toggle("is-active", Boolean(active));
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
            renderTable(modelId, meta);
          }
        }

        function renderTablesForModelIds(modelIds) {
          const seen = new Set();

          for (const modelId of modelIds) {
            if (!modelId || seen.has(modelId)) {
              continue;
            }

            const meta = tableMetaById.get(modelId);
            if (!meta) {
              continue;
            }

            seen.add(modelId);
            renderTable(modelId, meta);
          }
        }

        function renderTable(modelId, meta) {
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

        function isMethodTarget(modelId) {
          return renderedOverlays.some((overlay) =>
            overlay.active &&
            overlay.targetModelId === modelId,
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
