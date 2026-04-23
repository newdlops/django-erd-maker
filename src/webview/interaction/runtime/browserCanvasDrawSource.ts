export function getBrowserCanvasDrawSource(): string {
  return `
        const CATALOG_BUCKET_SIZE = 768;
        const CATALOG_TILE_SIZE = 1024;
        const CATALOG_TILE_PRELOAD_WORLD_PADDING = 160;
        const CATALOG_TILE_RASTER_BUDGET_MS = 6;
        const CATALOG_TILE_WARMUP_LIMIT = 2;
        const CATALOG_CROSSING_BUCKET_SIZE = 192;
        const CATALOG_CROSSING_CHECK_BUDGET = 60000;
        const CATALOG_CROSSING_MARKER_MIN_ZOOM = 0.1;
        const CATALOG_CROSSING_MAX_MARKERS = 900;
        const CATALOG_CROSSING_MAX_SEGMENTS = 3600;

        let panSnapshotCanvas = null;
        let panSnapshotContext = null;
        let lastDrawViewport = null;
        let catalogSpatialBuckets = null;
        let catalogTableBoundsById = null;
        let catalogTileCache = new Map();
        let catalogRasterQueue = [];
        let catalogQueuedTiles = new Set();
        let catalogRasterFrame = 0;
        let catalogSceneVersion = 1;
        let catalogWarmVersion = 0;
        let latestCatalogCrossings = [];

        function invalidateCatalogSceneCache() {
          catalogSceneVersion += 1;
          catalogSpatialBuckets = null;
          catalogTableBoundsById = null;
          catalogTileCache.clear();
          catalogRasterQueue = [];
          catalogQueuedTiles.clear();
          catalogWarmVersion = 0;

          if (catalogRasterFrame) {
            window.cancelAnimationFrame(catalogRasterFrame);
            catalogRasterFrame = 0;
          }
        }

        function drawCanvas(renderMode) {
          resizeDrawingCanvas();
          const dragPreview = renderMode === "drag-preview" && drag && drag.kind === "table";

          if (renderModel.modelCatalogMode) {
            drawCatalogCanvas(renderMode);
            if (!dragPreview) {
              lastDrawViewport = {
                panX: state.viewport.panX,
                panY: state.viewport.panY,
                zoom: state.viewport.zoom,
              };
            }
            return;
          }

          const viewportRect = getViewportRect();
          const currentViewport = {
            panX: state.viewport.panX,
            panY: state.viewport.panY,
            zoom: state.viewport.zoom,
          };

          if (dragPreview) {
            drawingContext.clearRect(0, 0, viewportRect.width, viewportRect.height);
            drawScene(getVisibleWorldBounds(), null, {
              skipCrossings: true,
              skipMethodOverlays: true,
            });
            return;
          }

          if (renderMode === "viewport" && canReusePanSnapshot(currentViewport, viewportRect)) {
            drawCanvasFromPanSnapshot(currentViewport, viewportRect);
          } else {
            drawingContext.clearRect(0, 0, viewportRect.width, viewportRect.height);
            drawScene(getVisibleWorldBounds(), null);
          }

          updatePanSnapshot();
          lastDrawViewport = currentViewport;
        }

        function redrawCanvasRegions(screenRects, options) {
          resizeDrawingCanvas();

          if (renderModel.modelCatalogMode) {
            drawCanvas("full");
            return;
          }

          const viewportRect = getViewportRect();
          const currentViewport = {
            panX: state.viewport.panX,
            panY: state.viewport.panY,
            zoom: state.viewport.zoom,
          };
          const dirtyRects = mergeScreenRects(
            (Array.isArray(screenRects) ? screenRects : [])
              .map((rect) => clipScreenRectToViewport(rect, viewportRect))
              .filter(Boolean),
          );

          if (dirtyRects.length === 0) {
            return;
          }

          for (const rect of dirtyRects) {
            drawingContext.clearRect(rect.x, rect.y, rect.width, rect.height);
            drawScene(getWorldBoundsForScreenRect(rect), rect, options);
          }

          updatePanSnapshot();
          lastDrawViewport = currentViewport;
        }

        function clipScreenRectToViewport(rect, viewportRect) {
          if (!rect || !viewportRect) {
            return undefined;
          }

          const left = Math.max(0, Math.floor(rect.x));
          const top = Math.max(0, Math.floor(rect.y));
          const right = Math.min(viewportRect.width, Math.ceil(rect.x + rect.width));
          const bottom = Math.min(viewportRect.height, Math.ceil(rect.y + rect.height));
          const width = right - left;
          const height = bottom - top;

          if (width <= 0 || height <= 0) {
            return undefined;
          }

          return {
            height,
            width,
            x: left,
            y: top,
          };
        }

        function mergeScreenRects(rects) {
          const pending = Array.isArray(rects) ? rects.slice() : [];
          const merged = [];

          while (pending.length > 0) {
            let current = pending.pop();
            let mergedAny = true;

            while (mergedAny) {
              mergedAny = false;

              for (let index = pending.length - 1; index >= 0; index -= 1) {
                if (!screenRectsTouch(current, pending[index])) {
                  continue;
                }

                current = mergeTwoScreenRects(current, pending[index]);
                pending.splice(index, 1);
                mergedAny = true;
              }
            }

            merged.push(current);
          }

          return merged;
        }

        function screenRectsTouch(left, right) {
          const gap = 14;

          return !(
            left.x + left.width + gap < right.x ||
            right.x + right.width + gap < left.x ||
            left.y + left.height + gap < right.y ||
            right.y + right.height + gap < left.y
          );
        }

        function mergeTwoScreenRects(left, right) {
          const minX = Math.min(left.x, right.x);
          const minY = Math.min(left.y, right.y);
          const maxX = Math.max(left.x + left.width, right.x + right.width);
          const maxY = Math.max(left.y + left.height, right.y + right.height);

          return {
            height: maxY - minY,
            width: maxX - minX,
            x: minX,
            y: minY,
          };
        }

        function getModelScreenRect(modelId, padding, targetState) {
          const meta = tableMetaById.get(modelId);
          if (!meta) {
            return undefined;
          }

          const position = getPositionForRenderState(targetState || state, modelId);
          return getTableScreenRectForPosition(position, meta, padding);
        }

        function getTableScreenRectForPosition(position, meta, padding) {
          if (!position || !meta) {
            return undefined;
          }

          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const extra = Math.max(0, Number(padding) || 0);
          const x = position.x * zoom + state.viewport.panX - extra;
          const y = position.y * zoom + state.viewport.panY - extra;

          return {
            height: meta.height * zoom + extra * 2,
            width: meta.width * zoom + extra * 2,
            x,
            y,
          };
        }

        function getOverlayScreenRect(overlay, padding) {
          if (!overlay) {
            return undefined;
          }

          return getLineScreenRect(
            overlay.x1,
            overlay.y1,
            overlay.x2,
            overlay.y2,
            padding,
          );
        }

        function getLineScreenRect(x1, y1, x2, y2, padding) {
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const extra = Math.max(0, Number(padding) || 0);
          const minX = Math.min(x1, x2) * zoom + state.viewport.panX - extra;
          const minY = Math.min(y1, y2) * zoom + state.viewport.panY - extra;
          const maxX = Math.max(x1, x2) * zoom + state.viewport.panX + extra;
          const maxY = Math.max(y1, y2) * zoom + state.viewport.panY + extra;

          return {
            height: Math.max(1, maxY - minY),
            width: Math.max(1, maxX - minX),
            x: minX,
            y: minY,
          };
        }

        function drawCatalogCanvas(renderMode) {
          const viewportRect = getViewportRect();
          const visibleBounds = getVisibleWorldBounds(CATALOG_TILE_PRELOAD_WORLD_PADDING);
          const visibleTiles = collectVisibleCatalogTiles(visibleBounds);

          warmCatalogTiles(visibleTiles);
          queueCatalogTilesForRaster(visibleTiles);

          drawingContext.clearRect(0, 0, viewportRect.width, viewportRect.height);
          drawingContext.save();
          drawingContext.translate(state.viewport.panX, state.viewport.panY);
          drawingContext.scale(state.viewport.zoom, state.viewport.zoom);

          drawCatalogEdges(visibleBounds);

          let renderedContentTileCount = 0;
          for (const tile of visibleTiles) {
            if (tile.renderedVersion !== catalogSceneVersion || !tile.canvas) {
              continue;
            }

            drawingContext.drawImage(tile.canvas, tile.x, tile.y, tile.width, tile.height);
            if (tile.tableCount > 0) {
              renderedContentTileCount += 1;
            }
          }

          if (renderedContentTileCount === 0) {
            drawTables(visibleBounds);
          }

          drawCatalogDynamicTableOverlays(visibleBounds);
          drawingContext.restore();
          scheduleCatalogTileRasterization();
        }

        function drawCatalogEdges(visibleBounds) {
          latestCatalogCrossings = [];
          if (!renderedEdges.length) {
            return;
          }

          const visibleSegments = [];
          drawingContext.save();
          drawingContext.lineCap = "round";
          drawingContext.lineJoin = "round";

          for (const edge of renderedEdges) {
            if (!polylineIntersectsBounds(edge.points, visibleBounds, 96)) {
              continue;
            }

            collectCatalogEdgeSegments(edge, visibleBounds, visibleSegments);
            drawingContext.strokeStyle = catalogEdgeStrokeStyle(edge.meta);
            drawingContext.lineWidth = catalogEdgeLineWidth(edge.meta);
            drawingContext.globalAlpha = catalogEdgeAlpha(edge.meta);
            drawingContext.setLineDash(catalogEdgeLineDash(edge.meta));

            drawPolyline(edge.points);
          }

          drawingContext.restore();
          latestCatalogCrossings = collectCatalogCrossings(visibleSegments, visibleBounds);
          drawCatalogCrossings(latestCatalogCrossings);
        }

        function catalogEdgeStrokeStyle(edgeMeta) {
          if (edgeMeta.cssKind.includes("many-to-many")) {
            return edgeMeta.provenance === "derived_reverse" ? "#d7c08c" : "#f7d18a";
          }

          if (edgeMeta.cssKind.includes("one-to-one")) {
            return edgeMeta.provenance === "derived_reverse" ? "#8eb7d7" : "#a8d8ff";
          }

          return edgeMeta.provenance === "derived_reverse" ? "#7ca8c7" : "#d4f3e6";
        }

        function catalogEdgeLineWidth(edgeMeta) {
          const reverseWidthDelta = edgeMeta.provenance === "derived_reverse" ? 0.8 : 0;

          if (edgeMeta.cssKind.includes("many-to-many")) {
            return 4.8 - reverseWidthDelta;
          }

          if (edgeMeta.cssKind.includes("one-to-one")) {
            return 4.4 - reverseWidthDelta;
          }

          return 4.2 - reverseWidthDelta;
        }

        function catalogEdgeAlpha(edgeMeta) {
          return edgeMeta.provenance === "derived_reverse" ? 0.42 : 0.84;
        }

        function catalogEdgeLineDash(edgeMeta) {
          return edgeMeta.provenance === "derived_reverse" ? [18, 12] : [];
        }

        function collectCatalogEdgeSegments(edge, visibleBounds, segments) {
          if (state.viewport.zoom < CATALOG_CROSSING_MARKER_MIN_ZOOM) {
            return;
          }

          if (segments.length >= CATALOG_CROSSING_MAX_SEGMENTS) {
            return;
          }

          for (const segment of findSegments(edge.points)) {
            if (segments.length >= CATALOG_CROSSING_MAX_SEGMENTS) {
              return;
            }

            const horizontal = segment.start.y === segment.end.y;
            const vertical = segment.start.x === segment.end.x;
            if (!horizontal && !vertical) {
              continue;
            }

            if (samePoint(segment.start, segment.end)) {
              continue;
            }

            if (
              !segmentIntersectsBounds(
                segment.start.x,
                segment.start.y,
                segment.end.x,
                segment.end.y,
                visibleBounds,
                64,
              )
            ) {
              continue;
            }

            segments.push({
              edgeId: edge.edgeId,
              end: segment.end,
              horizontal,
              start: segment.start,
              vertical,
            });
          }
        }

        function collectCatalogCrossings(segments, visibleBounds) {
          if (state.viewport.zoom < CATALOG_CROSSING_MARKER_MIN_ZOOM || segments.length < 2) {
            return [];
          }

          const buckets = new Map();
          for (const segment of segments) {
            const range = getCatalogCrossingBucketRange(segment, visibleBounds, 64);

            for (let row = range.startRow; row <= range.endRow; row += 1) {
              for (let column = range.startColumn; column <= range.endColumn; column += 1) {
                const key = column + ":" + row;
                let bucket = buckets.get(key);
                if (!bucket) {
                  bucket = {
                    horizontal: [],
                    vertical: [],
                  };
                  buckets.set(key, bucket);
                }

                if (segment.horizontal) {
                  bucket.horizontal.push(segment);
                } else {
                  bucket.vertical.push(segment);
                }
              }
            }
          }

          const crossings = [];
          const seenCrossings = new Set();
          let checkedPairs = 0;

          for (const bucket of buckets.values()) {
            if (!bucket.horizontal.length || !bucket.vertical.length) {
              continue;
            }

            for (const horizontal of bucket.horizontal) {
              for (const vertical of bucket.vertical) {
                if (checkedPairs >= CATALOG_CROSSING_CHECK_BUDGET) {
                  return crossings;
                }
                checkedPairs += 1;

                if (horizontal.edgeId === vertical.edgeId) {
                  continue;
                }

                const intersection = segmentIntersection(horizontal, vertical);
                if (!intersection) {
                  continue;
                }

                if (!pointInBounds(intersection.x, intersection.y, visibleBounds, 16)) {
                  continue;
                }

                if (
                  isPointAtSegmentEndpoint(intersection, horizontal) ||
                  isPointAtSegmentEndpoint(intersection, vertical)
                ) {
                  continue;
                }

                const key = Math.round(intersection.x) + ":" + Math.round(intersection.y);
                if (seenCrossings.has(key)) {
                  continue;
                }

                seenCrossings.add(key);
                crossings.push({
                  bridgeHorizontal:
                    Math.abs(horizontal.end.x - horizontal.start.x) >=
                    Math.abs(vertical.end.y - vertical.start.y),
                  position: intersection,
                });

                if (crossings.length >= CATALOG_CROSSING_MAX_MARKERS) {
                  return crossings;
                }
              }
            }
          }

          return crossings;
        }

        function getCatalogCrossingBucketRange(segment, visibleBounds, padding) {
          const minX = Math.max(Math.min(segment.start.x, segment.end.x), visibleBounds.left - padding);
          const maxX = Math.min(Math.max(segment.start.x, segment.end.x), visibleBounds.right + padding);
          const minY = Math.max(Math.min(segment.start.y, segment.end.y), visibleBounds.top - padding);
          const maxY = Math.min(Math.max(segment.start.y, segment.end.y), visibleBounds.bottom + padding);

          return {
            endColumn: Math.floor(maxX / CATALOG_CROSSING_BUCKET_SIZE),
            endRow: Math.floor(maxY / CATALOG_CROSSING_BUCKET_SIZE),
            startColumn: Math.floor(minX / CATALOG_CROSSING_BUCKET_SIZE),
            startRow: Math.floor(minY / CATALOG_CROSSING_BUCKET_SIZE),
          };
        }

        function drawCatalogCrossings(crossings) {
          if (!crossings.length) {
            return;
          }

          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const radius = Math.max(5, Math.min(24, 5.5 / zoom));
          const strokeWidth = Math.max(1.4, Math.min(6, 1.4 / zoom));

          drawingContext.save();
          drawingContext.lineCap = "round";
          drawingContext.lineJoin = "round";

          for (const crossing of crossings) {
            const x = crossing.position.x;
            const y = crossing.position.y;

            drawingContext.globalAlpha = 0.82;
            drawingContext.fillStyle = "#07121b";
            drawingContext.beginPath();
            drawingContext.arc(x, y, radius * 0.86, 0, Math.PI * 2);
            drawingContext.fill();

            drawingContext.globalAlpha = 0.94;
            drawingContext.strokeStyle = "rgba(255, 191, 105, 0.95)";
            drawingContext.lineWidth = strokeWidth;
            drawingContext.beginPath();
            if (crossing.bridgeHorizontal) {
              drawingContext.moveTo(x - radius, y);
              drawingContext.quadraticCurveTo(x, y - radius * 0.9, x + radius, y);
            } else {
              drawingContext.moveTo(x, y - radius);
              drawingContext.quadraticCurveTo(x + radius * 0.9, y, x, y + radius);
            }
            drawingContext.stroke();
          }

          drawingContext.restore();
        }

        function drawCatalogDynamicTableOverlays(visibleBounds) {
          if (drag && drag.kind === "table") {
            const meta = tableMetaById.get(drag.modelId);
            const table = tableRenderById.get(drag.modelId);
            const position = meta ? getCurrentPosition(drag.modelId) : null;
            if (
              meta &&
              table &&
              position &&
              rectIntersectsBounds(position.x, position.y, meta.width, meta.height, visibleBounds, 56)
            ) {
              drawCatalogSelectionOverlay(position, meta, "dragging");
            }
          }

          const selectedModelId = state.selectedModelId;
          if (!selectedModelId || (drag && drag.kind === "table" && drag.modelId === selectedModelId)) {
            return;
          }

          const meta = tableMetaById.get(selectedModelId);
          const table = tableRenderById.get(selectedModelId);
          const options = meta ? getTableOptions(state, selectedModelId) : null;
          if (!meta || !table || (options && options.hidden)) {
            return;
          }

          const position = getCurrentPosition(selectedModelId);
          if (!rectIntersectsBounds(position.x, position.y, meta.width, meta.height, visibleBounds, 56)) {
            return;
          }

          drawCatalogSelectionOverlay(position, meta, "selected");
        }

        function drawCatalogSelectionOverlay(position, meta, variant) {
          drawingContext.save();
          drawingContext.strokeStyle = variant === "dragging"
            ? "rgba(168, 216, 255, 0.72)"
            : "rgba(109, 208, 176, 0.62)";
          drawingContext.lineWidth = 2.2;
          drawRoundRectOn(drawingContext, position.x, position.y, meta.width, meta.height, 7);
          drawingContext.stroke();
          drawingContext.restore();
        }

        function collectVisibleCatalogTiles(visibleBounds) {
          const paddedBounds = {
            bottom: visibleBounds.bottom,
            left: visibleBounds.left,
            right: visibleBounds.right,
            top: visibleBounds.top,
          };
          const range = getTileRangeForBounds(paddedBounds);
          const tiles = [];

          for (let row = range.startRow; row <= range.endRow; row += 1) {
            for (let column = range.startColumn; column <= range.endColumn; column += 1) {
              tiles.push(getOrCreateCatalogTile(column, row));
            }
          }

          return tiles;
        }

        function queueCatalogTilesForRaster(tiles) {
          for (const tile of tiles) {
            if (tile.renderedVersion === catalogSceneVersion) {
              continue;
            }

            const key = getCatalogTileKey(tile.column, tile.row);
            if (catalogQueuedTiles.has(key)) {
              continue;
            }

            catalogQueuedTiles.add(key);
            catalogRasterQueue.push(key);
          }
        }

        function warmCatalogTiles(tiles) {
          if (catalogWarmVersion === catalogSceneVersion) {
            return;
          }

          const startedAt = performance.now();
          let warmed = 0;

          for (const tile of tiles) {
            if (tile.renderedVersion === catalogSceneVersion) {
              continue;
            }

            rasterizeCatalogTile(tile);
            warmed += 1;
            if (
              warmed >= CATALOG_TILE_WARMUP_LIMIT ||
              performance.now() - startedAt >= CATALOG_TILE_RASTER_BUDGET_MS
            ) {
              break;
            }
          }

          catalogWarmVersion = catalogSceneVersion;
        }

        function scheduleCatalogTileRasterization() {
          if (!catalogRasterQueue.length || catalogRasterFrame) {
            return;
          }

          catalogRasterFrame = window.requestAnimationFrame(() => {
            catalogRasterFrame = 0;
            rasterizeCatalogTileBatch();
          });
        }

        function rasterizeCatalogTileBatch() {
          const startedAt = performance.now();
          let renderedAny = false;

          while (catalogRasterQueue.length > 0) {
            const key = catalogRasterQueue.shift();
            catalogQueuedTiles.delete(key);
            const tile = catalogTileCache.get(key);
            if (!tile || tile.renderedVersion === catalogSceneVersion) {
              continue;
            }

            rasterizeCatalogTile(tile);
            renderedAny = true;

            if (performance.now() - startedAt >= CATALOG_TILE_RASTER_BUDGET_MS) {
              break;
            }
          }

          if (renderedAny) {
            scheduleViewportRender();
          }

          if (catalogRasterQueue.length > 0) {
            scheduleCatalogTileRasterization();
          }
        }

        function rasterizeCatalogTile(tile) {
          ensureCatalogSceneIndex();
          ensureCatalogTileCanvas(tile);

          if (!tile.context) {
            return;
          }

          tile.context.setTransform(1, 0, 0, 1, 0, 0);
          tile.context.clearRect(0, 0, tile.canvas.width, tile.canvas.height);
          tile.context.setTransform(getDeviceScale(), 0, 0, getDeviceScale(), 0, 0);

          const tableIds = collectCatalogTableIdsInBounds(tile.bounds);
          tile.tableCount = tableIds.length;
          for (const modelId of tableIds) {
            const meta = tableMetaById.get(modelId);
            const table = tableRenderById.get(modelId);
            const bounds = catalogTableBoundsById.get(modelId);
            if (!meta || !table || !bounds) {
              continue;
            }

            drawCatalogCachedTable(tile.context, bounds.x - tile.x, bounds.y - tile.y, meta, table);
          }

          tile.renderedVersion = catalogSceneVersion;
        }

        function drawCatalogCachedTable(context, localX, localY, meta, table) {
          const tableName = table.databaseTableName || meta.tableName || table.modelName;
          const modelName = table.modelName || meta.modelName || table.modelId;

          context.save();
          context.fillStyle = "#0f1e2c";
          context.strokeStyle = "rgba(123, 196, 170, 0.22)";
          context.lineWidth = 1.2;
          drawRoundRectOn(context, localX, localY, meta.width, meta.height, 7);
          context.fill();
          context.stroke();

          context.fillStyle = "rgba(123, 196, 170, 0.11)";
          context.save();
          drawRoundRectOn(context, localX, localY, meta.width, meta.height, 7);
          context.clip();
          context.fillRect(localX + 1, localY + 1, meta.width - 2, 32);
          context.restore();

          context.strokeStyle = "rgba(154, 184, 177, 0.18)";
          context.beginPath();
          context.moveTo(localX, localY + 33);
          context.lineTo(localX + meta.width, localY + 33);
          context.stroke();

          context.textAlign = "left";
          context.textBaseline = "middle";
          drawFittedTextOn(
            context,
            modelName,
            localX + 12,
            localY + 17,
            meta.width - 24,
            "#f1f7f4",
            "700 13px system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif",
          );
          drawFittedTextOn(
            context,
            tableName,
            localX + 12,
            localY + 55,
            meta.width - 24,
            "#9fb7b0",
            "500 12px system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif",
          );
          context.restore();
        }

        function ensureCatalogSceneIndex() {
          if (catalogSpatialBuckets && catalogTableBoundsById) {
            return;
          }

          catalogSpatialBuckets = new Map();
          catalogTableBoundsById = new Map();

          for (const [modelId, meta] of tableMetaById.entries()) {
            const options = getTableOptions(state, modelId);
            if (options.hidden) {
              continue;
            }

            const table = tableRenderById.get(modelId);
            if (!table) {
              continue;
            }

            const position = getCurrentPosition(modelId);
            const bounds = {
              height: meta.height,
              width: meta.width,
              x: position.x,
              y: position.y,
            };

            catalogTableBoundsById.set(modelId, bounds);
            addCatalogTableToBuckets(modelId, bounds);
          }
        }

        function addCatalogTableToBuckets(modelId, bounds) {
          const range = getBucketRangeForBounds({
            bottom: bounds.y + bounds.height,
            left: bounds.x,
            right: bounds.x + bounds.width,
            top: bounds.y,
          });

          for (let row = range.startRow; row <= range.endRow; row += 1) {
            for (let column = range.startColumn; column <= range.endColumn; column += 1) {
              const key = getCatalogBucketKey(column, row);
              if (!catalogSpatialBuckets.has(key)) {
                catalogSpatialBuckets.set(key, []);
              }
              catalogSpatialBuckets.get(key).push(modelId);
            }
          }
        }

        function collectCatalogTableIdsInBounds(bounds) {
          ensureCatalogSceneIndex();
          const ids = new Set();
          const range = getBucketRangeForBounds(bounds);

          for (let row = range.startRow; row <= range.endRow; row += 1) {
            for (let column = range.startColumn; column <= range.endColumn; column += 1) {
              const key = getCatalogBucketKey(column, row);
              const bucket = catalogSpatialBuckets.get(key);
              if (!bucket) {
                continue;
              }

              for (const modelId of bucket) {
                const tableBounds = catalogTableBoundsById.get(modelId);
                if (!tableBounds) {
                  continue;
                }

                if (
                  rectIntersectsBounds(
                    tableBounds.x,
                    tableBounds.y,
                    tableBounds.width,
                    tableBounds.height,
                    bounds,
                    0,
                  )
                ) {
                  ids.add(modelId);
                }
              }
            }
          }

          return Array.from(ids).sort((left, right) => left.localeCompare(right));
        }

        function getCatalogTableIdsNearPoint(point) {
          ensureCatalogSceneIndex();
          const range = getBucketRangeForBounds({
            bottom: point.y,
            left: point.x,
            right: point.x,
            top: point.y,
          });
          const ids = new Set();

          for (let row = range.startRow; row <= range.endRow; row += 1) {
            for (let column = range.startColumn; column <= range.endColumn; column += 1) {
              const bucket = catalogSpatialBuckets && catalogSpatialBuckets.get(getCatalogBucketKey(column, row));
              if (!bucket) {
                continue;
              }

              for (const modelId of bucket) {
                ids.add(modelId);
              }
            }
          }

          return Array.from(ids);
        }

        function getOrCreateCatalogTile(column, row) {
          const key = getCatalogTileKey(column, row);
          const existing = catalogTileCache.get(key);
          if (existing) {
            return existing;
          }

          const tile = {
            bounds: {
              bottom: (row + 1) * CATALOG_TILE_SIZE,
              left: column * CATALOG_TILE_SIZE,
              right: (column + 1) * CATALOG_TILE_SIZE,
              top: row * CATALOG_TILE_SIZE,
            },
            canvas: null,
            column,
            context: null,
            height: CATALOG_TILE_SIZE,
            key,
            renderedVersion: 0,
            row,
            tableCount: 0,
            width: CATALOG_TILE_SIZE,
            x: column * CATALOG_TILE_SIZE,
            y: row * CATALOG_TILE_SIZE,
          };

          catalogTileCache.set(key, tile);
          return tile;
        }

        function ensureCatalogTileCanvas(tile) {
          if (!tile.canvas) {
            tile.canvas = document.createElement("canvas");
            tile.context = tile.canvas.getContext("2d");
          }

          if (!tile.context) {
            return;
          }

          const deviceScale = getDeviceScale();
          const pixelWidth = Math.max(1, Math.round(tile.width * deviceScale));
          const pixelHeight = Math.max(1, Math.round(tile.height * deviceScale));
          if (tile.canvas.width !== pixelWidth || tile.canvas.height !== pixelHeight) {
            tile.canvas.width = pixelWidth;
            tile.canvas.height = pixelHeight;
          }
        }

        function getCatalogBucketKey(column, row) {
          return column + ":" + row;
        }

        function getCatalogTileKey(column, row) {
          return column + ":" + row;
        }

        function getBucketRangeForBounds(bounds) {
          return {
            endColumn: Math.floor(bounds.right / CATALOG_BUCKET_SIZE),
            endRow: Math.floor(bounds.bottom / CATALOG_BUCKET_SIZE),
            startColumn: Math.floor(bounds.left / CATALOG_BUCKET_SIZE),
            startRow: Math.floor(bounds.top / CATALOG_BUCKET_SIZE),
          };
        }

        function getTileRangeForBounds(bounds) {
          return {
            endColumn: Math.floor(bounds.right / CATALOG_TILE_SIZE),
            endRow: Math.floor(bounds.bottom / CATALOG_TILE_SIZE),
            startColumn: Math.floor(bounds.left / CATALOG_TILE_SIZE),
            startRow: Math.floor(bounds.top / CATALOG_TILE_SIZE),
          };
        }

        function drawScene(visibleBounds, clipRect, options) {
          const skipCrossings = Boolean(options && options.skipCrossings);
          const skipMethodOverlays = Boolean(options && options.skipMethodOverlays);
          drawingContext.save();
          if (clipRect) {
            drawingContext.beginPath();
            drawingContext.rect(clipRect.x, clipRect.y, clipRect.width, clipRect.height);
            drawingContext.clip();
          }
          drawingContext.translate(state.viewport.panX, state.viewport.panY);
          drawingContext.scale(state.viewport.zoom, state.viewport.zoom);
          drawEdges(visibleBounds);
          if (!skipMethodOverlays) {
            drawMethodOverlays(visibleBounds);
          }
          if (!skipCrossings) {
            drawCrossings(visibleBounds);
          }
          drawTables(visibleBounds);
          drawingContext.restore();
        }

        function drawCanvasFromPanSnapshot(currentViewport, viewportRect) {
          const deltaX = currentViewport.panX - lastDrawViewport.panX;
          const deltaY = currentViewport.panY - lastDrawViewport.panY;
          drawingContext.clearRect(0, 0, viewportRect.width, viewportRect.height);
          drawingContext.drawImage(
            panSnapshotCanvas,
            deltaX,
            deltaY,
            viewportRect.width,
            viewportRect.height,
          );

          for (const clipRect of getExposedScreenRects(deltaX, deltaY, viewportRect)) {
            drawScene(getWorldBoundsForScreenRect(clipRect), clipRect);
          }
        }

        function canReusePanSnapshot(currentViewport, viewportRect) {
          if (!lastDrawViewport || !panSnapshotCanvas) {
            return false;
          }

          if (currentViewport.zoom !== lastDrawViewport.zoom) {
            return false;
          }

          const deltaX = currentViewport.panX - lastDrawViewport.panX;
          const deltaY = currentViewport.panY - lastDrawViewport.panY;
          if (deltaX === 0 && deltaY === 0) {
            return false;
          }

          return (
            Math.abs(deltaX) < viewportRect.width &&
            Math.abs(deltaY) < viewportRect.height &&
            panSnapshotCanvas.width === drawingCanvas.width &&
            panSnapshotCanvas.height === drawingCanvas.height
          );
        }

        function updatePanSnapshot() {
          if (!panSnapshotCanvas) {
            panSnapshotCanvas = document.createElement("canvas");
            panSnapshotContext = panSnapshotCanvas.getContext("2d");
          }

          if (!panSnapshotContext) {
            return;
          }

          if (
            panSnapshotCanvas.width !== drawingCanvas.width ||
            panSnapshotCanvas.height !== drawingCanvas.height
          ) {
            panSnapshotCanvas.width = drawingCanvas.width;
            panSnapshotCanvas.height = drawingCanvas.height;
          }

          panSnapshotContext.setTransform(1, 0, 0, 1, 0, 0);
          panSnapshotContext.clearRect(0, 0, panSnapshotCanvas.width, panSnapshotCanvas.height);
          panSnapshotContext.drawImage(
            drawingCanvas,
            0,
            0,
            drawingCanvas.width,
            drawingCanvas.height,
            0,
            0,
            panSnapshotCanvas.width,
            panSnapshotCanvas.height,
          );
        }

        function getExposedScreenRects(deltaX, deltaY, viewportRect) {
          const rects = [];
          if (deltaX > 0) {
            rects.push({ x: 0, y: 0, width: deltaX, height: viewportRect.height });
          } else if (deltaX < 0) {
            rects.push({
              x: viewportRect.width + deltaX,
              y: 0,
              width: -deltaX,
              height: viewportRect.height,
            });
          }

          if (deltaY > 0) {
            rects.push({ x: 0, y: 0, width: viewportRect.width, height: deltaY });
          } else if (deltaY < 0) {
            rects.push({
              x: 0,
              y: viewportRect.height + deltaY,
              width: viewportRect.width,
              height: -deltaY,
            });
          }

          return rects.filter((rect) => rect.width > 0 && rect.height > 0);
        }

        function getViewportRect() {
          return getViewportScreenRect();
        }

        function getWorldBoundsForScreenRect(screenRect) {
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          return {
            bottom: (screenRect.y + screenRect.height - state.viewport.panY) / zoom,
            left: (screenRect.x - state.viewport.panX) / zoom,
            right: (screenRect.x + screenRect.width - state.viewport.panX) / zoom,
            top: (screenRect.y - state.viewport.panY) / zoom,
          };
        }

        function drawCrossings(visibleBounds) {
          drawingContext.save();
          drawingContext.strokeStyle = "rgba(255, 191, 105, 0.92)";
          drawingContext.fillStyle = "#0c141c";
          drawingContext.lineWidth = 1.4;
          for (const crossing of renderedCrossings) {
            if (!pointInBounds(crossing.position.x, crossing.position.y, visibleBounds, 20)) {
              continue;
            }

            drawingContext.beginPath();
            drawingContext.arc(crossing.position.x, crossing.position.y, 6.5, 0, Math.PI * 2);
            drawingContext.fill();
            drawingContext.stroke();
            drawingContext.beginPath();
            drawingContext.moveTo(crossing.position.x - 7, crossing.position.y);
            drawingContext.quadraticCurveTo(
              crossing.position.x,
              crossing.position.y - 7,
              crossing.position.x + 7,
              crossing.position.y,
            );
            drawingContext.stroke();
          }
          drawingContext.restore();
        }

        function drawEdges(visibleBounds) {
          for (const edge of renderedEdges) {
            if (!polylineIntersectsBounds(edge.points, visibleBounds, 48)) {
              continue;
            }

            drawingContext.save();
            drawingContext.strokeStyle = edge.meta.cssKind.includes("many-to-many")
              ? edge.meta.provenance === "derived_reverse"
                ? "#d7c08c"
                : "#f7d18a"
              : edge.meta.cssKind.includes("one-to-one")
                ? edge.meta.provenance === "derived_reverse"
                  ? "#8eb7d7"
                  : "#a8d8ff"
              : edge.meta.provenance === "derived_reverse"
                ? "#7ca8c7"
                : "#d4f3e6";
            drawingContext.lineWidth = edge.meta.provenance === "derived_reverse" ? 1.7 : 2.6;
            drawingContext.lineCap = "round";
            drawingContext.lineJoin = "round";
            drawingContext.globalAlpha = edge.meta.provenance === "derived_reverse" ? 0.66 : 0.96;
            if (edge.meta.provenance === "derived_reverse") {
              drawingContext.setLineDash([12, 9]);
            } else {
              drawingContext.setLineDash([]);
            }
            drawPolyline(edge.points);
            drawingContext.restore();
          }
        }

        function drawMethodOverlays(visibleBounds) {
          for (const overlay of renderedOverlays) {
            if (!overlay.active) {
              continue;
            }
            if (
              !segmentIntersectsBounds(
                overlay.x1,
                overlay.y1,
                overlay.x2,
                overlay.y2,
                visibleBounds,
                32,
              )
            ) {
              continue;
            }

            drawingContext.save();
            drawingContext.strokeStyle = "rgba(255, 191, 105, 0.72)";
            drawingContext.lineWidth = 3;
            drawingContext.setLineDash([10, 8]);
            drawingContext.beginPath();
            drawingContext.moveTo(overlay.x1, overlay.y1);
            drawingContext.lineTo(overlay.x2, overlay.y2);
            drawingContext.stroke();
            drawingContext.restore();
          }
        }

        function drawTables(visibleBounds) {
          for (const [modelId, meta] of tableMetaById.entries()) {
            const options = getTableOptions(state, modelId);
            if (options.hidden) {
              continue;
            }

            const table = tableRenderById.get(modelId);
            if (!table) {
              continue;
            }

            const position = getCurrentPosition(modelId);
            if (!rectIntersectsBounds(position.x, position.y, meta.width, meta.height, visibleBounds, 56)) {
              continue;
            }

            drawTableFrame(position, meta, table, options);
            drawModelTableContent(position, meta, table);
          }
        }

        function drawTableFrame(position, meta, table, options) {
          const selected = state.selectedModelId === table.modelId;
          const methodTarget = isMethodTarget(table.modelId);
          const dragging = drag && drag.kind === "table" && drag.modelId === table.modelId;

          drawingContext.save();
          drawingContext.shadowColor = "rgba(0, 0, 0, 0.3)";
          drawingContext.shadowBlur = 28;
          drawingContext.shadowOffsetY = 14;
          drawingContext.fillStyle = "#0f1e2c";
          drawingContext.strokeStyle = dragging
            ? "rgba(168, 216, 255, 0.72)"
            : methodTarget
              ? "rgba(255, 191, 105, 0.72)"
              : selected
                ? "rgba(109, 208, 176, 0.62)"
                : "rgba(123, 196, 170, 0.26)";
          drawingContext.lineWidth = selected || methodTarget || dragging ? 2.2 : 1.4;
          drawRoundRectOn(drawingContext, position.x, position.y, meta.width, meta.height, 7);
          drawingContext.fill();
          drawingContext.stroke();
          drawingContext.shadowColor = "transparent";
          drawingContext.fillStyle = selected ? "rgba(109, 208, 176, 0.16)" : "rgba(123, 196, 170, 0.11)";
          drawingContext.save();
          drawRoundRectOn(drawingContext, position.x, position.y, meta.width, meta.height, 7);
          drawingContext.clip();
          drawingContext.fillRect(position.x + 1, position.y + 1, meta.width - 2, 32);
          drawingContext.restore();
          drawingContext.strokeStyle = "rgba(154, 184, 177, 0.22)";
          drawingContext.lineWidth = 1;
          drawingContext.beginPath();
          drawingContext.moveTo(position.x, position.y + 33);
          drawingContext.lineTo(position.x + meta.width, position.y + 33);
          drawingContext.stroke();
          drawingContext.restore();
        }

        function drawModelTableContent(position, meta, table) {
          const tableName = table.databaseTableName || meta.tableName || table.modelName;
          const modelName = table.modelName || meta.modelName || table.modelId;
          drawingContext.save();
          drawingContext.textAlign = "left";
          drawingContext.textBaseline = "middle";
          drawFittedTextOn(
            drawingContext,
            modelName,
            position.x + 12,
            position.y + 17,
            meta.width - 24,
            "#f1f7f4",
            "700 13px system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif",
          );
          drawFittedTextOn(
            drawingContext,
            tableName,
            position.x + 12,
            position.y + 55,
            meta.width - 24,
            "#9fb7b0",
            "500 12px system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif",
          );
          drawingContext.restore();
        }

        function drawPolyline(points) {
          if (points.length === 0) {
            return;
          }

          drawingContext.beginPath();
          drawingContext.moveTo(points[0].x, points[0].y);
          for (const point of points.slice(1)) {
            drawingContext.lineTo(point.x, point.y);
          }
          drawingContext.stroke();
        }

        function drawRoundRectOn(context, x, y, width, height, radius) {
          context.beginPath();
          if (context.roundRect) {
            context.roundRect(x, y, width, height, radius);
            return;
          }

          context.moveTo(x + radius, y);
          context.lineTo(x + width - radius, y);
          context.quadraticCurveTo(x + width, y, x + width, y + radius);
          context.lineTo(x + width, y + height - radius);
          context.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
          context.lineTo(x + radius, y + height);
          context.quadraticCurveTo(x, y + height, x, y + height - radius);
          context.lineTo(x, y + radius);
          context.quadraticCurveTo(x, y, x + radius, y);
          context.closePath();
        }

        function drawText(text, x, y, color, font) {
          drawingContext.fillStyle = color;
          drawingContext.font = font;
          drawingContext.fillText(String(text), x, y);
        }

        function drawFittedTextOn(context, text, x, y, maxWidth, color, font) {
          const value = String(text);
          context.fillStyle = color;
          context.font = font;
          if (context.measureText(value).width <= maxWidth) {
            context.fillText(value, x, y);
            return;
          }

          let truncated = value;
          while (truncated.length > 1 && context.measureText(truncated + "…").width > maxWidth) {
            truncated = truncated.slice(0, -1);
          }
          context.fillText(truncated.length > 1 ? truncated + "…" : "…", x, y);
        }

        function drawFittedText(text, x, y, maxWidth, color, font) {
          drawFittedTextOn(drawingContext, text, x, y, maxWidth, color, font);
        }

        function resizeDrawingCanvas() {
          const rect = getViewportScreenRect();
          if (rect.width <= 0 || rect.height <= 0) {
            return;
          }

          const deviceScale = getDeviceScale();
          const width = Math.max(1, Math.round(rect.width * deviceScale));
          const height = Math.max(1, Math.round(rect.height * deviceScale));
          if (drawingCanvas.width !== width || drawingCanvas.height !== height) {
            drawingCanvas.width = width;
            drawingCanvas.height = height;
          }
          drawingContext.setTransform(deviceScale, 0, 0, deviceScale, 0, 0);
        }

        function getDeviceScale() {
          return window.devicePixelRatio || 1;
        }

        function getVisibleWorldBounds(padding) {
          const rect = getViewportScreenRect();
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const extra = padding || 0;
          return {
            bottom: (rect.height - state.viewport.panY) / zoom + extra,
            left: -state.viewport.panX / zoom - extra,
            right: (rect.width - state.viewport.panX) / zoom + extra,
            top: -state.viewport.panY / zoom - extra,
          };
        }

        function pointInBounds(x, y, bounds, padding) {
          return (
            x >= bounds.left - padding &&
            x <= bounds.right + padding &&
            y >= bounds.top - padding &&
            y <= bounds.bottom + padding
          );
        }

        function rectIntersectsBounds(x, y, width, height, bounds, padding) {
          return !(
            x + width < bounds.left - padding ||
            x > bounds.right + padding ||
            y + height < bounds.top - padding ||
            y > bounds.bottom + padding
          );
        }

        function polylineIntersectsBounds(points, bounds, padding) {
          if (points.length === 0) {
            return false;
          }

          let minX = Number.POSITIVE_INFINITY;
          let minY = Number.POSITIVE_INFINITY;
          let maxX = Number.NEGATIVE_INFINITY;
          let maxY = Number.NEGATIVE_INFINITY;

          for (const point of points) {
            minX = Math.min(minX, point.x);
            minY = Math.min(minY, point.y);
            maxX = Math.max(maxX, point.x);
            maxY = Math.max(maxY, point.y);
          }

          return rectIntersectsBounds(
            minX,
            minY,
            Math.max(1, maxX - minX),
            Math.max(1, maxY - minY),
            bounds,
            padding,
          );
        }

        function segmentIntersectsBounds(x1, y1, x2, y2, bounds, padding) {
          const minX = Math.min(x1, x2);
          const minY = Math.min(y1, y2);
          const maxX = Math.max(x1, x2);
          const maxY = Math.max(y1, y2);
          return rectIntersectsBounds(
            minX,
            minY,
            Math.max(1, maxX - minX),
            Math.max(1, maxY - minY),
            bounds,
            padding,
          );
        }
  `;
}
