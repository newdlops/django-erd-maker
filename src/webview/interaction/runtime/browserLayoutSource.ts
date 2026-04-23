export function getBrowserLayoutSource(): string {
  return `
        const DENSE_GRAPH_GRID_ROUTING_MAX_SEGMENTS = 1400;
        const DENSE_GRAPH_GRID_ROUTING_MAX_TABLES = 72;
        const FAST_FORCE_LAYOUT_TABLE_THRESHOLD = 56;
        let relationLayoutStateCache = null;
        let relationLayoutStateCacheKey = "";
        let baseLayoutCache = null;
        let baseLayoutCacheKey = "";

        function getLayoutVariants(settingsOverride) {
          return {
            circular: getLayoutVariant("circular", settingsOverride),
            clustered: getLayoutVariant("clustered", settingsOverride),
            flow: getLayoutVariant("flow", settingsOverride),
            graph: getLayoutVariant("graph", settingsOverride),
            hierarchical: getLayoutVariant("hierarchical", settingsOverride),
            neural: getLayoutVariant("neural", settingsOverride),
            radial: getLayoutVariant("radial", settingsOverride),
          };
        }

        function getLayoutVariant(layoutMode, settingsOverride) {
          const tuning = createLayoutTuning(settingsOverride);
          const cacheKey = JSON.stringify([
            tuning.edgeDetour,
            tuning.nodeSpacing,
            tableMetaList.length,
          ]);

          if (!(layoutVariantCache instanceof Map) || layoutVariantCacheKey !== cacheKey) {
            layoutVariantCache = new Map();
            layoutVariantCacheKey = cacheKey;
          }

          if (!layoutVariantCache.has(layoutMode)) {
            layoutVariantCache.set(
              layoutMode,
              computeLayoutVariant(layoutMode, tableMetaList, tuning),
            );
          }

          if (layoutVariantCache.has(layoutMode)) {
            return layoutVariantCache.get(layoutMode);
          }

          if (!layoutVariantCache.has("hierarchical")) {
            layoutVariantCache.set(
              "hierarchical",
              computeLayoutVariant("hierarchical", tableMetaList, tuning),
            );
          }

          return layoutVariantCache.get("hierarchical") || {};
        }

        function createLayoutVariants(tableMetaList, tuning = createLayoutTuning()) {
          return {
            circular: computeLayoutVariant("circular", tableMetaList, tuning),
            clustered: computeLayoutVariant("clustered", tableMetaList, tuning),
            flow: computeLayoutVariant("flow", tableMetaList, tuning),
            graph: computeLayoutVariant("graph", tableMetaList, tuning),
            hierarchical: computeLayoutVariant("hierarchical", tableMetaList, tuning),
            neural: computeLayoutVariant("neural", tableMetaList, tuning),
            radial: computeLayoutVariant("radial", tableMetaList, tuning),
          };
        }

        function computeLayoutVariant(layoutMode, tableMetaList, tuning = createLayoutTuning()) {
          if (tableMetaList.length > 500) {
            return createCatalogPlaceholderLayoutVariant(layoutMode, tableMetaList, tuning);
          }

          switch (layoutMode) {
            case "circular":
              return finalizeLayoutVariant(
                tableMetaList,
                createCircularLayout(tableMetaList, tuning),
                {
                  collisionPaddingX: 112 * tuning.nodeSpacing,
                  collisionPaddingY: 88 * tuning.nodeSpacing,
                  iterations: Math.round(54 * tuning.nodeSpacing),
                  margin: 128 * tuning.nodeSpacing,
                  springStrength: 0.018,
                  tuning,
                },
              );
            case "clustered":
              return finalizeLayoutVariant(
                tableMetaList,
                createClusteredLayout(tableMetaList, tuning),
                {
                  collisionPaddingX: 104 * tuning.nodeSpacing,
                  collisionPaddingY: 84 * tuning.nodeSpacing,
                  iterations: Math.round(48 * tuning.nodeSpacing),
                  margin: 120 * tuning.nodeSpacing,
                  springStrength: 0.025,
                  tuning,
                },
              );
            case "flow":
              return finalizeLayoutVariant(
                tableMetaList,
                createFlowLayout(tableMetaList, tuning),
                {
                  axisLock: "y",
                  collisionPaddingX: 112 * tuning.nodeSpacing,
                  collisionPaddingY: 92 * tuning.nodeSpacing,
                  iterations: Math.round(56 * tuning.nodeSpacing),
                  margin: 128 * tuning.nodeSpacing,
                  springStrength: 0.022,
                  tuning,
                },
              );
            case "graph":
              return finalizeLayoutVariant(
                tableMetaList,
                createRelationGraphLayout(tableMetaList, tuning),
                {
                  collisionPaddingX: 128 * tuning.nodeSpacing,
                  collisionPaddingY: 102 * tuning.nodeSpacing,
                  iterations: Math.round(66 * tuning.nodeSpacing),
                  margin: 136 * tuning.nodeSpacing,
                  springStrength: 0.016,
                  tuning,
                },
              );
            case "neural":
              return finalizeLayoutVariant(
                tableMetaList,
                createNeuralLayout(tableMetaList, tuning),
                {
                  axisLock: "x",
                  collisionPaddingX: 120 * tuning.nodeSpacing,
                  collisionPaddingY: 104 * tuning.nodeSpacing,
                  iterations: Math.round(58 * tuning.nodeSpacing),
                  margin: 132 * tuning.nodeSpacing,
                  springStrength: 0.018,
                  tuning,
                },
              );
            case "radial":
              return finalizeLayoutVariant(
                tableMetaList,
                createRadialLayout(tableMetaList, tuning),
                {
                  collisionPaddingX: 116 * tuning.nodeSpacing,
                  collisionPaddingY: 94 * tuning.nodeSpacing,
                  iterations: Math.round(56 * tuning.nodeSpacing),
                  margin: 132 * tuning.nodeSpacing,
                  springStrength: 0.018,
                  tuning,
                },
              );
            case "hierarchical":
            default:
              if (shouldUseAnalyzerHierarchicalSeed(tableMetaList)) {
                const seedPositions = createAnalyzerSeedLayout(tableMetaList);
                const wasmPositions =
                  typeof tryOptimizeLayoutWithWasm === "function"
                    ? tryOptimizeLayoutWithWasm(tableMetaList, seedPositions, tuning)
                    : undefined;

                return normalizePositionMap(
                  clonePositionMap(wasmPositions || seedPositions),
                  tableMetaList,
                  96 * tuning.nodeSpacing,
                );
              }

              return finalizeLayoutVariant(
                tableMetaList,
                createHierarchicalLayout(tableMetaList, tuning),
                {
                  axisLock: "x",
                  collisionPaddingX: 122 * tuning.nodeSpacing,
                  collisionPaddingY: 104 * tuning.nodeSpacing,
                  iterations: Math.round(60 * tuning.nodeSpacing),
                  margin: 136 * tuning.nodeSpacing,
                  springStrength: 0.018,
                  tuning,
                },
              );
          }
        }

        function shouldUseAnalyzerHierarchicalSeed(tableMetaList) {
          return (
            typeof renderModel !== "undefined" &&
            renderModel &&
            renderModel.baseLayoutMode === "hierarchical" &&
            Array.isArray(tableMetaList) &&
            tableMetaList.length > 0 &&
            tableMetaList.every((table) =>
              table.basePosition &&
              Number.isFinite(table.basePosition.x) &&
              Number.isFinite(table.basePosition.y)
            )
          );
        }

        function createAnalyzerSeedLayout(tableMetaList) {
          const positions = {};

          tableMetaList.forEach((table) => {
            positions[table.modelId] = {
              x: round2(table.basePosition?.x || 0),
              y: round2(table.basePosition?.y || 0),
            };
          });

          return positions;
        }

        function createLayoutTuning(settingsOverride) {
          const settings =
            settingsOverride ||
            (typeof getAppliedLayoutSettings === "function"
              ? getAppliedLayoutSettings()
              : (state && state.settings) || {});
          return {
            edgeDetour: Number.isFinite(settings.edgeDetour) ? settings.edgeDetour : 1.35,
            nodeSpacing: Number.isFinite(settings.nodeSpacing) ? settings.nodeSpacing : 1.4,
          };
        }

        function getRelationLayoutState(tableMetaList) {
          const cacheKey = String(tableMetaList.length) + ":" + String(edgeMeta.length);
          if (relationLayoutStateCache && relationLayoutStateCacheKey === cacheKey) {
            return relationLayoutStateCache;
          }

          relationLayoutStateCache = createRelationLayoutState(tableMetaList);
          relationLayoutStateCacheKey = cacheKey;
          return relationLayoutStateCache;
        }

        function getResolvedBaseLayouts() {
          const appliedSettings =
            typeof getAppliedLayoutSettings === "function"
              ? getAppliedLayoutSettings()
              : createLayoutTuning();
          const layoutMode = state && state.layoutMode ? state.layoutMode : "hierarchical";
          const cacheKey = JSON.stringify([
            layoutMode,
            Number.isFinite(appliedSettings.edgeDetour) ? appliedSettings.edgeDetour : 1.35,
            Number.isFinite(appliedSettings.nodeSpacing) ? appliedSettings.nodeSpacing : 1.4,
          ]);

          if (baseLayoutCache && baseLayoutCacheKey === cacheKey) {
            return baseLayoutCache;
          }

          const fallback = getLayoutVariant("hierarchical", appliedSettings);
          const active = layoutMode === "hierarchical"
            ? fallback
            : getLayoutVariant(layoutMode, appliedSettings);

          baseLayoutCache = {
            active,
            fallback,
          };
          baseLayoutCacheKey = cacheKey;
          return baseLayoutCache;
        }

        function getCanvasWorldWidthBudget(tableMetaList, widestTableWidth, tuning = createLayoutTuning()) {
          if (
            typeof canvas === "undefined" ||
            !canvas ||
            typeof canvas.getBoundingClientRect !== "function"
          ) {
            return 0;
          }

          const canvasRect = canvas.getBoundingClientRect();
          const canvasWidth = Math.max(0, canvasRect.width || 0);
          if (canvasWidth <= 0) {
            return 0;
          }

          const readableZoom = estimateLayoutReadableZoom(
            tableMetaList.length,
            widestTableWidth,
          );
          const expansionMultiplier = tableMetaList.length >= 80
            ? 2.15
            : tableMetaList.length >= 36
              ? 1.9
              : 1.68;

          return round2(
            (canvasWidth / readableZoom) *
              expansionMultiplier *
              Math.max(1, tuning.nodeSpacing * 0.96),
          );
        }

        function estimateLayoutReadableZoom(tableCount, widestTableWidth) {
          const widthBasedZoom = Math.max(
            0.24,
            Math.min(0.82, 176 / Math.max(1, widestTableWidth)),
          );

          if (tableCount <= 24) {
            return Math.max(0.58, widthBasedZoom);
          }

          if (tableCount <= 60) {
            return Math.max(0.46, widthBasedZoom - 0.06);
          }

          if (tableCount <= 120) {
            return Math.max(0.36, widthBasedZoom - 0.12);
          }

          return Math.max(0.28, widthBasedZoom - 0.18);
        }

        function createCatalogPlaceholderLayoutVariants(tableMetaList, tuning = createLayoutTuning()) {
          return {
            circular: createCatalogPlaceholderLayoutVariant("circular", tableMetaList, tuning),
            clustered: createCatalogPlaceholderLayoutVariant("clustered", tableMetaList, tuning),
            flow: createCatalogPlaceholderLayoutVariant("flow", tableMetaList, tuning),
            graph: createCatalogPlaceholderLayoutVariant("graph", tableMetaList, tuning),
            hierarchical: createCatalogPlaceholderLayoutVariant("hierarchical", tableMetaList, tuning),
            neural: createCatalogPlaceholderLayoutVariant("neural", tableMetaList, tuning),
            radial: createCatalogPlaceholderLayoutVariant("radial", tableMetaList, tuning),
          };
        }

        function createCatalogPlaceholderLayoutVariant(
          layoutMode,
          tableMetaList,
          tuning = createLayoutTuning(),
        ) {
          const config = createCatalogPlaceholderLayoutConfig(tableMetaList, tuning);

          switch (layoutMode) {
            case "circular":
              return createCatalogCircularLayout(tableMetaList, config);
            case "clustered":
              return createCatalogClusteredLayout(tableMetaList, config);
            case "flow":
              return createCatalogFlowLayout(tableMetaList, config);
            case "graph":
              return createCatalogRelationGraphLayout(tableMetaList, config);
            case "neural":
              return createCatalogNeuralLayout(tableMetaList, config);
            case "radial":
              return createCatalogRadialLayout(tableMetaList, config);
            case "hierarchical":
            default:
              return createGridLayout(tableMetaList);
          }
        }

        function finalizeLayoutVariant(tableMetaList, positions, options) {
          const finalizeOptions = createAdaptiveFinalizeOptions(tableMetaList, options || {});
          const wasmPositions =
            typeof tryOptimizeLayoutWithWasm === "function"
              ? tryOptimizeLayoutWithWasm(
                  tableMetaList,
                  positions,
                  finalizeOptions.tuning || createLayoutTuning(),
                )
              : undefined;
          const workingPositions = clonePositionMap(wasmPositions || positions);

          if (wasmPositions) {
            return normalizePositionMap(
              workingPositions,
              tableMetaList,
              finalizeOptions.margin || 72,
            );
          }

          if (tableMetaList.length > 1) {
            relaxLayoutSpacing(tableMetaList, positions, workingPositions, finalizeOptions);
          }

          return normalizePositionMap(workingPositions, tableMetaList, finalizeOptions.margin || 72);
        }

        function createAdaptiveFinalizeOptions(tableMetaList, options) {
          const count = tableMetaList.length;
          let maxIterations = Number.POSITIVE_INFINITY;

          if (count >= 160) {
            maxIterations = 10;
          } else if (count >= 96) {
            maxIterations = 14;
          } else if (count >= 56) {
            maxIterations = 20;
          } else if (count >= 32) {
            maxIterations = 30;
          }

          return {
            ...options,
            iterations: Math.max(
              4,
              Math.min(options.iterations || 28, maxIterations),
            ),
          };
        }

        function clonePositionMap(positions) {
          const clone = {};

          for (const [modelId, position] of Object.entries(positions || {})) {
            clone[modelId] = {
              x: round2(position.x),
              y: round2(position.y),
            };
          }

          return clone;
        }

        function relaxLayoutSpacing(tableMetaList, basePositions, workingPositions, options) {
          const ids = tableMetaList.map((table) => table.modelId);
          const tableById = new Map(tableMetaList.map((table) => [table.modelId, table]));
          const axisLock = options.axisLock || "none";
          const collisionPaddingX = options.collisionPaddingX || 56;
          const collisionPaddingY = options.collisionPaddingY || 44;
          const iterations = options.iterations || 28;
          const springStrength = options.springStrength || 0.04;
          const primarySpringStrength = springStrength * 1.2;
          const secondarySpringStrength = springStrength * 0.55;
          const proximityPaddingX = collisionPaddingX * 1.45;
          const proximityPaddingY = collisionPaddingY * 1.35;
          const proximityStrength = options.proximityStrength || 0.18;
          const maxStep = options.maxStep || Math.max(52, Math.max(collisionPaddingX, collisionPaddingY) * 0.45);

          for (let iteration = 0; iteration < iterations; iteration += 1) {
            const deltaById = new Map(ids.map((modelId) => [modelId, { x: 0, y: 0 }]));
            let resolvedPairs = 0;

            for (const [leftIndex, rightIndex] of createRelaxationPairIndexes(
              ids,
              tableById,
              workingPositions,
              proximityPaddingX,
              proximityPaddingY,
            )) {
              const leftId = ids[leftIndex];
              const rightId = ids[rightIndex];
              const leftTable = tableById.get(leftId);
              const rightTable = tableById.get(rightId);
              const leftPosition = workingPositions[leftId];
              const rightPosition = workingPositions[rightId];
              if (!leftTable || !rightTable || !leftPosition || !rightPosition) {
                continue;
              }

              const overlapX = computeExpandedOverlap(
                leftPosition.x,
                leftTable.width,
                rightPosition.x,
                rightTable.width,
                collisionPaddingX,
              );
              const overlapY = computeExpandedOverlap(
                leftPosition.y,
                leftTable.height,
                rightPosition.y,
                rightTable.height,
                collisionPaddingY,
              );
              if (overlapX <= 0 || overlapY <= 0) {
                const leftCenterX = leftPosition.x + leftTable.width / 2;
                const rightCenterX = rightPosition.x + rightTable.width / 2;
                const leftCenterY = leftPosition.y + leftTable.height / 2;
                const rightCenterY = rightPosition.y + rightTable.height / 2;
                const deltaX = rightCenterX - leftCenterX || alternatingDirection(leftIndex, rightIndex);
                const deltaY = rightCenterY - leftCenterY || alternatingDirection(rightIndex, leftIndex);
                const distanceX = Math.abs(deltaX);
                const distanceY = Math.abs(deltaY);
                const desiredDistanceX = (leftTable.width + rightTable.width) / 2 + proximityPaddingX;
                const desiredDistanceY = (leftTable.height + rightTable.height) / 2 + proximityPaddingY;
                const deficitX = desiredDistanceX - distanceX;
                const deficitY = desiredDistanceY - distanceY;

                if (deficitX > 0 && deficitY > 0) {
                  const leftDelta = deltaById.get(leftId);
                  const rightDelta = deltaById.get(rightId);
                  const resolveAxis = preferredSeparationAxis(axisLock, deficitX, deficitY);

                  resolvedPairs += 1;
                  if (resolveAxis === "x") {
                    const push = deficitX * proximityStrength;
                    const direction = Math.sign(deltaX) || 1;

                    leftDelta.x -= push * direction;
                    rightDelta.x += push * direction;
                  } else {
                    const push = deficitY * proximityStrength;
                    const direction = Math.sign(deltaY) || 1;

                    leftDelta.y -= push * direction;
                    rightDelta.y += push * direction;
                  }
                }

                continue;
              }

              resolvedPairs += 1;
              const leftCenterX = leftPosition.x + leftTable.width / 2;
              const rightCenterX = rightPosition.x + rightTable.width / 2;
              const leftCenterY = leftPosition.y + leftTable.height / 2;
              const rightCenterY = rightPosition.y + rightTable.height / 2;
              const deltaX = rightCenterX - leftCenterX || alternatingDirection(leftIndex, rightIndex);
              const deltaY = rightCenterY - leftCenterY || alternatingDirection(rightIndex, leftIndex);
              const leftDelta = deltaById.get(leftId);
              const rightDelta = deltaById.get(rightId);
              const resolveAxis = preferredSeparationAxis(axisLock, overlapX, overlapY);

              if (resolveAxis === "x") {
                const push = overlapX / 2 + 1;
                const direction = Math.sign(deltaX) || 1;

                leftDelta.x -= push * direction;
                rightDelta.x += push * direction;
                continue;
              }

              const push = overlapY / 2 + 1;
              const direction = Math.sign(deltaY) || 1;

              leftDelta.y -= push * direction;
              rightDelta.y += push * direction;
            }

            ids.forEach((modelId) => {
              const current = workingPositions[modelId];
              const base = basePositions[modelId] || current;
              const delta = deltaById.get(modelId);
              const springX = axisLock === "x" ? primarySpringStrength : secondarySpringStrength;
              const springY = axisLock === "y" ? primarySpringStrength : secondarySpringStrength;

              delta.x += (base.x - current.x) * springX;
              delta.y += (base.y - current.y) * springY;

              const magnitude = Math.hypot(delta.x, delta.y);
              if (magnitude > maxStep) {
                const scale = maxStep / magnitude;
                delta.x *= scale;
                delta.y *= scale;
              }

              current.x = round2(current.x + delta.x);
              current.y = round2(current.y + delta.y);
            });

            if (resolvedPairs === 0 && iteration >= 6) {
              break;
            }
          }
        }

        function createRelaxationPairIndexes(
          ids,
          tableById,
          workingPositions,
          proximityPaddingX,
          proximityPaddingY,
        ) {
          if (ids.length <= 18) {
            return createAllRelaxationPairIndexes(ids.length);
          }

          const cellWidth = Math.max(96, proximityPaddingX * 1.6);
          const cellHeight = Math.max(84, proximityPaddingY * 1.6);
          const bucketIndexesByKey = new Map();

          ids.forEach((modelId, index) => {
            const table = tableById.get(modelId);
            const position = workingPositions[modelId];
            if (!table || !position) {
              return;
            }

            const minColumn = Math.floor((position.x - proximityPaddingX) / cellWidth);
            const maxColumn = Math.floor((position.x + table.width + proximityPaddingX) / cellWidth);
            const minRow = Math.floor((position.y - proximityPaddingY) / cellHeight);
            const maxRow = Math.floor((position.y + table.height + proximityPaddingY) / cellHeight);

            for (let row = minRow; row <= maxRow; row += 1) {
              for (let column = minColumn; column <= maxColumn; column += 1) {
                const key = column + ":" + row;
                if (!bucketIndexesByKey.has(key)) {
                  bucketIndexesByKey.set(key, []);
                }
                bucketIndexesByKey.get(key).push(index);
              }
            }
          });

          const pairKeys = new Set();
          const pairs = [];

          for (const bucketIndexes of bucketIndexesByKey.values()) {
            for (let left = 0; left < bucketIndexes.length; left += 1) {
              for (let right = left + 1; right < bucketIndexes.length; right += 1) {
                const leftIndex = bucketIndexes[left];
                const rightIndex = bucketIndexes[right];
                const pairKey = leftIndex < rightIndex
                  ? leftIndex + ":" + rightIndex
                  : rightIndex + ":" + leftIndex;

                if (pairKeys.has(pairKey)) {
                  continue;
                }

                pairKeys.add(pairKey);
                pairs.push(
                  leftIndex < rightIndex
                    ? [leftIndex, rightIndex]
                    : [rightIndex, leftIndex],
                );
              }
            }
          }

          return pairs;
        }

        function createAllRelaxationPairIndexes(count) {
          const pairs = [];

          for (let leftIndex = 0; leftIndex < count; leftIndex += 1) {
            for (let rightIndex = leftIndex + 1; rightIndex < count; rightIndex += 1) {
              pairs.push([leftIndex, rightIndex]);
            }
          }

          return pairs;
        }

        function computeExpandedOverlap(startA, sizeA, startB, sizeB, padding) {
          return Math.min(startA + sizeA + padding, startB + sizeB + padding) -
            Math.max(startA - padding, startB - padding);
        }

        function preferredSeparationAxis(axisLock, overlapX, overlapY) {
          if (axisLock === "x") {
            return "y";
          }

          if (axisLock === "y") {
            return "x";
          }

          return overlapX <= overlapY ? "x" : "y";
        }

        function alternatingDirection(leftIndex, rightIndex) {
          return (leftIndex + rightIndex) % 2 === 0 ? 1 : -1;
        }

        function normalizePositionMap(positions, tableMetaList, margin) {
          if (tableMetaList.length === 0) {
            return positions;
          }

          let minX = Number.POSITIVE_INFINITY;
          let minY = Number.POSITIVE_INFINITY;

          tableMetaList.forEach((table) => {
            const position = positions[table.modelId];
            if (!position) {
              return;
            }

            minX = Math.min(minX, position.x);
            minY = Math.min(minY, position.y);
          });

          if (!Number.isFinite(minX) || !Number.isFinite(minY)) {
            return positions;
          }

          const offsetX = round2(margin - minX);
          const offsetY = round2(margin - minY);
          const normalized = {};

          for (const [modelId, position] of Object.entries(positions)) {
            normalized[modelId] = {
              x: round2(position.x + offsetX),
              y: round2(position.y + offsetY),
            };
          }

          return normalized;
        }

        function createCatalogPlaceholderLayoutConfig(tableMetaList, tuning = createLayoutTuning()) {
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);
          return {
            cellHeight: Math.max(1, maxHeight + 28 * tuning.nodeSpacing),
            cellWidth: Math.max(1, maxWidth + 48 * tuning.nodeSpacing),
            clusterGapX: 120 * tuning.nodeSpacing,
            clusterGapY: 120 * tuning.nodeSpacing,
            margin: 24 * tuning.nodeSpacing,
            maxShelfWidth: Math.max(
              Math.max(1, maxWidth + 48 * tuning.nodeSpacing) * 8,
              Math.sqrt(Math.max(tableMetaList.length, 1)) * Math.max(1, maxWidth + 48 * tuning.nodeSpacing) * 1.4,
            ),
          };
        }

        function createGridLayout(tableMetaList) {
          const ordered = tableMetaList
            .slice()
            .sort((left, right) => left.modelId.localeCompare(right.modelId));
          const maxWidth = ordered.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = ordered.reduce((largest, table) => Math.max(largest, table.height), 0);
          const columns = computeGridColumnCount(ordered.length, maxWidth, maxHeight);
          const positions = {};

          ordered.forEach((table, index) => {
            positions[table.modelId] = {
              x: round2(24 + (index % columns) * (maxWidth + 48)),
              y: round2(24 + Math.floor(index / columns) * (maxHeight + 28)),
            };
          });

          return positions;
        }

        function createCatalogCircularLayout(tableMetaList, config) {
          const ordered = tableMetaList
            .slice()
            .sort((left, right) => left.modelId.localeCompare(right.modelId));
          const positions = {};
          const itemDiameter = Math.max(config.cellWidth, config.cellHeight, 1);
          const ringSpacing = itemDiameter * 1.35;
          const estimatedRings = Math.max(2, Math.ceil(Math.sqrt(Math.max(ordered.length, 1)) / 1.6));
          const center = config.margin + estimatedRings * ringSpacing + itemDiameter;
          let index = 0;

          if (ordered.length > 0) {
            const table = ordered[index];
            positions[table.modelId] = {
              x: round2(center - table.width / 2),
              y: round2(center - table.height / 2),
            };
            index += 1;
          }

          for (let ring = 1; index < ordered.length; ring += 1) {
            const radius = ring * ringSpacing;
            const capacity = Math.max(8, Math.floor((Math.PI * 2 * radius) / itemDiameter));
            const count = Math.min(capacity, ordered.length - index);

            for (let slot = 0; slot < count; slot += 1) {
              const table = ordered[index];
              const angle = (Math.PI * 2 * slot) / count - Math.PI / 2;
              positions[table.modelId] = {
                x: round2(center + Math.cos(angle) * radius - table.width / 2),
                y: round2(center + Math.sin(angle) * radius - table.height / 2),
              };
              index += 1;
            }
          }

          return positions;
        }

        function createCatalogClusteredLayout(tableMetaList, config) {
          return createRelationAwareClusteredLayout(
            groupCatalogTables(tableMetaList),
            config,
          );
        }

        function groupCatalogTables(tableMetaList) {
          return groupTablesByKey(tableMetaList, (table) => getCatalogClusterKey(table));
        }

        function getCatalogClusterKey(table) {
          return table.appLabel + ":" + getModelNamePrefix(table.modelName || table.modelId);
        }

        function getModelNamePrefix(modelName) {
          const bareName = String(modelName).split(".").pop() || String(modelName);
          const match = bareName.match(/^[A-Z]?[a-z]+|^[A-Z]+(?![a-z])/);
          return match ? match[0].toLowerCase() : bareName.slice(0, 1).toLowerCase();
        }

        function computeGridColumnCount(count, maxWidth, maxHeight) {
          if (count <= 1) {
            return 1;
          }

          const cellWidth = Math.max(1, maxWidth + 48);
          const cellHeight = Math.max(1, maxHeight + 28);
          return Math.max(1, Math.ceil(Math.sqrt((count * cellHeight) / cellWidth)));
        }

        function createCircularLayout(tableMetaList, tuning = createLayoutTuning()) {
          const ordered = tableMetaList
            .slice()
            .sort((left, right) => left.modelId.localeCompare(right.modelId));
          const maxWidth = ordered.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = ordered.reduce((largest, table) => Math.max(largest, table.height), 0);
          const radius = Math.max(
            260 * tuning.nodeSpacing,
            (ordered.length * 74 + Math.max(maxWidth, maxHeight) / 2) * tuning.nodeSpacing,
          );
          const centerX = 640;
          const centerY = 380;
          const positions = {};

          ordered.forEach((table, index) => {
            const angle = (Math.PI * 2 * index) / Math.max(ordered.length, 1) - Math.PI / 2;
            positions[table.modelId] = {
              x: round2(centerX + Math.cos(angle) * radius - table.width / 2),
              y: round2(centerY + Math.sin(angle) * radius - table.height / 2),
            };
          });

          return positions;
        }

        function createClusteredLayout(tableMetaList, tuning = createLayoutTuning()) {
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);
          const worldWidthBudget = getCanvasWorldWidthBudget(tableMetaList, maxWidth, tuning);

          return createRelationAwareClusteredLayout(
            groupTablesByKey(tableMetaList, (table) => table.appLabel),
            {
              cellHeight: Math.max(1, maxHeight + 132 * tuning.nodeSpacing),
              cellWidth: Math.max(1, maxWidth + 104 * tuning.nodeSpacing),
              clusterGapX: 190 * tuning.nodeSpacing,
              clusterGapY: 170 * tuning.nodeSpacing,
              margin: 108 * tuning.nodeSpacing,
              maxShelfWidth: Math.max(
                960 * tuning.nodeSpacing,
                worldWidthBudget,
                Math.sqrt(Math.max(tableMetaList.length, 1)) *
                  Math.max(1, maxWidth + 104 * tuning.nodeSpacing) *
                  2.15,
              ),
            },
          );
        }

        function createRelationGraphLayout(tableMetaList, tuning = createLayoutTuning()) {
          const config = createRelationLayoutConfig(tableMetaList, {
              componentGapX: 264,
              componentGapY: 224,
              layerGapX: 0,
              layerGapY: 0,
              margin: 104,
              ringStep: Math.max(
                220,
                tableMetaList.reduce((largest, table) => Math.max(largest, table.width, table.height), 0) + 188,
              ),
              sweepIterations: 5,
            }, tuning);

          return createHybridRelationGraphLayout(tableMetaList, {
            ...config,
            ringStep: Math.max(config.ringStep, 320 * tuning.nodeSpacing),
          });
        }

        function createCatalogRelationGraphLayout(tableMetaList, config) {
          return createConcentricRelationLayout(
            tableMetaList,
            {
              componentGapX: config.clusterGapX,
              componentGapY: config.clusterGapY,
              layerGapX: 0,
              layerGapY: 0,
              margin: config.margin,
              maxShelfWidth: config.maxShelfWidth,
              ringStep: Math.max(config.cellWidth, config.cellHeight) * 0.84,
            },
          );
        }

        function createFlowLayout(tableMetaList, tuning = createLayoutTuning()) {
          return createLayeredRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 260,
              componentGapY: 210,
              layerGapX: tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0) + 148,
              layerGapY: tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0) + 224,
              margin: 88,
              orientation: "vertical",
              ringStep: 0,
              sweepIterations: 7,
            }, tuning),
          );
        }

        function createNeuralLayout(tableMetaList, tuning = createLayoutTuning()) {
          return createLayeredRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 260,
              componentGapY: 210,
              layerGapX: tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0) + 240,
              layerGapY: tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0) + 132,
              margin: 88,
              orientation: "horizontal",
              ringStep: 0,
              sweepIterations: 7,
            }, tuning),
          );
        }

        function createRadialLayout(tableMetaList, tuning = createLayoutTuning()) {
          return createRadialRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 230,
              componentGapY: 190,
              layerGapX: 0,
              layerGapY: 0,
              margin: 88,
              ringStep: Math.max(
                150,
                tableMetaList.reduce((largest, table) => Math.max(largest, table.width, table.height), 0) + 148,
              ),
              sweepIterations: 6,
            }, tuning),
          );
        }

        function createCatalogFlowLayout(tableMetaList, config) {
          return createLayeredRelationLayout(
            tableMetaList,
            {
              componentGapX: config.clusterGapX + 24,
              componentGapY: config.clusterGapY,
              layerGapX: config.cellWidth,
              layerGapY: config.cellHeight + 84,
              margin: config.margin,
              maxShelfWidth: config.maxShelfWidth,
              orientation: "vertical",
              ringStep: 0,
              sweepIterations: 4,
            },
          );
        }

        function createCatalogNeuralLayout(tableMetaList, config) {
          return createLayeredRelationLayout(
            tableMetaList,
            {
              componentGapX: config.clusterGapX + 24,
              componentGapY: config.clusterGapY,
              layerGapX: config.cellWidth + 88,
              layerGapY: config.cellHeight,
              margin: config.margin,
              maxShelfWidth: config.maxShelfWidth,
              orientation: "horizontal",
              ringStep: 0,
              sweepIterations: 4,
            },
          );
        }

        function createCatalogRadialLayout(tableMetaList, config) {
          return createRadialRelationLayout(
            tableMetaList,
            {
              componentGapX: config.clusterGapX,
              componentGapY: config.clusterGapY,
              layerGapX: 0,
              layerGapY: 0,
              margin: config.margin,
              maxShelfWidth: config.maxShelfWidth,
              ringStep: Math.max(config.cellWidth, config.cellHeight) * 0.84,
              sweepIterations: 4,
            },
          );
        }

        function createRelationLayoutConfig(tableMetaList, overrides, tuning = createLayoutTuning()) {
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);
          const spacing = Math.max(1, tuning.nodeSpacing * 1.18);
          const worldWidthBudget = getCanvasWorldWidthBudget(tableMetaList, maxWidth, tuning);

          return {
            componentGapX: round2(overrides.componentGapX * spacing),
            componentGapY: round2(overrides.componentGapY * spacing),
            layerGapX: round2(overrides.layerGapX * spacing),
            layerGapY: round2(overrides.layerGapY * spacing),
            margin: round2(overrides.margin * spacing),
            maxShelfWidth: Math.max(
              1080 * spacing,
              worldWidthBudget,
              Math.sqrt(Math.max(tableMetaList.length, 1)) *
                Math.max(1, maxWidth + 112 * spacing) *
                2.3,
            ),
            orientation: overrides.orientation || "horizontal",
            ringStep: round2((overrides.ringStep || Math.max(maxWidth, maxHeight) + 72) * spacing),
            sweepIterations: createAdaptiveSweepIterations(
              overrides.sweepIterations || 0,
              tableMetaList.length,
            ),
          };
        }

        function createAdaptiveSweepIterations(iterations, tableCount) {
          if (iterations <= 0) {
            return 0;
          }

          if (tableCount >= 160) {
            return Math.min(iterations, 1);
          }

          if (tableCount >= 96) {
            return Math.min(iterations, 2);
          }

          if (tableCount >= 56) {
            return Math.min(iterations, 3);
          }

          return iterations;
        }

        function createRelationLayoutState(tableMetaList) {
          const tableById = new Map(tableMetaList.map((table) => [table.modelId, table]));
          const adjacencyById = createEmptyRelationNodeMap(tableMetaList);
          const incomingById = createEmptyRelationNodeMap(tableMetaList);
          const outgoingById = createEmptyRelationNodeMap(tableMetaList);

          for (const edge of edgeMeta) {
            if (
              edge.sourceModelId === edge.targetModelId ||
              !tableById.has(edge.sourceModelId) ||
              !tableById.has(edge.targetModelId)
            ) {
              continue;
            }

            adjacencyById.get(edge.sourceModelId).add(edge.targetModelId);
            adjacencyById.get(edge.targetModelId).add(edge.sourceModelId);
            outgoingById.get(edge.sourceModelId).add(edge.targetModelId);
            incomingById.get(edge.targetModelId).add(edge.sourceModelId);
          }

          return {
            adjacencyById,
            components: createRelationComponents(
              tableMetaList,
              adjacencyById,
              incomingById,
              outgoingById,
            ),
            incomingById,
            outgoingById,
            tableById,
          };
        }

        function createEmptyRelationNodeMap(tableMetaList) {
          return new Map(tableMetaList.map((table) => [table.modelId, new Set()]));
        }

        function createRelationComponents(tableMetaList, adjacencyById, incomingById, outgoingById) {
          const visited = new Set();
          const components = [];
          const orderedTableIds = tableMetaList
            .map((table) => table.modelId)
            .sort((left, right) =>
              compareRelationNodePriority(left, right, adjacencyById, incomingById, outgoingById),
            );

          for (const startId of orderedTableIds) {
            if (visited.has(startId)) {
              continue;
            }

            const queue = [startId];
            const componentIds = [];
            visited.add(startId);

            while (queue.length > 0) {
              const currentId = queue.shift();
              componentIds.push(currentId);

              const neighbors = Array.from(adjacencyById.get(currentId) || [])
                .sort((left, right) =>
                  compareRelationNodePriority(left, right, adjacencyById, incomingById, outgoingById),
                );

              for (const neighborId of neighbors) {
                if (visited.has(neighborId)) {
                  continue;
                }

                visited.add(neighborId);
                queue.push(neighborId);
              }
            }

            componentIds.sort((left, right) =>
              compareRelationNodePriority(left, right, adjacencyById, incomingById, outgoingById),
            );
            components.push(componentIds);
          }

          return components.sort((left, right) => {
            const sizeDelta = right.length - left.length;
            if (sizeDelta !== 0) {
              return sizeDelta;
            }

            return left[0].localeCompare(right[0]);
          });
        }

        function compareRelationNodePriority(leftId, rightId, adjacencyById, incomingById, outgoingById) {
          const leftTotalDegree = adjacencyById.get(leftId)?.size || 0;
          const rightTotalDegree = adjacencyById.get(rightId)?.size || 0;
          if (leftTotalDegree !== rightTotalDegree) {
            return rightTotalDegree - leftTotalDegree;
          }

          const leftOutgoingDegree = outgoingById.get(leftId)?.size || 0;
          const rightOutgoingDegree = outgoingById.get(rightId)?.size || 0;
          if (leftOutgoingDegree !== rightOutgoingDegree) {
            return rightOutgoingDegree - leftOutgoingDegree;
          }

          const leftIncomingDegree = incomingById.get(leftId)?.size || 0;
          const rightIncomingDegree = incomingById.get(rightId)?.size || 0;
          if (leftIncomingDegree !== rightIncomingDegree) {
            return leftIncomingDegree - rightIncomingDegree;
          }

          return leftId.localeCompare(rightId);
        }

        function createConcentricRelationLayout(tableMetaList, config) {
          const relationState = getRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            describeComponentPlan(
              createConcentricComponentPlan(componentIds, relationState, config, componentIndex),
              componentIds,
              relationState,
            ),
          );

          return placeRelationComponentPlans(componentPlans, config);
        }

        function createForceDirectedRelationLayout(tableMetaList, config) {
          const relationState = getRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            describeComponentPlan(
              createForceDirectedComponentPlan(componentIds, relationState, config, componentIndex),
              componentIds,
              relationState,
            ),
          );

          return placeRelationComponentPlans(componentPlans, config);
        }

        function createHybridRelationGraphLayout(tableMetaList, config) {
          const relationState = getRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            describeComponentPlan(
              createHybridGraphComponentPlan(
                componentIds,
                relationState,
                config,
                componentIndex,
              ),
              componentIds,
              relationState,
            ),
          );

          return placeRelationComponentPlans(componentPlans, config);
        }

        function createHybridGraphComponentPlan(componentIds, relationState, config, componentIndex) {
          const edgeCount = createRelationEdgePairs(componentIds, relationState.adjacencyById).length;
          const density = edgeCount / Math.max(componentIds.length - 1, 1);
          const shouldPreferLayered =
            componentIds.length >= FAST_FORCE_LAYOUT_TABLE_THRESHOLD / 2 ||
            density >= 1.2;

          if (shouldPreferLayered) {
            return createLayeredComponentPlan(
              componentIds,
              relationState,
              {
                ...config,
                layerGapX: Math.max(config.layerGapX || 0, config.ringStep * 0.96),
                layerGapY: Math.max(
                  config.layerGapY || 0,
                  (config.ringStep || 0) * (density >= 1.5 ? 0.82 : 0.72),
                ),
                orientation: density >= 1.5 || componentIds.length >= 14 ? "horizontal" : "vertical",
                sweepIterations: Math.max(config.sweepIterations || 0, 4),
              },
            );
          }

          return createRadialComponentPlan(
            componentIds,
            relationState,
            {
              ...config,
              ringStep: Math.max(config.ringStep, 286),
              sweepIterations: Math.max(config.sweepIterations || 0, 3),
            },
            componentIndex,
          );
        }

        function createForceDirectedComponentPlan(componentIds, relationState, config, componentIndex) {
          if (componentIds.length <= 2) {
            return createConcentricComponentPlan(componentIds, relationState, config, componentIndex);
          }

          const rootId = pickRelationComponentRoot(componentIds, relationState);
          const seedPlan = createConcentricComponentPlan(
            componentIds,
            relationState,
            config,
            componentIndex,
          );
          const centersById = createCenteredPositionMap(
            componentIds,
            seedPlan,
            relationState.tableById,
          );
          const edgePairs = createRelationEdgePairs(componentIds, relationState.adjacencyById);
          const forceConfig = createForceLayoutConfig(
            componentIds,
            relationState,
            config,
            edgePairs.length,
          );
          let temperature = 1;

          for (let iteration = 0; iteration < forceConfig.iterations; iteration += 1) {
            const deltaById = new Map(componentIds.map((modelId) => [modelId, { x: 0, y: 0 }]));

            for (let leftIndex = 0; leftIndex < componentIds.length; leftIndex += 1) {
              const leftId = componentIds[leftIndex];
              const leftCenter = centersById.get(leftId);
              const leftTable = relationState.tableById.get(leftId);

              for (let rightIndex = leftIndex + 1; rightIndex < componentIds.length; rightIndex += 1) {
                const rightId = componentIds[rightIndex];
                const rightCenter = centersById.get(rightId);
                const rightTable = relationState.tableById.get(rightId);
                let dx = rightCenter.x - leftCenter.x;
                let dy = rightCenter.y - leftCenter.y;
                let distance = Math.hypot(dx, dy);

                if (distance < 0.001) {
                  const angle = (leftIndex * 1.97 + rightIndex * 0.73 + 1) * Math.PI * 0.5;
                  dx = Math.cos(angle);
                  dy = Math.sin(angle);
                  distance = 1;
                }

                const unitX = dx / distance;
                const unitY = dy / distance;
                const preferredGap =
                  Math.max(leftTable.width + rightTable.width, leftTable.height + rightTable.height) / 2 +
                  forceConfig.collisionPadding;
                const repulsion = forceConfig.repulsionStrength / distance;
                const collisionPush =
                  distance < preferredGap
                    ? (preferredGap - distance) * forceConfig.collisionStrength
                    : 0;
                const force = repulsion + collisionPush;
                const leftDelta = deltaById.get(leftId);
                const rightDelta = deltaById.get(rightId);

                leftDelta.x -= unitX * force;
                leftDelta.y -= unitY * force;
                rightDelta.x += unitX * force;
                rightDelta.y += unitY * force;
              }
            }

            edgePairs.forEach(([sourceId, targetId]) => {
              const sourceCenter = centersById.get(sourceId);
              const targetCenter = centersById.get(targetId);
              let dx = targetCenter.x - sourceCenter.x;
              let dy = targetCenter.y - sourceCenter.y;
              let distance = Math.hypot(dx, dy);

              if (distance < 0.001) {
                dx = 1;
                dy = 0;
                distance = 1;
              }

              const sourceDegree = relationState.adjacencyById.get(sourceId)?.size || 0;
              const targetDegree = relationState.adjacencyById.get(targetId)?.size || 0;
              const desiredLength =
                forceConfig.preferredEdgeLength +
                (sourceDegree + targetDegree) * forceConfig.degreeDistanceStep;
              const springForce = (distance - desiredLength) * forceConfig.springStrength;
              const unitX = dx / distance;
              const unitY = dy / distance;
              const sourceDelta = deltaById.get(sourceId);
              const targetDelta = deltaById.get(targetId);

              sourceDelta.x += unitX * springForce;
              sourceDelta.y += unitY * springForce;
              targetDelta.x -= unitX * springForce;
              targetDelta.y -= unitY * springForce;
            });

            componentIds.forEach((modelId) => {
              const center = centersById.get(modelId);
              const delta = deltaById.get(modelId);
              const degree = relationState.adjacencyById.get(modelId)?.size || 0;
              const centerStrength = forceConfig.centerStrength * (1 + degree * 0.04);

              delta.x += -center.x * centerStrength;
              delta.y += -center.y * centerStrength;
            });

            const rootCenter = centersById.get(rootId);
            const rootDelta = deltaById.get(rootId);
            rootDelta.x += -rootCenter.x * forceConfig.rootCenterStrength;
            rootDelta.y += -rootCenter.y * forceConfig.rootCenterStrength;

            componentIds.forEach((modelId) => {
              const center = centersById.get(modelId);
              const delta = deltaById.get(modelId);
              const magnitude = Math.hypot(delta.x, delta.y);
              const maxStep = Math.max(1, forceConfig.maxStep * temperature);
              const scale = magnitude > maxStep ? maxStep / magnitude : 1;

              center.x = round2(center.x + delta.x * scale);
              center.y = round2(center.y + delta.y * scale);
            });

            normalizeCenterPositions(componentIds, centersById);
            temperature *= forceConfig.cooling;
          }

          const localPositions = {};
          componentIds.forEach((modelId) => {
            const center = centersById.get(modelId);
            const table = relationState.tableById.get(modelId);
            localPositions[modelId] = {
              x: round2(center.x - table.width / 2),
              y: round2(center.y - table.height / 2),
            };
          });

          return normalizeLocalComponentPositions(localPositions, relationState.tableById);
        }

        function createCenteredPositionMap(componentIds, componentPlan, tableById) {
          const centerX = componentPlan.width / 2;
          const centerY = componentPlan.height / 2;
          const centersById = new Map();

          componentIds.forEach((modelId) => {
            const table = tableById.get(modelId);
            const position = componentPlan.positions[modelId] || { x: 0, y: 0 };
            centersById.set(modelId, {
              x: round2(position.x + table.width / 2 - centerX),
              y: round2(position.y + table.height / 2 - centerY),
            });
          });

          return centersById;
        }

        function createRelationEdgePairs(componentIds, adjacencyById) {
          const componentIdSet = new Set(componentIds);
          const pairs = [];

          componentIds.forEach((sourceId) => {
            Array.from(adjacencyById.get(sourceId) || [])
              .filter((targetId) => componentIdSet.has(targetId) && sourceId.localeCompare(targetId) < 0)
              .sort((left, right) => left.localeCompare(right))
              .forEach((targetId) => {
                pairs.push([sourceId, targetId]);
              });
          });

          return pairs;
        }

        function createForceLayoutConfig(componentIds, relationState, config, edgeCount) {
          const nodeCount = Math.max(componentIds.length, 1);
          const totalWidth = componentIds.reduce(
            (sum, modelId) => sum + relationState.tableById.get(modelId).width,
            0,
          );
          const totalHeight = componentIds.reduce(
            (sum, modelId) => sum + relationState.tableById.get(modelId).height,
            0,
          );
          const averageSpan = Math.max(1, (totalWidth + totalHeight) / (nodeCount * 2));
          const density = edgeCount / Math.max(nodeCount - 1, 1);

          return {
            centerStrength: nodeCount >= 24 ? 0.008 : 0.012,
            collisionPadding: Math.max(52, averageSpan * 0.38),
            collisionStrength: 0.24,
            cooling: nodeCount >= 64 ? 0.94 : 0.955,
            degreeDistanceStep: Math.max(10, averageSpan * 0.11),
            iterations: Math.min(132, Math.max(42, Math.round(34 + Math.sqrt(nodeCount) * 8))),
            maxStep: Math.max(28, averageSpan * 0.36),
            preferredEdgeLength: Math.max(
              220,
              (config.ringStep || averageSpan) * (density >= 1.6 ? 1.05 : 1.22),
            ),
            repulsionStrength: Math.max(
              5200,
              averageSpan * averageSpan * (density >= 1.6 ? 1.7 : 2.05),
            ),
            rootCenterStrength: nodeCount >= 24 ? 0.035 : 0.05,
            springStrength: density >= 1.6 ? 0.035 : 0.045,
          };
        }

        function normalizeCenterPositions(componentIds, centersById) {
          if (componentIds.length === 0) {
            return;
          }

          const centroid = componentIds.reduce(
            (sum, modelId) => {
              const center = centersById.get(modelId);
              sum.x += center.x;
              sum.y += center.y;
              return sum;
            },
            { x: 0, y: 0 },
          );
          const centroidX = centroid.x / componentIds.length;
          const centroidY = centroid.y / componentIds.length;

          componentIds.forEach((modelId) => {
            const center = centersById.get(modelId);
            center.x = round2(center.x - centroidX);
            center.y = round2(center.y - centroidY);
          });
        }

        function createConcentricComponentPlan(componentIds, relationState, config, componentIndex) {
          const rootId = pickRelationComponentRoot(componentIds, relationState);
          const distanceById = createUndirectedDistanceMap(
            componentIds,
            rootId,
            relationState.adjacencyById,
          );
          const ringIdsByDepth = new Map();

          componentIds.forEach((modelId) => {
            const depth = distanceById.get(modelId) || 0;
            if (!ringIdsByDepth.has(depth)) {
              ringIdsByDepth.set(depth, []);
            }
            ringIdsByDepth.get(depth).push(modelId);
          });

          const localPositions = {};
          const orderedDepths = Array.from(ringIdsByDepth.keys()).sort((left, right) => left - right);

          orderedDepths.forEach((depth) => {
            const ringIds = ringIdsByDepth.get(depth)
              .slice()
              .sort((left, right) =>
                compareRelationNodePriority(
                  left,
                  right,
                  relationState.adjacencyById,
                  relationState.incomingById,
                  relationState.outgoingById,
                ),
              );
            if (depth === 0) {
              const rootTable = relationState.tableById.get(ringIds[0]);
              localPositions[ringIds[0]] = {
                x: round2(-rootTable.width / 2),
                y: round2(-rootTable.height / 2),
              };
              return;
            }

            const radius = depth * config.ringStep;
            const angleOffset = componentIndex * 0.61 + depth * 0.17;

            ringIds.forEach((modelId, index) => {
              const table = relationState.tableById.get(modelId);
              const angle = angleOffset + (Math.PI * 2 * index) / Math.max(ringIds.length, 1);
              localPositions[modelId] = {
                x: round2(Math.cos(angle) * radius - table.width / 2),
                y: round2(Math.sin(angle) * radius - table.height / 2),
              };
            });
          });

          return normalizeLocalComponentPositions(localPositions, relationState.tableById);
        }

        function pickRelationComponentRoot(componentIds, relationState) {
          return componentIds
            .slice()
            .sort((left, right) =>
              compareRelationNodePriority(
                left,
                right,
                relationState.adjacencyById,
                relationState.incomingById,
                relationState.outgoingById,
              ),
            )[0];
        }

        function createUndirectedDistanceMap(componentIds, rootId, adjacencyById) {
          const componentIdSet = new Set(componentIds);
          const distanceById = new Map([[rootId, 0]]);
          const queue = [rootId];

          while (queue.length > 0) {
            const currentId = queue.shift();
            const nextDistance = (distanceById.get(currentId) || 0) + 1;

            for (const neighborId of adjacencyById.get(currentId) || []) {
              if (!componentIdSet.has(neighborId) || distanceById.has(neighborId)) {
                continue;
              }

              distanceById.set(neighborId, nextDistance);
              queue.push(neighborId);
            }
          }

          componentIds.forEach((modelId) => {
            if (!distanceById.has(modelId)) {
              distanceById.set(modelId, 0);
            }
          });

          return distanceById;
        }

        function normalizeLocalComponentPositions(localPositions, tableById) {
          let minX = Number.POSITIVE_INFINITY;
          let minY = Number.POSITIVE_INFINITY;
          let maxX = Number.NEGATIVE_INFINITY;
          let maxY = Number.NEGATIVE_INFINITY;

          for (const [modelId, position] of Object.entries(localPositions)) {
            const table = tableById.get(modelId);
            minX = Math.min(minX, position.x);
            minY = Math.min(minY, position.y);
            maxX = Math.max(maxX, position.x + table.width);
            maxY = Math.max(maxY, position.y + table.height);
          }

          const normalizedPositions = {};
          for (const [modelId, position] of Object.entries(localPositions)) {
            normalizedPositions[modelId] = {
              x: round2(position.x - minX),
              y: round2(position.y - minY),
            };
          }

          return {
            height: round2(Math.max(1, maxY - minY)),
            positions: normalizedPositions,
            width: round2(Math.max(1, maxX - minX)),
          };
        }

        function createLayeredRelationLayout(tableMetaList, config) {
          const relationState = getRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds) =>
            describeComponentPlan(
              createLayeredComponentPlan(componentIds, relationState, config),
              componentIds,
              relationState,
            ),
          );

          return placeRelationComponentPlans(componentPlans, config);
        }

        function createLayeredComponentPlan(componentIds, relationState, config) {
          const layerById = createNeuralLayerMap(componentIds, relationState);
          const layers = createRelationLayers(componentIds, layerById, relationState);
          const orderedLayers = reduceLayerCrossings(
            layers,
            layerById,
            relationState,
            config.sweepIterations || 0,
          );
          const orientation = config.orientation || "horizontal";
          const maxTableWidth = Math.max(
            1,
            ...componentIds.map((modelId) => relationState.tableById.get(modelId).width),
          );
          const maxTableHeight = Math.max(
            1,
            ...componentIds.map((modelId) => relationState.tableById.get(modelId).height),
          );
          const localPositions = {};

          if (orientation === "vertical") {
            const maxLayerSize = Math.max(1, ...orderedLayers.map((layerIds) => layerIds.length));
            const componentWidth = Math.max(
              maxTableWidth,
              (maxLayerSize - 1) * config.layerGapX + maxTableWidth,
            );

            orderedLayers.forEach((layerIds, layerIndex) => {
              const layerWidth =
                layerIds.length > 0
                  ? (layerIds.length - 1) * config.layerGapX + maxTableWidth
                  : maxTableWidth;
              const layerOffsetX = Math.max(0, (componentWidth - layerWidth) / 2);

              layerIds.forEach((modelId, index) => {
                const table = relationState.tableById.get(modelId);
                localPositions[modelId] = {
                  x: round2(layerOffsetX + index * config.layerGapX),
                  y: round2(layerIndex * config.layerGapY),
                };
              });
            });
          } else {
            const maxLayerSize = Math.max(1, ...orderedLayers.map((layerIds) => layerIds.length));
            const componentHeight = Math.max(
              maxTableHeight,
              (maxLayerSize - 1) * config.layerGapY + maxTableHeight,
            );

            orderedLayers.forEach((layerIds, layerIndex) => {
              const layerHeight =
                layerIds.length > 0
                  ? (layerIds.length - 1) * config.layerGapY + maxTableHeight
                  : maxTableHeight;
              const layerOffsetY = Math.max(0, (componentHeight - layerHeight) / 2);

              layerIds.forEach((modelId, index) => {
                const table = relationState.tableById.get(modelId);
                localPositions[modelId] = {
                  x: round2(layerIndex * config.layerGapX),
                  y: round2(layerOffsetY + index * config.layerGapY),
                };
              });
            });
          }

          return normalizeLocalComponentPositions(localPositions, relationState.tableById);
        }

        function createRadialRelationLayout(tableMetaList, config) {
          const relationState = getRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            describeComponentPlan(
              createRadialComponentPlan(componentIds, relationState, config, componentIndex),
              componentIds,
              relationState,
            ),
          );

          return placeRelationComponentPlans(componentPlans, config);
        }

        function createRadialComponentPlan(componentIds, relationState, config, componentIndex) {
          const layerById = createNeuralLayerMap(componentIds, relationState);
          const layers = createRelationLayers(componentIds, layerById, relationState);
          const orderedLayers = reduceLayerCrossings(
            layers,
            layerById,
            relationState,
            config.sweepIterations || 0,
          );
          const maxTableSpan = Math.max(
            1,
            ...componentIds.map((modelId) => {
              const table = relationState.tableById.get(modelId);
              return Math.max(table.width, table.height);
            }),
          );
          const localPositions = {};

          orderedLayers.forEach((layerIds, layerIndex) => {
            if (layerIds.length === 0) {
              return;
            }

            const ringCapacityRadius =
              layerIds.length <= 1
                ? 0
                : (layerIds.length * (maxTableSpan + 88)) / (Math.PI * 2);
            const radius =
              layerIndex === 0
                ? layerIds.length === 1
                  ? 0
                  : Math.max(config.ringStep * 0.45, maxTableSpan * 0.9, ringCapacityRadius)
                : Math.max(layerIndex * config.ringStep, ringCapacityRadius);
            const angleOffset = componentIndex * 0.41 + layerIndex * 0.19 - Math.PI / 2;

            layerIds.forEach((modelId, index) => {
              const table = relationState.tableById.get(modelId);
              const angle =
                layerIds.length <= 1
                  ? angleOffset
                  : angleOffset + (Math.PI * 2 * index) / layerIds.length;
              localPositions[modelId] = {
                x: round2(Math.cos(angle) * radius - table.width / 2),
                y: round2(Math.sin(angle) * radius - table.height / 2),
              };
            });
          });

          return normalizeLocalComponentPositions(localPositions, relationState.tableById);
        }

        function createNeuralLayerMap(componentIds, relationState) {
          const componentIdSet = new Set(componentIds);
          const incomingCountById = new Map(
            componentIds.map((modelId) => [
              modelId,
              Array.from(relationState.incomingById.get(modelId) || []).filter((neighborId) =>
                componentIdSet.has(neighborId),
              ).length,
            ]),
          );
          const remainingIds = new Set(componentIds);
          const layerById = new Map();
          const queue = componentIds
            .filter((modelId) => (incomingCountById.get(modelId) || 0) === 0)
            .sort((left, right) =>
              compareRelationNodePriority(
                left,
                right,
                relationState.adjacencyById,
                relationState.incomingById,
                relationState.outgoingById,
              ),
            );

          while (remainingIds.size > 0) {
            if (queue.length === 0) {
              queue.push(
                pickNextNeuralSeed(componentIds, remainingIds, incomingCountById, relationState),
              );
            }

            const currentId = queue.shift();
            if (!remainingIds.has(currentId)) {
              continue;
            }

            const parentLayers = Array.from(relationState.incomingById.get(currentId) || [])
              .filter((neighborId) => componentIdSet.has(neighborId) && layerById.has(neighborId))
              .map((neighborId) => layerById.get(neighborId));
            const layer = parentLayers.length > 0 ? Math.max(...parentLayers) + 1 : 0;

            layerById.set(currentId, layer);
            remainingIds.delete(currentId);

            const outgoingIds = Array.from(relationState.outgoingById.get(currentId) || [])
              .filter((neighborId) => remainingIds.has(neighborId))
              .sort((left, right) =>
                compareRelationNodePriority(
                  left,
                  right,
                  relationState.adjacencyById,
                  relationState.incomingById,
                  relationState.outgoingById,
                ),
              );

            outgoingIds.forEach((neighborId) => {
              incomingCountById.set(
                neighborId,
                Math.max(0, (incomingCountById.get(neighborId) || 0) - 1),
              );
              if ((incomingCountById.get(neighborId) || 0) === 0) {
                queue.push(neighborId);
              }
            });
          }

          return layerById;
        }

        function pickNextNeuralSeed(componentIds, remainingIds, incomingCountById, relationState) {
          return componentIds
            .filter((modelId) => remainingIds.has(modelId))
            .sort((left, right) => {
              const leftScore =
                (relationState.outgoingById.get(left)?.size || 0) -
                (relationState.incomingById.get(left)?.size || 0);
              const rightScore =
                (relationState.outgoingById.get(right)?.size || 0) -
                (relationState.incomingById.get(right)?.size || 0);
              if (leftScore !== rightScore) {
                return rightScore - leftScore;
              }

              const incomingDelta =
                (incomingCountById.get(left) || 0) -
                (incomingCountById.get(right) || 0);
              if (incomingDelta !== 0) {
                return incomingDelta;
              }

              return compareRelationNodePriority(
                left,
                right,
                relationState.adjacencyById,
                relationState.incomingById,
                relationState.outgoingById,
              );
            })[0];
        }

        function createRelationLayers(componentIds, layerById, relationState) {
          const maxLayerIndex = Math.max(...Array.from(layerById.values()), 0);
          const layers = Array.from({ length: maxLayerIndex + 1 }, () => []);

          componentIds.forEach((modelId) => {
            layers[layerById.get(modelId) || 0].push(modelId);
          });

          return layers.map((layerIds) =>
            layerIds.slice().sort((left, right) =>
              compareRelationNodePriority(
                left,
                right,
                relationState.adjacencyById,
                relationState.incomingById,
                relationState.outgoingById,
              ),
            ),
          );
        }

        function reduceLayerCrossings(layers, layerById, relationState, sweepIterations) {
          if (layers.length <= 2 || sweepIterations <= 0) {
            return layers.map((layerIds) => layerIds.slice());
          }

          const orderedLayers = layers.map((layerIds) => layerIds.slice());
          const useTranspose = countLayeredNodes(orderedLayers) <= 56;

          for (let iteration = 0; iteration < sweepIterations; iteration += 1) {
            for (let layerIndex = 1; layerIndex < orderedLayers.length; layerIndex += 1) {
              orderedLayers[layerIndex] = sweepLayerIdsByNeighborBarycenter(
                orderedLayers[layerIndex],
                layerIndex,
                orderedLayers,
                layerById,
                relationState,
                "incoming",
              );
            }

            if (useTranspose) {
              transposeRelationLayers(orderedLayers, layerById, relationState);
            }

            for (let layerIndex = orderedLayers.length - 2; layerIndex >= 0; layerIndex -= 1) {
              orderedLayers[layerIndex] = sweepLayerIdsByNeighborBarycenter(
                orderedLayers[layerIndex],
                layerIndex,
                orderedLayers,
                layerById,
                relationState,
                "outgoing",
              );
            }

            if (useTranspose) {
              transposeRelationLayers(orderedLayers, layerById, relationState);
            }
          }

          return orderedLayers;
        }

        function countLayeredNodes(layers) {
          return layers.reduce((total, layerIds) => total + layerIds.length, 0);
        }

        function transposeRelationLayers(orderedLayers, layerById, relationState) {
          for (let pass = 0; pass < 2; pass += 1) {
            let passImproved = false;

            for (let layerIndex = 0; layerIndex < orderedLayers.length; layerIndex += 1) {
              const layerIds = orderedLayers[layerIndex];
              if (!layerIds || layerIds.length <= 1) {
                continue;
              }

              for (let itemIndex = 0; itemIndex < layerIds.length - 1; itemIndex += 1) {
                const baselineCrossings = countLayerNeighborhoodCrossings(
                  orderedLayers,
                  layerIndex,
                  layerById,
                  relationState,
                );
                const leftId = layerIds[itemIndex];
                const rightId = layerIds[itemIndex + 1];

                layerIds[itemIndex] = rightId;
                layerIds[itemIndex + 1] = leftId;

                const swappedCrossings = countLayerNeighborhoodCrossings(
                  orderedLayers,
                  layerIndex,
                  layerById,
                  relationState,
                );

                if (swappedCrossings < baselineCrossings) {
                  passImproved = true;
                  continue;
                }

                layerIds[itemIndex] = leftId;
                layerIds[itemIndex + 1] = rightId;
              }
            }

            if (!passImproved) {
              break;
            }
          }
        }

        function countLayerNeighborhoodCrossings(orderedLayers, layerIndex, layerById, relationState) {
          let crossings = 0;

          if (layerIndex > 0) {
            crossings += countLayerPairCrossings(
              orderedLayers[layerIndex - 1],
              orderedLayers[layerIndex],
              layerIndex - 1,
              layerIndex,
              layerById,
              relationState,
            );
          }

          if (layerIndex < orderedLayers.length - 1) {
            crossings += countLayerPairCrossings(
              orderedLayers[layerIndex],
              orderedLayers[layerIndex + 1],
              layerIndex,
              layerIndex + 1,
              layerById,
              relationState,
            );
          }

          return crossings;
        }

        function countLayerPairCrossings(
          upperLayerIds,
          lowerLayerIds,
          upperLayerIndex,
          lowerLayerIndex,
          layerById,
          relationState,
        ) {
          if (!upperLayerIds || !lowerLayerIds || upperLayerIds.length === 0 || lowerLayerIds.length === 0) {
            return 0;
          }

          const upperOrderById = new Map(upperLayerIds.map((modelId, index) => [modelId, index]));
          const lowerOrderById = new Map(lowerLayerIds.map((modelId, index) => [modelId, index]));
          const edgePairs = [];

          upperLayerIds.forEach((upperId) => {
            Array.from(relationState.adjacencyById.get(upperId) || [])
              .filter((neighborId) =>
                layerById.get(neighborId) === lowerLayerIndex && lowerOrderById.has(neighborId),
              )
              .sort((left, right) => left.localeCompare(right))
              .forEach((lowerId) => {
                edgePairs.push({
                  lower: lowerOrderById.get(lowerId),
                  upper: upperOrderById.get(upperId),
                });
              });
          });

          if (edgePairs.length <= 1) {
            return 0;
          }

          edgePairs.sort((left, right) => left.upper - right.upper || left.lower - right.lower);
          let crossings = 0;

          for (let leftIndex = 0; leftIndex < edgePairs.length; leftIndex += 1) {
            for (let rightIndex = leftIndex + 1; rightIndex < edgePairs.length; rightIndex += 1) {
              const leftPair = edgePairs[leftIndex];
              const rightPair = edgePairs[rightIndex];

              if (leftPair.upper === rightPair.upper || leftPair.lower === rightPair.lower) {
                continue;
              }

              if (leftPair.lower > rightPair.lower) {
                crossings += 1;
              }
            }
          }

          return crossings;
        }

        function sweepLayerIdsByNeighborBarycenter(
          layerIds,
          layerIndex,
          orderedLayers,
          layerById,
          relationState,
          direction,
        ) {
          return layerIds.slice().sort((left, right) => {
            const leftScore = computeLayerSweepScore(
              left,
              layerIndex,
              orderedLayers,
              layerById,
              relationState,
              direction,
            );
            const rightScore = computeLayerSweepScore(
              right,
              layerIndex,
              orderedLayers,
              layerById,
              relationState,
              direction,
            );

            if (Number.isFinite(leftScore.primaryBarycenter) && Number.isFinite(rightScore.primaryBarycenter)) {
              if (leftScore.primaryBarycenter !== rightScore.primaryBarycenter) {
                return leftScore.primaryBarycenter - rightScore.primaryBarycenter;
              }
            } else if (Number.isFinite(leftScore.primaryBarycenter) !== Number.isFinite(rightScore.primaryBarycenter)) {
              return Number.isFinite(leftScore.primaryBarycenter) ? -1 : 1;
            }

            if (Number.isFinite(leftScore.secondaryBarycenter) && Number.isFinite(rightScore.secondaryBarycenter)) {
              if (leftScore.secondaryBarycenter !== rightScore.secondaryBarycenter) {
                return leftScore.secondaryBarycenter - rightScore.secondaryBarycenter;
              }
            } else if (Number.isFinite(leftScore.secondaryBarycenter) !== Number.isFinite(rightScore.secondaryBarycenter)) {
              return Number.isFinite(leftScore.secondaryBarycenter) ? -1 : 1;
            }

            if (leftScore.primaryWeight !== rightScore.primaryWeight) {
              return rightScore.primaryWeight - leftScore.primaryWeight;
            }

            return compareRelationNodePriority(
              left,
              right,
              relationState.adjacencyById,
              relationState.incomingById,
              relationState.outgoingById,
            );
          });
        }

        function computeLayerSweepScore(
          modelId,
          layerIndex,
          orderedLayers,
          layerById,
          relationState,
          direction,
        ) {
          const primaryBarycenter = computeNeighborLayerBarycenter(
            direction === "incoming" ? relationState.incomingById : relationState.outgoingById,
            modelId,
            layerIndex,
            orderedLayers,
            layerById,
            direction === "incoming"
              ? (neighborLayerIndex) => neighborLayerIndex < layerIndex
              : (neighborLayerIndex) => neighborLayerIndex > layerIndex,
          );
          const secondaryBarycenter = computeNeighborLayerBarycenter(
            direction === "incoming" ? relationState.outgoingById : relationState.incomingById,
            modelId,
            layerIndex,
            orderedLayers,
            layerById,
            direction === "incoming"
              ? (neighborLayerIndex) => neighborLayerIndex > layerIndex
              : (neighborLayerIndex) => neighborLayerIndex < layerIndex,
          );

          return {
            primaryBarycenter: primaryBarycenter.value,
            primaryWeight: primaryBarycenter.weight,
            secondaryBarycenter: secondaryBarycenter.value,
          };
        }

        function computeNeighborLayerBarycenter(
          neighborMap,
          modelId,
          layerIndex,
          orderedLayers,
          layerById,
          includeNeighborLayer,
        ) {
          let weightedSum = 0;
          let weight = 0;

          Array.from(neighborMap.get(modelId) || []).forEach((neighborId) => {
            const neighborLayerIndex = layerById.get(neighborId);
            if (
              neighborLayerIndex === undefined ||
              !includeNeighborLayer(neighborLayerIndex)
            ) {
              return;
            }

            const neighborLayer = orderedLayers[neighborLayerIndex];
            const neighborIndex = neighborLayer ? neighborLayer.indexOf(neighborId) : -1;
            if (neighborIndex < 0) {
              return;
            }

            const normalizedIndex =
              neighborLayer.length <= 1
                ? 0
                : neighborIndex / (neighborLayer.length - 1);
            const layerDistance = Math.max(1, Math.abs(layerIndex - neighborLayerIndex));
            const contributionWeight = 1 / layerDistance;

            weightedSum += normalizedIndex * contributionWeight;
            weight += contributionWeight;
          });

          return {
            value: weight > 0 ? weightedSum / weight : Number.POSITIVE_INFINITY,
            weight,
          };
        }

        function createOrderedRelationLayerIds(
          layerIds,
          layerIndex,
          layerById,
          relationState,
          previousLayerOrderById,
        ) {
          return layerIds.slice().sort((left, right) => {
            const leftScore = computeRelationLayerSortScore(
              left,
              layerIndex,
              layerById,
              relationState,
              previousLayerOrderById,
            );
            const rightScore = computeRelationLayerSortScore(
              right,
              layerIndex,
              layerById,
              relationState,
              previousLayerOrderById,
            );

            if (Number.isFinite(leftScore.barycenter) && Number.isFinite(rightScore.barycenter)) {
              if (leftScore.barycenter !== rightScore.barycenter) {
                return leftScore.barycenter - rightScore.barycenter;
              }
            } else if (Number.isFinite(leftScore.barycenter) !== Number.isFinite(rightScore.barycenter)) {
              return Number.isFinite(leftScore.barycenter) ? -1 : 1;
            }

            if (leftScore.outgoingWeight !== rightScore.outgoingWeight) {
              return rightScore.outgoingWeight - leftScore.outgoingWeight;
            }

            return compareRelationNodePriority(
              left,
              right,
              relationState.adjacencyById,
              relationState.incomingById,
              relationState.outgoingById,
            );
          });
        }

        function computeRelationLayerSortScore(
          modelId,
          layerIndex,
          layerById,
          relationState,
          previousLayerOrderById,
        ) {
          const incomingIndices = Array.from(relationState.incomingById.get(modelId) || [])
            .filter((neighborId) =>
              (layerById.get(neighborId) || 0) < layerIndex &&
              previousLayerOrderById.has(neighborId),
            )
            .map((neighborId) => previousLayerOrderById.get(neighborId));

          return {
            barycenter:
              incomingIndices.length > 0
                ? incomingIndices.reduce((sum, value) => sum + value, 0) / incomingIndices.length
                : Number.POSITIVE_INFINITY,
            outgoingWeight: relationState.outgoingById.get(modelId)?.size || 0,
          };
        }

        function describeComponentPlan(componentPlan, componentIds, relationState) {
          const edgeCount = createRelationEdgePairs(componentIds, relationState.adjacencyById).length;
          const nodeCount = componentIds.length;
          const densityScore = edgeCount / Math.max(nodeCount - 1, 1);

          return {
            ...componentPlan,
            densityScore,
            edgeCount,
            isDense: nodeCount >= 8 || densityScore >= 1.45,
            nodeCount,
          };
        }

        function placeRelationComponentPlans(componentPlans, config) {
          const positions = {};
          const maxShelfWidth = Math.max(config.margin + 1, config.maxShelfWidth || 0);
          const orderedComponentPlans = componentPlans
            .map((componentPlan, index) => ({
              componentPlan,
              index,
            }))
            .sort((left, right) =>
              Number(Boolean(right.componentPlan.isDense)) - Number(Boolean(left.componentPlan.isDense)) ||
              (right.componentPlan.densityScore || 0) - (left.componentPlan.densityScore || 0) ||
              (right.componentPlan.nodeCount || 0) - (left.componentPlan.nodeCount || 0) ||
              right.componentPlan.width * right.componentPlan.height -
                left.componentPlan.width * left.componentPlan.height ||
              left.index - right.index,
            )
            .map((entry) => entry.componentPlan);
          let cursorX = config.margin;
          let cursorY = config.margin;
          let shelfHeight = 0;

          orderedComponentPlans.forEach((componentPlan) => {
            const rowGap = componentPlan.isDense
              ? config.componentGapY * 1.35
              : config.componentGapY;
            if (componentPlan.isDense && cursorX > config.margin) {
              cursorX = config.margin;
              cursorY += shelfHeight + rowGap;
              shelfHeight = 0;
            }

            if (
              cursorX > config.margin &&
              cursorX + componentPlan.width > maxShelfWidth
            ) {
              cursorX = config.margin;
              cursorY += shelfHeight + rowGap;
              shelfHeight = 0;
            }

            for (const [modelId, localPosition] of Object.entries(componentPlan.positions)) {
              positions[modelId] = {
                x: round2(cursorX + localPosition.x),
                y: round2(cursorY + localPosition.y),
              };
            }

            cursorX += componentPlan.width + config.componentGapX;
            shelfHeight = Math.max(shelfHeight, componentPlan.height);

            if (componentPlan.isDense) {
              cursorX = config.margin;
              cursorY += shelfHeight + rowGap;
              shelfHeight = 0;
            }
          });

          return positions;
        }

        function groupTablesByKey(tableMetaList, getGroupKey) {
          return tableMetaList.reduce((groups, table) => {
            const key = getGroupKey(table);
            if (!groups.has(key)) {
              groups.set(key, []);
            }
            groups.get(key).push(table);
            return groups;
          }, new Map());
        }

        function createRelationAwareClusteredLayout(groupedTables, config) {
          const positions = {};
          const relationState = createRelationAwareClusterState(groupedTables);
          const orderedGroupKeys = createRelationAwareGroupOrder(relationState);
          const groupOrderIndexByKey = new Map(
            orderedGroupKeys.map((groupKey, index) => [groupKey, index]),
          );
          const maxShelfWidth = Math.max(config.margin + config.cellWidth, config.maxShelfWidth || 0);
          let cursorX = config.margin;
          let cursorY = config.margin;
          let shelfHeight = 0;

          for (const groupKey of orderedGroupKeys) {
            const group = groupedTables.get(groupKey) || [];
            if (!group.length) {
              continue;
            }

            const orderedTables = orderTablesWithinRelationGroup(
              group,
              relationState,
              groupKey,
              groupOrderIndexByKey,
            );
            const columns = computeRelationAwareGroupColumnCount(
              orderedTables.length,
              config.cellWidth,
              config.cellHeight,
              relationState.groupTotalWeightByKey.get(groupKey) || 0,
            );
            const rows = Math.ceil(orderedTables.length / columns);
            const groupWidth = columns * config.cellWidth;
            const groupHeight = rows * config.cellHeight;

            if (cursorX > config.margin && cursorX + groupWidth > maxShelfWidth) {
              cursorX = config.margin;
              cursorY += shelfHeight + config.clusterGapY;
              shelfHeight = 0;
            }

            orderedTables.forEach((table, index) => {
              positions[table.modelId] = {
                x: round2(cursorX + (index % columns) * config.cellWidth),
                y: round2(cursorY + Math.floor(index / columns) * config.cellHeight),
              };
            });

            cursorX += groupWidth + config.clusterGapX;
            shelfHeight = Math.max(shelfHeight, groupHeight);
          }

          return positions;
        }

        function createRelationAwareClusterState(groupedTables) {
          const groupKeys = Array.from(groupedTables.keys()).sort((left, right) => left.localeCompare(right));
          const groupWeightsByKey = new Map(
            groupKeys.map((groupKey) => [groupKey, new Map()]),
          );
          const groupTotalWeightByKey = new Map(
            groupKeys.map((groupKey) => [groupKey, 0]),
          );
          const modelToGroupKey = new Map();
          const modelWeightsById = new Map();

          for (const [groupKey, tables] of groupedTables.entries()) {
            for (const table of tables) {
              modelToGroupKey.set(table.modelId, groupKey);
            }
          }

          for (const edge of edgeMeta) {
            const sourceGroupKey = modelToGroupKey.get(edge.sourceModelId);
            const targetGroupKey = modelToGroupKey.get(edge.targetModelId);
            if (!sourceGroupKey || !targetGroupKey) {
              continue;
            }

            addWeightedNeighbor(modelWeightsById, edge.sourceModelId, edge.targetModelId, 1);
            addWeightedNeighbor(modelWeightsById, edge.targetModelId, edge.sourceModelId, 1);

            if (sourceGroupKey === targetGroupKey) {
              continue;
            }

            addWeightedNeighbor(groupWeightsByKey, sourceGroupKey, targetGroupKey, 1);
            addWeightedNeighbor(groupWeightsByKey, targetGroupKey, sourceGroupKey, 1);
            groupTotalWeightByKey.set(
              sourceGroupKey,
              (groupTotalWeightByKey.get(sourceGroupKey) || 0) + 1,
            );
            groupTotalWeightByKey.set(
              targetGroupKey,
              (groupTotalWeightByKey.get(targetGroupKey) || 0) + 1,
            );
          }

          return {
            groupedTables,
            groupKeys,
            groupTotalWeightByKey,
            groupWeightsByKey,
            modelToGroupKey,
            modelWeightsById,
          };
        }

        function addWeightedNeighbor(weightsBySource, sourceKey, targetKey, amount) {
          if (!weightsBySource.has(sourceKey)) {
            weightsBySource.set(sourceKey, new Map());
          }

          const neighbors = weightsBySource.get(sourceKey);
          neighbors.set(targetKey, (neighbors.get(targetKey) || 0) + amount);
        }

        function createRelationAwareGroupOrder(relationState) {
          const remaining = new Set(relationState.groupKeys);
          const ordered = [];

          while (remaining.size > 0) {
            const nextGroupKey = ordered.length === 0
              ? pickRelationAwareStartGroup(remaining, relationState)
              : pickRelationAwareNextGroup(remaining, ordered, relationState);
            const insertionIndex = pickBestGroupInsertionIndex(
              ordered,
              nextGroupKey,
              relationState.groupWeightsByKey,
            );

            ordered.splice(insertionIndex, 0, nextGroupKey);
            remaining.delete(nextGroupKey);
          }

          return ordered;
        }

        function pickRelationAwareStartGroup(remaining, relationState) {
          return Array.from(remaining).sort((left, right) => {
            const weightDelta =
              (relationState.groupTotalWeightByKey.get(right) || 0) -
              (relationState.groupTotalWeightByKey.get(left) || 0);
            if (weightDelta !== 0) {
              return weightDelta;
            }

            const sizeDelta =
              (relationState.groupedTables.get(right)?.length || 0) -
              (relationState.groupedTables.get(left)?.length || 0);
            if (sizeDelta !== 0) {
              return sizeDelta;
            }

            return left.localeCompare(right);
          })[0];
        }

        function pickRelationAwareNextGroup(remaining, ordered, relationState) {
          const placed = new Set(ordered);

          return Array.from(remaining).sort((left, right) => {
            const placedWeightDelta =
              computeRelationWeightToPlaced(right, placed, relationState.groupWeightsByKey) -
              computeRelationWeightToPlaced(left, placed, relationState.groupWeightsByKey);
            if (placedWeightDelta !== 0) {
              return placedWeightDelta;
            }

            const totalWeightDelta =
              (relationState.groupTotalWeightByKey.get(right) || 0) -
              (relationState.groupTotalWeightByKey.get(left) || 0);
            if (totalWeightDelta !== 0) {
              return totalWeightDelta;
            }

            const sizeDelta =
              (relationState.groupedTables.get(right)?.length || 0) -
              (relationState.groupedTables.get(left)?.length || 0);
            if (sizeDelta !== 0) {
              return sizeDelta;
            }

            return left.localeCompare(right);
          })[0];
        }

        function computeRelationWeightToPlaced(groupKey, placed, groupWeightsByKey) {
          const neighbors = groupWeightsByKey.get(groupKey);
          if (!neighbors) {
            return 0;
          }

          let total = 0;
          for (const [neighborKey, weight] of neighbors.entries()) {
            if (placed.has(neighborKey)) {
              total += weight;
            }
          }

          return total;
        }

        function pickBestGroupInsertionIndex(ordered, groupKey, groupWeightsByKey) {
          if (ordered.length === 0) {
            return 0;
          }

          let bestCost = Number.POSITIVE_INFINITY;
          let bestIndex = ordered.length;

          for (let index = 0; index <= ordered.length; index += 1) {
            let cost = 0;

            for (let otherIndex = 0; otherIndex < ordered.length; otherIndex += 1) {
              const otherGroupKey = ordered[otherIndex];
              const distance = Math.abs(index - (otherIndex >= index ? otherIndex + 1 : otherIndex));
              cost += getGroupRelationWeight(groupWeightsByKey, groupKey, otherGroupKey) * distance;
            }

            if (cost < bestCost) {
              bestCost = cost;
              bestIndex = index;
            }
          }

          return bestIndex;
        }

        function getGroupRelationWeight(groupWeightsByKey, leftKey, rightKey) {
          return groupWeightsByKey.get(leftKey)?.get(rightKey) || 0;
        }

        function orderTablesWithinRelationGroup(group, relationState, groupKey, groupOrderIndexByKey) {
          return group.slice().sort((left, right) => {
            const leftScore = scoreTableWithinRelationGroup(
              left.modelId,
              relationState,
              groupKey,
              groupOrderIndexByKey,
            );
            const rightScore = scoreTableWithinRelationGroup(
              right.modelId,
              relationState,
              groupKey,
              groupOrderIndexByKey,
            );
            const leftFinite = Number.isFinite(leftScore.externalBarycenter);
            const rightFinite = Number.isFinite(rightScore.externalBarycenter);

            if (leftFinite && rightFinite && leftScore.externalBarycenter !== rightScore.externalBarycenter) {
              return leftScore.externalBarycenter - rightScore.externalBarycenter;
            }
            if (leftFinite !== rightFinite) {
              return leftFinite ? -1 : 1;
            }
            if (leftScore.externalWeight !== rightScore.externalWeight) {
              return rightScore.externalWeight - leftScore.externalWeight;
            }
            if (leftScore.internalWeight !== rightScore.internalWeight) {
              return rightScore.internalWeight - leftScore.internalWeight;
            }
            if (leftScore.totalWeight !== rightScore.totalWeight) {
              return rightScore.totalWeight - leftScore.totalWeight;
            }

            return left.modelId.localeCompare(right.modelId);
          });
        }

        function scoreTableWithinRelationGroup(modelId, relationState, ownGroupKey, groupOrderIndexByKey) {
          const neighbors = relationState.modelWeightsById.get(modelId);
          if (!neighbors) {
            return {
              externalBarycenter: Number.POSITIVE_INFINITY,
              externalWeight: 0,
              internalWeight: 0,
              totalWeight: 0,
            };
          }

          let externalWeightedIndex = 0;
          let externalWeight = 0;
          let internalWeight = 0;
          let totalWeight = 0;

          for (const [neighborModelId, weight] of neighbors.entries()) {
            totalWeight += weight;
            const neighborGroupKey = relationState.modelToGroupKey.get(neighborModelId);
            if (!neighborGroupKey) {
              continue;
            }

            if (neighborGroupKey === ownGroupKey) {
              internalWeight += weight;
              continue;
            }

            externalWeight += weight;
            externalWeightedIndex += (groupOrderIndexByKey.get(neighborGroupKey) || 0) * weight;
          }

          return {
            externalBarycenter:
              externalWeight > 0
                ? externalWeightedIndex / externalWeight
                : Number.POSITIVE_INFINITY,
            externalWeight,
            internalWeight,
            totalWeight,
          };
        }

        function computeRelationAwareGroupColumnCount(count, cellWidth, cellHeight, groupWeight) {
          const baseColumns = computeGridColumnCount(count, cellWidth - 48, cellHeight - 28);
          const weightBoost =
            (groupWeight >= 10 ? 1 : 0) +
            (groupWeight >= 28 ? 1 : 0);

          return Math.max(1, Math.min(count, baseColumns + weightBoost));
        }

        function createHierarchicalLayout(tableMetaList, tuning = createLayoutTuning()) {
          const relationState = getRelationLayoutState(tableMetaList);

          if (hasRelationEdgesInLayout(relationState)) {
            const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
            const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);

            return createLayeredRelationLayout(
              tableMetaList,
              createRelationLayoutConfig(tableMetaList, {
                componentGapX: Math.max(300, maxWidth * 1.2),
                componentGapY: 250,
                layerGapX: maxWidth + 280,
                layerGapY: maxHeight + 172,
                margin: 112,
                orientation: "horizontal",
                ringStep: 0,
                sweepIterations: 8,
              }, tuning),
            );
          }

          return createSeededHierarchicalLayout(tableMetaList, relationState, tuning);
        }

        function hasRelationEdgesInLayout(relationState) {
          for (const neighbors of relationState.adjacencyById.values()) {
            if (neighbors.size > 0) {
              return true;
            }
          }

          return false;
        }

        function createSeededHierarchicalLayout(tableMetaList, relationState, tuning = createLayoutTuning()) {
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const worldWidthBudget = getCanvasWorldWidthBudget(tableMetaList, maxWidth, tuning);
          const componentPlans = relationState.components.map((componentIds) =>
            describeComponentPlan(
              createExpandedHierarchicalComponentPlan(componentIds, relationState, tuning),
              componentIds,
              relationState,
            ),
          );

          return placeRelationComponentPlans(componentPlans, {
            componentGapX: Math.max(280, maxWidth * 1.25) * tuning.nodeSpacing,
            componentGapY: 240 * tuning.nodeSpacing,
            margin: 112 * tuning.nodeSpacing,
            maxShelfWidth: Math.max(
              1320 * tuning.nodeSpacing,
              worldWidthBudget,
              Math.sqrt(Math.max(tableMetaList.length, 1)) *
                Math.max(1, maxWidth + 180 * tuning.nodeSpacing) *
                2.25,
            ),
          });
        }

        function createExpandedHierarchicalComponentPlan(
          componentIds,
          relationState,
          tuning = createLayoutTuning(),
        ) {
          const columnsByBaseX = new Map();
          const orderedBaseXs = [];

          componentIds.forEach((modelId) => {
            const table = relationState.tableById.get(modelId);
            const baseX = round2(table.basePosition?.x || 0);

            if (!columnsByBaseX.has(baseX)) {
              columnsByBaseX.set(baseX, []);
              orderedBaseXs.push(baseX);
            }

            columnsByBaseX.get(baseX).push(modelId);
          });

          orderedBaseXs.sort((left, right) => left - right);

          const averageWidth =
            componentIds.reduce((sum, modelId) => sum + relationState.tableById.get(modelId).width, 0) /
            Math.max(componentIds.length, 1);
          const averageHeight =
            componentIds.reduce((sum, modelId) => sum + relationState.tableById.get(modelId).height, 0) /
            Math.max(componentIds.length, 1);
          const columnGap = Math.max(240, averageWidth * 1.15) * tuning.nodeSpacing;
          const rowGap = Math.max(108, averageHeight * 0.95) * tuning.nodeSpacing;
          const columnPlans = orderedBaseXs.map((baseX) => {
            const orderedIds = (columnsByBaseX.get(baseX) || [])
              .slice()
              .sort((left, right) => {
                const leftTable = relationState.tableById.get(left);
                const rightTable = relationState.tableById.get(right);

                return (
                  (leftTable.basePosition?.y || 0) - (rightTable.basePosition?.y || 0) ||
                  left.localeCompare(right)
                );
              });
            const width = orderedIds.reduce(
              (largest, modelId) => Math.max(largest, relationState.tableById.get(modelId).width),
              0,
            );
            const height = orderedIds.reduce((sum, modelId, index) => {
              const table = relationState.tableById.get(modelId);
              return sum + table.height + (index > 0 ? rowGap : 0);
            }, 0);

            return {
              height,
              orderedIds,
              width,
            };
          });
          const componentHeight = Math.max(1, ...columnPlans.map((columnPlan) => columnPlan.height));
          const localPositions = {};
          let cursorX = 0;

          columnPlans.forEach((columnPlan) => {
            let cursorY = Math.max(0, (componentHeight - columnPlan.height) / 2);

            columnPlan.orderedIds.forEach((modelId, index) => {
              const table = relationState.tableById.get(modelId);

              localPositions[modelId] = {
                x: round2(cursorX + Math.max(0, (columnPlan.width - table.width) / 2)),
                y: round2(cursorY),
              };
              cursorY += table.height;
              if (index < columnPlan.orderedIds.length - 1) {
                cursorY += rowGap;
              }
            });

            cursorX += columnPlan.width + columnGap;
          });

          return normalizeLocalComponentPositions(localPositions, relationState.tableById);
        }

        function findSegments(points) {
          const segments = [];

          for (let index = 1; index < points.length; index += 1) {
            segments.push({
              end: points[index],
              start: points[index - 1],
            });
          }

          return segments;
        }

        function getBasePosition(modelId) {
          const layouts = getResolvedBaseLayouts();

          return layouts.active[modelId] || layouts.fallback[modelId] || { x: 0, y: 0 };
        }

        function getCenter(position, table) {
          return {
            x: round2(position.x + table.width / 2),
            y: round2(position.y + table.height / 2),
          };
        }

        function getCurrentPosition(modelId) {
          const options = getTableOptions(state, modelId);
          return options.manualPosition || getBasePosition(modelId);
        }

        function findTableAtCanvasPoint(event) {
          const point = toWorldPoint(event);
          const orderedTables = renderModel.modelCatalogMode
            ? getCatalogTableIdsNearPoint(point)
                .map((modelId) => tableMetaById.get(modelId))
                .filter(Boolean)
                .reverse()
            : Array.from(tableMetaById.values()).reverse();

          return orderedTables.find((table) => {
            if (!isVisibleModel(table.modelId)) {
              return false;
            }

            const position = getCurrentPosition(table.modelId);
            return (
              point.x >= position.x &&
              point.x <= position.x + table.width &&
              point.y >= position.y &&
              point.y <= position.y + table.height
            );
          });
        }

        function isPointAtSegmentEndpoint(point, segment) {
          return (
            samePoint(point, segment.start) ||
            samePoint(point, segment.end)
          );
        }

        function normalizePoints(points) {
          return points.filter((point, index) => {
            if (index === 0) {
              return true;
            }

            return !samePoint(point, points[index - 1]);
          });
        }

        function buildOrthogonalPath(sourcePosition, sourceTable, targetPosition, targetTable) {
          const sourceCenter = getCenter(sourcePosition, sourceTable);
          const targetCenter = getCenter(targetPosition, targetTable);
          const deltaX = targetCenter.x - sourceCenter.x;
          const deltaY = targetCenter.y - sourceCenter.y;

          if (Math.abs(deltaX) >= Math.abs(deltaY)) {
            const start = {
              x: deltaX >= 0 ? round2(sourcePosition.x + sourceTable.width) : round2(sourcePosition.x),
              y: sourceCenter.y,
            };
            const end = {
              x: deltaX >= 0 ? round2(targetPosition.x) : round2(targetPosition.x + targetTable.width),
              y: targetCenter.y,
            };
            const midX = round2((start.x + end.x) / 2);

            return normalizePoints([
              start,
              { x: midX, y: start.y },
              { x: midX, y: end.y },
              end,
            ]);
          }

          const start = {
            x: sourceCenter.x,
            y: deltaY >= 0 ? round2(sourcePosition.y + sourceTable.height) : round2(sourcePosition.y),
          };
          const end = {
            x: targetCenter.x,
            y: deltaY >= 0 ? round2(targetPosition.y) : round2(targetPosition.y + targetTable.height),
          };
          const midY = round2((start.y + end.y) / 2);

          return normalizePoints([
            start,
            { x: start.x, y: midY },
            { x: end.x, y: midY },
            end,
          ]);
        }

        function routeVisibleEdgesWithPorts(edgeEntries, routingContext) {
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

          routes.forEach((route) => {
            route.start = getCatalogPortPoint(
              route.entry.sourcePosition,
              route.entry.sourceTable,
              route.sourceSide,
              route.sourceRef.portIndex || 0,
              route.sourceRef.portCount || 1,
            );
            route.end = getCatalogPortPoint(
              route.entry.targetPosition,
              route.entry.targetTable,
              route.targetSide,
              route.targetRef.portIndex || 0,
              route.targetRef.portCount || 1,
            );
          });

          const bundleDescriptorsByEdgeId = spaceVisibleEdgeBundleDescriptors(routes);
          const occupiedRects = collectVisibleRoutingRects(
            edgeEntries,
            routingContext && routingContext.routingRects,
          );
          const routedSegments = [];

          return routes
            .slice()
            .sort(compareVisibleRoutesForRouting)
            .map((route) => {
              const bundleDescriptor = bundleDescriptorsByEdgeId.get(route.entry.meta.edgeId);
              const points = buildObstacleAwarePathFromPorts(
                route.start,
                route.sourceSide,
                route.end,
                route.targetSide,
                bundleDescriptor,
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

        function compareVisibleRoutesForRouting(left, right) {
          const leftSpan = Math.abs(left.start.x - left.end.x) + Math.abs(left.start.y - left.end.y);
          const rightSpan = Math.abs(right.start.x - right.end.x) + Math.abs(right.start.y - right.end.y);
          const provenanceDelta =
            visibleRouteProvenanceRank(left.entry.meta.provenance) -
            visibleRouteProvenanceRank(right.entry.meta.provenance);

          return (
            provenanceDelta ||
            rightSpan - leftSpan ||
            left.entry.meta.edgeId.localeCompare(right.entry.meta.edgeId)
          );
        }

        function visibleRouteProvenanceRank(provenance) {
          return provenance === "declared" ? 0 : 1;
        }

        function collectVisibleRoutingRects(edgeEntries, routingRectsOverride) {
          if (Array.isArray(routingRectsOverride)) {
            return routingRectsOverride;
          }

          if (typeof tableMetaById !== "undefined" && typeof isVisibleModel === "function") {
            return Array.from(tableMetaById.values())
              .filter((table) => isVisibleModel(table.modelId))
              .map((table) => {
                const position = getCurrentPosition(table.modelId);

                return {
                  maxX: round2(position.x + table.width),
                  maxY: round2(position.y + table.height),
                  minX: round2(position.x),
                  minY: round2(position.y),
                  modelId: table.modelId,
                };
              });
          }

          const rectsByModelId = new Map();

          (edgeEntries || []).forEach((entry) => {
            [
              [entry.meta.sourceModelId, entry.sourcePosition, entry.sourceTable],
              [entry.meta.targetModelId, entry.targetPosition, entry.targetTable],
            ].forEach(([modelId, position, table]) => {
              if (!table || !position || rectsByModelId.has(modelId)) {
                return;
              }

              rectsByModelId.set(modelId, {
                maxX: round2(position.x + table.width),
                maxY: round2(position.y + table.height),
                minX: round2(position.x),
                minY: round2(position.y),
                modelId,
              });
            });
          });

          return Array.from(rectsByModelId.values());
        }

        function spaceVisibleEdgeBundleDescriptors(routes) {
          const descriptors = routes
            .map((route) => createVisibleEdgeBundleDescriptor(route))
            .filter(Boolean);
          const descriptorsByEdgeId = new Map();

          for (const orientation of ["vertical", "horizontal"]) {
            const orientedDescriptors = descriptors
              .filter((descriptor) => descriptor.orientation === orientation)
              .sort(compareVisibleEdgeBundleDescriptors);
            const placedDescriptors = [];

            orientedDescriptors.forEach((descriptor) => {
              const trunkCoordinate = pickVisibleEdgeBundleTrunkCoordinate(
                descriptor,
                placedDescriptors,
              );
              const spacedDescriptor = {
                ...descriptor,
                trunkCoordinate,
              };

              descriptorsByEdgeId.set(descriptor.edgeId, spacedDescriptor);
              placedDescriptors.push(spacedDescriptor);
            });
          }

          return descriptorsByEdgeId;
        }

        function createVisibleEdgeBundleDescriptor(route) {
          const tuning = createLayoutTuning();
          const detourDistance = round2(88 * tuning.edgeDetour);
          const rangeExtension = round2(64 * tuning.edgeDetour);
          const sourceHorizontal = route.sourceSide === "left" || route.sourceSide === "right";
          const targetHorizontal = route.targetSide === "left" || route.targetSide === "right";
          const sourceExit = offsetPointBySide(route.start, route.sourceSide, 24);
          const targetExit = offsetPointBySide(route.end, route.targetSide, 24);
          const provenance = route.entry.meta.provenance;

          if (sourceHorizontal && targetHorizontal) {
            const minExit = Math.min(sourceExit.x, targetExit.x);
            const maxExit = Math.max(sourceExit.x, targetExit.x);
            const laneBiasSign = getVisibleBundleBiasSign("vertical", route.sourceSide, route.targetSide);
            let preferredCoordinate = round2((sourceExit.x + targetExit.x) / 2);
            let minCoordinate = round2(minExit + 18);
            let maxCoordinate = round2(maxExit - 18);

            if (provenance === "derived_reverse") {
              const reverseRange = createReverseVisibleBundleRange(
                minExit,
                maxExit,
                laneBiasSign,
                detourDistance,
                rangeExtension,
              );

              minCoordinate = reverseRange.minCoordinate;
              maxCoordinate = reverseRange.maxCoordinate;
              preferredCoordinate = reverseRange.preferredCoordinate;
            }

            if (maxCoordinate < minCoordinate) {
              minCoordinate = round2(preferredCoordinate - 72);
              maxCoordinate = round2(preferredCoordinate + 72);
            }

            return {
              edgeId: route.entry.meta.edgeId,
              laneBiasSign,
              maxCoordinate,
              minCoordinate,
              orientation: "vertical",
              preferredCoordinate,
              provenance,
              spanMax: Math.max(sourceExit.y, targetExit.y),
              spanMin: Math.min(sourceExit.y, targetExit.y),
            };
          }

          if (!sourceHorizontal && !targetHorizontal) {
            const minExit = Math.min(sourceExit.y, targetExit.y);
            const maxExit = Math.max(sourceExit.y, targetExit.y);
            const laneBiasSign = getVisibleBundleBiasSign("horizontal", route.sourceSide, route.targetSide);
            let preferredCoordinate = round2((sourceExit.y + targetExit.y) / 2);
            let minCoordinate = round2(minExit + 18);
            let maxCoordinate = round2(maxExit - 18);

            if (provenance === "derived_reverse") {
              const reverseRange = createReverseVisibleBundleRange(
                minExit,
                maxExit,
                laneBiasSign,
                detourDistance,
                rangeExtension,
              );

              minCoordinate = reverseRange.minCoordinate;
              maxCoordinate = reverseRange.maxCoordinate;
              preferredCoordinate = reverseRange.preferredCoordinate;
            }

            if (maxCoordinate < minCoordinate) {
              minCoordinate = round2(preferredCoordinate - 72);
              maxCoordinate = round2(preferredCoordinate + 72);
            }

            return {
              edgeId: route.entry.meta.edgeId,
              laneBiasSign,
              maxCoordinate,
              minCoordinate,
              orientation: "horizontal",
              preferredCoordinate,
              provenance,
              spanMax: Math.max(sourceExit.x, targetExit.x),
              spanMin: Math.min(sourceExit.x, targetExit.x),
            };
          }

          return undefined;
        }

        function getVisibleBundleBiasSign(orientation, sourceSide, targetSide) {
          if (orientation === "vertical") {
            return sourceSide === "left" || targetSide === "left" ? -1 : 1;
          }

          return sourceSide === "top" || targetSide === "top" ? -1 : 1;
        }

        function createReverseVisibleBundleRange(
          minExit,
          maxExit,
          laneBiasSign,
          detourDistance,
          rangeExtension,
        ) {
          if (laneBiasSign < 0) {
            return {
              maxCoordinate: round2(minExit - 24),
              minCoordinate: round2(minExit - detourDistance - rangeExtension),
              preferredCoordinate: round2(minExit - detourDistance),
            };
          }

          return {
            maxCoordinate: round2(maxExit + detourDistance + rangeExtension),
            minCoordinate: round2(maxExit + 24),
            preferredCoordinate: round2(maxExit + detourDistance),
          };
        }

        function compareVisibleEdgeBundleDescriptors(left, right) {
          const spanCenterDelta =
            round2((left.spanMin + left.spanMax) / 2) -
            round2((right.spanMin + right.spanMax) / 2);

          return (
            visibleRouteProvenanceRank(left.provenance) - visibleRouteProvenanceRank(right.provenance) ||
            left.preferredCoordinate - right.preferredCoordinate ||
            spanCenterDelta ||
            (right.spanMax - right.spanMin) - (left.spanMax - left.spanMin) ||
            left.edgeId.localeCompare(right.edgeId)
          );
        }

        function pickVisibleEdgeBundleTrunkCoordinate(descriptor, placedDescriptors) {
          const tuning = createLayoutTuning();
          const overlappingDescriptors = placedDescriptors.filter((candidate) =>
            doCatalogBundleSpansOverlap(
              descriptor.spanMin,
              descriptor.spanMax,
              candidate.spanMin,
              candidate.spanMax,
            ),
          );
          const minSeparation = round2(
            (descriptor.provenance === "derived_reverse" ? 34 : 28) * tuning.edgeDetour,
          );
          const step = round2(24 * tuning.edgeDetour);
          const maxLaneOffset = descriptor.provenance === "derived_reverse" ? 12 : 10;

          for (const offset of createVisibleBundleCandidateOffsets(
            descriptor,
            step,
            maxLaneOffset,
          )) {
            const candidateCoordinate = clampCatalogBundleCoordinate(
              descriptor.preferredCoordinate + offset,
              descriptor.minCoordinate,
              descriptor.maxCoordinate,
            );
            const hasConflict = overlappingDescriptors.some((candidate) =>
              Math.abs(candidateCoordinate - candidate.trunkCoordinate) < minSeparation,
            );

            if (!hasConflict) {
              return candidateCoordinate;
            }
          }

          return clampCatalogBundleCoordinate(
            descriptor.preferredCoordinate,
            descriptor.minCoordinate,
            descriptor.maxCoordinate,
          );
        }

        function createVisibleBundleCandidateOffsets(descriptor, step, maxLaneOffset) {
          if (descriptor.laneBiasSign !== -1 && descriptor.laneBiasSign !== 1) {
            return createCatalogBundleCandidateOffsets(step, maxLaneOffset);
          }

          const offsets = [0];

          for (let offsetIndex = 1; offsetIndex <= maxLaneOffset; offsetIndex += 1) {
            offsets.push(step * offsetIndex * descriptor.laneBiasSign);
          }

          for (let offsetIndex = 1; offsetIndex <= maxLaneOffset; offsetIndex += 1) {
            offsets.push(step * offsetIndex * -descriptor.laneBiasSign);
          }

          return Array.from(new Set(offsets.map((offset) => round2(offset))));
        }

        function routeCatalogEdgesWithPorts(edgeEntries, routingContext) {
          const endpointRefsByKey = new Map();
          const routes = [];
          const effectiveLayoutMode =
            routingContext && routingContext.layoutMode
              ? routingContext.layoutMode
              : state.layoutMode;
          const bundleState = effectiveLayoutMode === "clustered"
            ? createCatalogBundleState(edgeEntries)
            : undefined;

          edgeEntries.forEach((entry, edgeIndex) => {
            const sourceCenter = getCenter(entry.sourcePosition, entry.sourceTable);
            const targetCenter = getCenter(entry.targetPosition, entry.targetTable);
            const bundleRoute = bundleState
              ? getCatalogBundleRoute(
                  bundleState,
                  getCatalogClusterKey(entry.sourceTable),
                  getCatalogClusterKey(entry.targetTable),
                )
              : undefined;
            const sourceSide = bundleRoute
              ? bundleRoute.sourceSide
              : getPreferredConnectionSide(sourceCenter, targetCenter);
            const targetSide = bundleRoute
              ? bundleRoute.targetSide
              : getPreferredConnectionSide(targetCenter, sourceCenter);
            const sourceRef = {
              edgeIndex,
              endpoint: "source",
              peerCenter: bundleRoute ? bundleRoute.targetGroupCenter : targetCenter,
              side: sourceSide,
            };
            const targetRef = {
              edgeIndex,
              endpoint: "target",
              peerCenter: bundleRoute ? bundleRoute.sourceGroupCenter : sourceCenter,
              side: targetSide,
            };

            addCatalogEndpointRef(endpointRefsByKey, entry.meta.sourceModelId, sourceSide, sourceRef);
            addCatalogEndpointRef(endpointRefsByKey, entry.meta.targetModelId, targetSide, targetRef);
            routes.push({
              entry,
              bundleRoute,
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

          const occupiedRects = collectVisibleRoutingRects(
            edgeEntries,
            routingContext && routingContext.routingRects,
          );
          const routedSegments = [];

          return routes.map((route) => {
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
              route.bundleRoute,
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

        function createCatalogBundleState(edgeEntries) {
          const groupBoundsByKey = createCatalogGroupBoundsByKey(edgeEntries);
          const descriptorsByPairKey = new Map();

          edgeEntries.forEach((entry) => {
            const sourceGroupKey = getCatalogClusterKey(entry.sourceTable);
            const targetGroupKey = getCatalogClusterKey(entry.targetTable);
            if (sourceGroupKey === targetGroupKey) {
              return;
            }

            const pairKey = getCatalogGroupPairKey(sourceGroupKey, targetGroupKey);
            if (descriptorsByPairKey.has(pairKey)) {
              return;
            }

            const sourceBounds = groupBoundsByKey.get(sourceGroupKey);
            const targetBounds = groupBoundsByKey.get(targetGroupKey);
            if (!sourceBounds || !targetBounds) {
              return;
            }

            descriptorsByPairKey.set(
              pairKey,
              createCatalogBundleDescriptor(sourceBounds, targetBounds),
            );
          });

          return {
            descriptorsByPairKey: spaceCatalogBundleDescriptors(descriptorsByPairKey),
            groupBoundsByKey,
          };
        }

        function createCatalogGroupBoundsByKey(edgeEntries) {
          const groupBoundsByKey = new Map();

          edgeEntries.forEach((entry) => {
            addCatalogGroupBounds(
              groupBoundsByKey,
              getCatalogClusterKey(entry.sourceTable),
              entry.sourcePosition,
              entry.sourceTable,
            );
            addCatalogGroupBounds(
              groupBoundsByKey,
              getCatalogClusterKey(entry.targetTable),
              entry.targetPosition,
              entry.targetTable,
            );
          });

          return groupBoundsByKey;
        }

        function addCatalogGroupBounds(groupBoundsByKey, groupKey, position, table) {
          const minX = round2(position.x);
          const minY = round2(position.y);
          const maxX = round2(position.x + table.width);
          const maxY = round2(position.y + table.height);

          if (!groupBoundsByKey.has(groupKey)) {
            groupBoundsByKey.set(groupKey, {
              center: {
                x: round2((minX + maxX) / 2),
                y: round2((minY + maxY) / 2),
              },
              maxX,
              maxY,
              minX,
              minY,
            });
            return;
          }

          const current = groupBoundsByKey.get(groupKey);
          current.minX = Math.min(current.minX, minX);
          current.minY = Math.min(current.minY, minY);
          current.maxX = Math.max(current.maxX, maxX);
          current.maxY = Math.max(current.maxY, maxY);
          current.center = {
            x: round2((current.minX + current.maxX) / 2),
            y: round2((current.minY + current.maxY) / 2),
          };
        }

        function getCatalogGroupPairKey(leftKey, rightKey) {
          return leftKey < rightKey ? leftKey + "->" + rightKey : rightKey + "->" + leftKey;
        }

        function createCatalogBundleDescriptor(sourceBounds, targetBounds) {
          const deltaX = targetBounds.center.x - sourceBounds.center.x;
          const deltaY = targetBounds.center.y - sourceBounds.center.y;

          if (Math.abs(deltaX) >= Math.abs(deltaY)) {
            const leftBounds = deltaX >= 0 ? sourceBounds : targetBounds;
            const rightBounds = deltaX >= 0 ? targetBounds : sourceBounds;
            const preferredCoordinate = leftBounds.maxX <= rightBounds.minX
              ? round2((leftBounds.maxX + rightBounds.minX) / 2)
              : round2((sourceBounds.center.x + targetBounds.center.x) / 2);
            const hasGap = leftBounds.maxX + 48 <= rightBounds.minX;
            const minCoordinate = hasGap
              ? round2(leftBounds.maxX + 24)
              : round2(preferredCoordinate - 96);
            const maxCoordinate = hasGap
              ? round2(rightBounds.minX - 24)
              : round2(preferredCoordinate + 96);

            return {
              maxCoordinate: Math.max(minCoordinate, maxCoordinate),
              minCoordinate: Math.min(minCoordinate, maxCoordinate),
              orientation: "vertical",
              preferredCoordinate,
              spanMax: Math.max(sourceBounds.maxY, targetBounds.maxY),
              spanMin: Math.min(sourceBounds.minY, targetBounds.minY),
              trunkCoordinate: clampCatalogBundleCoordinate(
                preferredCoordinate,
                minCoordinate,
                maxCoordinate,
              ),
            };
          }

          const upperBounds = deltaY >= 0 ? sourceBounds : targetBounds;
          const lowerBounds = deltaY >= 0 ? targetBounds : sourceBounds;
          const preferredCoordinate = upperBounds.maxY <= lowerBounds.minY
            ? round2((upperBounds.maxY + lowerBounds.minY) / 2)
            : round2((sourceBounds.center.y + targetBounds.center.y) / 2);
          const hasGap = upperBounds.maxY + 48 <= lowerBounds.minY;
          const minCoordinate = hasGap
            ? round2(upperBounds.maxY + 24)
            : round2(preferredCoordinate - 96);
          const maxCoordinate = hasGap
            ? round2(lowerBounds.minY - 24)
            : round2(preferredCoordinate + 96);

          return {
            maxCoordinate: Math.max(minCoordinate, maxCoordinate),
            minCoordinate: Math.min(minCoordinate, maxCoordinate),
            orientation: "horizontal",
            preferredCoordinate,
            spanMax: Math.max(sourceBounds.maxX, targetBounds.maxX),
            spanMin: Math.min(sourceBounds.minX, targetBounds.minX),
            trunkCoordinate: clampCatalogBundleCoordinate(
              preferredCoordinate,
              minCoordinate,
              maxCoordinate,
            ),
          };
        }

        function spaceCatalogBundleDescriptors(descriptorsByPairKey) {
          const spacedDescriptorsByPairKey = new Map(descriptorsByPairKey);

          for (const orientation of ["vertical", "horizontal"]) {
            const descriptors = Array.from(descriptorsByPairKey.entries())
              .map(([pairKey, descriptor]) => ({
                pairKey,
                ...descriptor,
              }))
              .filter((descriptor) => descriptor.orientation === orientation)
              .sort(compareCatalogBundleDescriptors);
            const placedDescriptors = [];

            descriptors.forEach((descriptor) => {
              const trunkCoordinate = pickCatalogBundleTrunkCoordinate(
                descriptor,
                placedDescriptors,
              );
              const spacedDescriptor = {
                ...descriptor,
                trunkCoordinate,
              };

              spacedDescriptorsByPairKey.set(descriptor.pairKey, spacedDescriptor);
              placedDescriptors.push(spacedDescriptor);
            });
          }

          return spacedDescriptorsByPairKey;
        }

        function compareCatalogBundleDescriptors(left, right) {
          const spanCenterDelta =
            round2((left.spanMin + left.spanMax) / 2) -
            round2((right.spanMin + right.spanMax) / 2);

          return (
            left.preferredCoordinate - right.preferredCoordinate ||
            spanCenterDelta ||
            (right.spanMax - right.spanMin) - (left.spanMax - left.spanMin) ||
            left.pairKey.localeCompare(right.pairKey)
          );
        }

        function pickCatalogBundleTrunkCoordinate(descriptor, placedDescriptors) {
          const overlappingDescriptors = placedDescriptors.filter((candidate) =>
            doCatalogBundleSpansOverlap(
              descriptor.spanMin,
              descriptor.spanMax,
              candidate.spanMin,
              candidate.spanMax,
            ),
          );
          const minSeparation = 40;
          const step = 32;
          const maxLaneOffset = 8;

          for (const offset of createCatalogBundleCandidateOffsets(step, maxLaneOffset)) {
            const candidateCoordinate = clampCatalogBundleCoordinate(
              descriptor.preferredCoordinate + offset,
              descriptor.minCoordinate,
              descriptor.maxCoordinate,
            );
            const hasConflict = overlappingDescriptors.some((candidate) =>
              Math.abs(candidateCoordinate - candidate.trunkCoordinate) < minSeparation,
            );

            if (!hasConflict) {
              return candidateCoordinate;
            }
          }

          return clampCatalogBundleCoordinate(
            descriptor.preferredCoordinate,
            descriptor.minCoordinate,
            descriptor.maxCoordinate,
          );
        }

        function createCatalogBundleCandidateOffsets(step, maxLaneOffset) {
          const offsets = [0];

          for (let lane = 1; lane <= maxLaneOffset; lane += 1) {
            offsets.push(step * lane, -step * lane);
          }

          return offsets;
        }

        function doCatalogBundleSpansOverlap(leftMin, leftMax, rightMin, rightMax) {
          return leftMin <= rightMax && rightMin <= leftMax;
        }

        function clampCatalogBundleCoordinate(value, minCoordinate, maxCoordinate) {
          return round2(
            Math.min(Math.max(value, minCoordinate), maxCoordinate),
          );
        }

        function getCatalogBundleRoute(bundleState, sourceGroupKey, targetGroupKey) {
          if (!bundleState || sourceGroupKey === targetGroupKey) {
            return undefined;
          }

          const descriptor = bundleState.descriptorsByPairKey.get(
            getCatalogGroupPairKey(sourceGroupKey, targetGroupKey),
          );
          const sourceBounds = bundleState.groupBoundsByKey.get(sourceGroupKey);
          const targetBounds = bundleState.groupBoundsByKey.get(targetGroupKey);
          if (!descriptor || !sourceBounds || !targetBounds) {
            return undefined;
          }

          if (descriptor.orientation === "vertical") {
            const sourceOnLeft = sourceBounds.center.x <= targetBounds.center.x;
            return {
              orientation: descriptor.orientation,
              sourceGroupCenter: sourceBounds.center,
              sourceSide: sourceOnLeft ? "right" : "left",
              targetGroupCenter: targetBounds.center,
              targetSide: sourceOnLeft ? "left" : "right",
              trunkCoordinate: descriptor.trunkCoordinate,
            };
          }

          const sourceOnTop = sourceBounds.center.y <= targetBounds.center.y;
          return {
            orientation: descriptor.orientation,
            sourceGroupCenter: sourceBounds.center,
            sourceSide: sourceOnTop ? "bottom" : "top",
            targetGroupCenter: targetBounds.center,
            targetSide: sourceOnTop ? "top" : "bottom",
            trunkCoordinate: descriptor.trunkCoordinate,
          };
        }

        function addCatalogEndpointRef(endpointRefsByKey, modelId, side, ref) {
          const key = modelId + ":" + side;
          if (!endpointRefsByKey.has(key)) {
            endpointRefsByKey.set(key, []);
          }
          endpointRefsByKey.get(key).push(ref);
        }

        function compareCatalogEndpointRefs(left, right) {
          if (left.side === "left" || left.side === "right") {
            return (
              left.peerCenter.y - right.peerCenter.y ||
              left.peerCenter.x - right.peerCenter.x ||
              left.edgeIndex - right.edgeIndex
            );
          }

          return (
            left.peerCenter.x - right.peerCenter.x ||
            left.peerCenter.y - right.peerCenter.y ||
            left.edgeIndex - right.edgeIndex
          );
        }

        function getPreferredConnectionSide(originCenter, peerCenter) {
          const deltaX = peerCenter.x - originCenter.x;
          const deltaY = peerCenter.y - originCenter.y;

          if (Math.abs(deltaX) >= Math.abs(deltaY)) {
            return deltaX >= 0 ? "right" : "left";
          }

          return deltaY >= 0 ? "bottom" : "top";
        }

        function getCatalogPortPoint(position, table, side, portIndex, portCount) {
          const inset = 12;
          const horizontal = side === "top" || side === "bottom";
          const length = horizontal ? table.width : table.height;
          const usableLength = Math.max(1, length - inset * 2);
          const offset = round2(inset + usableLength * ((portIndex + 1) / (portCount + 1)));

          switch (side) {
            case "left":
              return { x: round2(position.x), y: round2(position.y + offset) };
            case "right":
              return { x: round2(position.x + table.width), y: round2(position.y + offset) };
            case "top":
              return { x: round2(position.x + offset), y: round2(position.y) };
            case "bottom":
            default:
              return { x: round2(position.x + offset), y: round2(position.y + table.height) };
          }
        }

        function buildOrthogonalPathFromPorts(start, sourceSide, end, targetSide) {
          const sourceExit = offsetPointBySide(start, sourceSide, 24);
          const targetExit = offsetPointBySide(end, targetSide, 24);
          const points = [start, sourceExit];
          const sourceHorizontal = sourceSide === "left" || sourceSide === "right";
          const targetHorizontal = targetSide === "left" || targetSide === "right";

          if (sourceHorizontal && targetHorizontal) {
            const midX = round2((sourceExit.x + targetExit.x) / 2);
            points.push({ x: midX, y: sourceExit.y }, { x: midX, y: targetExit.y });
          } else if (!sourceHorizontal && !targetHorizontal) {
            const midY = round2((sourceExit.y + targetExit.y) / 2);
            points.push({ x: sourceExit.x, y: midY }, { x: targetExit.x, y: midY });
          } else if (sourceHorizontal) {
            points.push({ x: targetExit.x, y: sourceExit.y });
          } else {
            points.push({ x: sourceExit.x, y: targetExit.y });
          }

          points.push(targetExit, end);
          return normalizePoints(points);
        }

        function buildObstacleAwarePathFromPorts(
          start,
          sourceSide,
          end,
          targetSide,
          preferredBundleRoute,
          routeMeta,
          occupiedRects,
          routedSegments,
        ) {
          const sourceExit = offsetPointBySide(start, sourceSide, 24);
          const targetExit = offsetPointBySide(end, targetSide, 24);
          const candidatePaths = [];
          const xChannels = createRoutingChannelCandidates("x", sourceExit, targetExit, occupiedRects, routeMeta);
          const yChannels = createRoutingChannelCandidates("y", sourceExit, targetExit, occupiedRects, routeMeta);
          const shouldTryGridRouting =
            occupiedRects.length <= DENSE_GRAPH_GRID_ROUTING_MAX_TABLES &&
            routedSegments.length <= DENSE_GRAPH_GRID_ROUTING_MAX_SEGMENTS;
          const gridPath = shouldTryGridRouting
            ? buildGridRoutedPathFromPorts(
                start,
                sourceSide,
                end,
                targetSide,
                preferredBundleRoute,
                routeMeta,
                occupiedRects,
                routedSegments,
              )
            : undefined;

          if (preferredBundleRoute) {
            candidatePaths.push(
              buildBundledPathFromPorts(start, sourceSide, end, targetSide, preferredBundleRoute),
            );
          }

          candidatePaths.push(buildOrthogonalPathFromPorts(start, sourceSide, end, targetSide));
          xChannels.forEach((channel) => {
            candidatePaths.push(buildPathThroughVerticalChannel(start, sourceSide, end, targetSide, channel));
          });
          yChannels.forEach((channel) => {
            candidatePaths.push(buildPathThroughHorizontalChannel(start, sourceSide, end, targetSide, channel));
          });
          if (gridPath) {
            candidatePaths.push(gridPath);
          }

          let bestPoints = candidatePaths[0];
          let bestScore = Number.POSITIVE_INFINITY;

          candidatePaths.forEach((points) => {
            const score = scoreOrthogonalPath(
              points,
              occupiedRects,
              routedSegments,
              preferredBundleRoute,
              routeMeta,
            );

            if (score < bestScore) {
              bestPoints = points;
              bestScore = score;
            }
          });

          return bestPoints;
        }

        function buildGridRoutedPathFromPorts(
          start,
          sourceSide,
          end,
          targetSide,
          preferredBundleRoute,
          routeMeta,
          occupiedRects,
          routedSegments,
        ) {
          const tuning = createLayoutTuning();
          const sourceExit = offsetPointBySide(start, sourceSide, 24);
          const targetExit = offsetPointBySide(end, targetSide, 24);
          const clearance = round2((routeMeta?.provenance === "derived_reverse" ? 44 : 36) * tuning.edgeDetour);
          const routingRects = collectGridRoutingRects(sourceExit, targetExit, occupiedRects, clearance);
          const xValues = collectGridRoutingAxisValues("x", sourceExit, targetExit, preferredBundleRoute, routingRects, clearance);
          const yValues = collectGridRoutingAxisValues("y", sourceExit, targetExit, preferredBundleRoute, routingRects, clearance);
          const sourceKey = gridPointKey(sourceExit.x, sourceExit.y);
          const targetKey = gridPointKey(targetExit.x, targetExit.y);
          const pointByKey = new Map();

          xValues.forEach((x) => {
            yValues.forEach((y) => {
              const key = gridPointKey(x, y);
              pointByKey.set(key, { x, y });
            });
          });
          pointByKey.set(sourceKey, sourceExit);
          pointByKey.set(targetKey, targetExit);

          const xIndexByValue = new Map(xValues.map((value, index) => [value, index]));
          const yIndexByValue = new Map(yValues.map((value, index) => [value, index]));
          const open = [{
            cost: 0,
            direction: "none",
            key: sourceKey,
            point: sourceExit,
          }];
          const bestCostByState = new Map([[sourceKey + ":none", 0]]);
          const previousByState = new Map();
          let bestTargetStateKey = undefined;
          let guard = 0;

          while (open.length > 0 && guard < 8000) {
            guard += 1;
            open.sort((left, right) => left.cost - right.cost);
            const current = open.shift();
            const currentStateKey = current.key + ":" + current.direction;

            if (current.key === targetKey) {
              bestTargetStateKey = currentStateKey;
              break;
            }

            const neighbors = collectGridRoutingNeighbors(
              current.point,
              xValues,
              yValues,
              xIndexByValue,
              yIndexByValue,
              pointByKey,
            );

            neighbors.forEach((neighbor) => {
              if (!neighbor || samePoint(current.point, neighbor.point)) {
                return;
              }

              const segment = {
                end: neighbor.point,
                start: current.point,
              };
              if (!isGridRoutingSegmentClear(segment, routingRects, clearance)) {
                return;
              }

              const direction = segment.start.x === segment.end.x ? "vertical" : "horizontal";
              const turnPenalty =
                current.direction !== "none" && current.direction !== direction
                  ? 28 * tuning.edgeDetour
                  : 0;
              const interactionPenalty = computeRoutedSegmentInteractionPenalty(
                segment,
                routedSegments,
                routeMeta,
              );
              const stepCost =
                Math.abs(segment.end.x - segment.start.x) +
                Math.abs(segment.end.y - segment.start.y) +
                turnPenalty +
                interactionPenalty;
              const nextCost = current.cost + stepCost;
              const nextStateKey = neighbor.key + ":" + direction;

              if (nextCost >= (bestCostByState.get(nextStateKey) ?? Number.POSITIVE_INFINITY)) {
                return;
              }

              bestCostByState.set(nextStateKey, nextCost);
              previousByState.set(nextStateKey, currentStateKey);
              open.push({
                cost: nextCost,
                direction,
                key: neighbor.key,
                point: neighbor.point,
              });
            });
          }

          if (!bestTargetStateKey) {
            return undefined;
          }

          const gridPoints = [];
          let cursorStateKey = bestTargetStateKey;
          while (cursorStateKey) {
            const [pointKey] = cursorStateKey.split(":");
            const point = pointByKey.get(pointKey);
            if (point) {
              gridPoints.push(point);
            }
            cursorStateKey = previousByState.get(cursorStateKey);
          }
          gridPoints.reverse();

          if (gridPoints.length < 2) {
            return undefined;
          }

          return normalizePoints([start, ...gridPoints, end]);
        }

        function collectGridRoutingRects(sourceExit, targetExit, occupiedRects, clearance) {
          const minX = Math.min(sourceExit.x, targetExit.x) - clearance * 8;
          const maxX = Math.max(sourceExit.x, targetExit.x) + clearance * 8;
          const minY = Math.min(sourceExit.y, targetExit.y) - clearance * 8;
          const maxY = Math.max(sourceExit.y, targetExit.y) + clearance * 8;
          const midpoint = {
            x: (sourceExit.x + targetExit.x) / 2,
            y: (sourceExit.y + targetExit.y) / 2,
          };

          return occupiedRects
            .filter((rect) =>
              rect.maxX >= minX &&
              rect.minX <= maxX &&
              rect.maxY >= minY &&
              rect.minY <= maxY,
            )
            .sort((left, right) =>
              distanceBetweenPointAndRect(midpoint, left) -
                distanceBetweenPointAndRect(midpoint, right),
            )
            .slice(0, 72);
        }

        function collectGridRoutingAxisValues(
          axis,
          sourceExit,
          targetExit,
          preferredBundleRoute,
          routingRects,
          clearance,
        ) {
          const sourceValue = axis === "x" ? sourceExit.x : sourceExit.y;
          const targetValue = axis === "x" ? targetExit.x : targetExit.y;
          const values = [
            sourceValue,
            targetValue,
            round2((sourceValue + targetValue) / 2),
            round2(Math.min(sourceValue, targetValue) - clearance * 2),
            round2(Math.max(sourceValue, targetValue) + clearance * 2),
          ];

          if (
            preferredBundleRoute &&
            (
              (axis === "x" && preferredBundleRoute.orientation === "vertical") ||
              (axis === "y" && preferredBundleRoute.orientation === "horizontal")
            )
          ) {
            values.push(preferredBundleRoute.trunkCoordinate);
          }

          routingRects.forEach((rect) => {
            if (axis === "x") {
              values.push(
                round2(rect.minX - clearance),
                round2(rect.maxX + clearance),
                round2(rect.minX - clearance * 2.25),
                round2(rect.maxX + clearance * 2.25),
              );
            } else {
              values.push(
                round2(rect.minY - clearance),
                round2(rect.maxY + clearance),
                round2(rect.minY - clearance * 2.25),
                round2(rect.maxY + clearance * 2.25),
              );
            }
          });

          return Array.from(
            new Set(values.filter((value) => Number.isFinite(value)).map((value) => round2(value))),
          ).sort((left, right) => left - right);
        }

        function collectGridRoutingNeighbors(
          point,
          xValues,
          yValues,
          xIndexByValue,
          yIndexByValue,
          pointByKey,
        ) {
          const neighbors = [];
          const xIndex = xIndexByValue.get(point.x);
          const yIndex = yIndexByValue.get(point.y);

          if (xIndex !== undefined) {
            [xIndex - 1, xIndex + 1].forEach((candidateIndex) => {
              const x = xValues[candidateIndex];
              if (x === undefined) {
                return;
              }
              const key = gridPointKey(x, point.y);
              neighbors.push({
                key,
                point: pointByKey.get(key) || { x, y: point.y },
              });
            });
          }

          if (yIndex !== undefined) {
            [yIndex - 1, yIndex + 1].forEach((candidateIndex) => {
              const y = yValues[candidateIndex];
              if (y === undefined) {
                return;
              }
              const key = gridPointKey(point.x, y);
              neighbors.push({
                key,
                point: pointByKey.get(key) || { x: point.x, y },
              });
            });
          }

          return neighbors;
        }

        function isGridRoutingSegmentClear(segment, routingRects, clearance) {
          return routingRects.every((rect) =>
            !segmentIntersectsRect(segment, rect, Math.max(10, clearance * 0.72)),
          );
        }

        function computeRoutedSegmentInteractionPenalty(segment, routedSegments, routeMeta) {
          const reverseRoute = routeMeta?.provenance === "derived_reverse";
          let penalty = 0;

          routedSegments.forEach((existing) => {
            if (segmentsShareLane(segment, existing)) {
              penalty += reverseRoute ? 210 : 150;
            }

            const intersection = segmentIntersection(segment, existing);
            if (
              intersection &&
              !isPointAtSegmentEndpoint(intersection, segment) &&
              !isPointAtSegmentEndpoint(intersection, existing)
            ) {
              penalty += reverseRoute ? 420 : 360;
            }
          });

          return penalty;
        }

        function distanceBetweenPointAndRect(point, rect) {
          const dx = point.x < rect.minX
            ? rect.minX - point.x
            : point.x > rect.maxX
              ? point.x - rect.maxX
              : 0;
          const dy = point.y < rect.minY
            ? rect.minY - point.y
            : point.y > rect.maxY
              ? point.y - rect.maxY
              : 0;

          return Math.hypot(dx, dy);
        }

        function gridPointKey(x, y) {
          return round2(x) + "," + round2(y);
        }

        function createRoutingChannelCandidates(axis, sourceExit, targetExit, occupiedRects, routeMeta) {
          const tuning = createLayoutTuning();
          const rectGap = round2(42 * tuning.edgeDetour);
          const outerGap = round2(72 * tuning.edgeDetour);
          const farGap = round2(124 * tuning.edgeDetour);
          const mid = axis === "x"
            ? round2((sourceExit.x + targetExit.x) / 2)
            : round2((sourceExit.y + targetExit.y) / 2);
          const values = [mid];
          let minBound = Number.POSITIVE_INFINITY;
          let maxBound = Number.NEGATIVE_INFINITY;

          occupiedRects.forEach((rect) => {
            if (axis === "x") {
              values.push(round2(rect.minX - rectGap), round2(rect.maxX + rectGap));
              minBound = Math.min(minBound, rect.minX);
              maxBound = Math.max(maxBound, rect.maxX);
            } else {
              values.push(round2(rect.minY - rectGap), round2(rect.maxY + rectGap));
              minBound = Math.min(minBound, rect.minY);
              maxBound = Math.max(maxBound, rect.maxY);
            }
          });

          const sourceValue = axis === "x" ? sourceExit.x : sourceExit.y;
          const targetValue = axis === "x" ? targetExit.x : targetExit.y;
          values.push(
            round2(Math.min(sourceValue, targetValue) - outerGap),
            round2(Math.max(sourceValue, targetValue) + outerGap),
          );

          if (Number.isFinite(minBound) && Number.isFinite(maxBound)) {
            values.push(round2(minBound - outerGap), round2(maxBound + outerGap));
          }

          if (routeMeta?.provenance === "derived_reverse") {
            values.push(
              round2(Math.min(sourceValue, targetValue) - farGap),
              round2(Math.max(sourceValue, targetValue) + farGap),
            );
            if (Number.isFinite(minBound) && Number.isFinite(maxBound)) {
              values.push(round2(minBound - farGap), round2(maxBound + farGap));
            }
          }

          return Array.from(
            new Set(values.filter((value) => Number.isFinite(value)).map((value) => round2(value))),
          )
            .sort((left, right) =>
              Math.abs(left - mid) - Math.abs(right - mid) ||
              left - right,
            )
            .slice(0, 18);
        }

        function buildPathThroughVerticalChannel(start, sourceSide, end, targetSide, channelX) {
          const sourceExit = offsetPointBySide(start, sourceSide, 24);
          const targetExit = offsetPointBySide(end, targetSide, 24);

          return normalizePoints([
            start,
            sourceExit,
            { x: round2(channelX), y: sourceExit.y },
            { x: round2(channelX), y: targetExit.y },
            targetExit,
            end,
          ]);
        }

        function buildPathThroughHorizontalChannel(start, sourceSide, end, targetSide, channelY) {
          const sourceExit = offsetPointBySide(start, sourceSide, 24);
          const targetExit = offsetPointBySide(end, targetSide, 24);

          return normalizePoints([
            start,
            sourceExit,
            { x: sourceExit.x, y: round2(channelY) },
            { x: targetExit.x, y: round2(channelY) },
            targetExit,
            end,
          ]);
        }

        function scoreOrthogonalPath(
          points,
          occupiedRects,
          routedSegments,
          preferredBundleRoute,
          routeMeta,
        ) {
          const tuning = createLayoutTuning();
          const reverseRoute = routeMeta?.provenance === "derived_reverse";
          const segments = findSegments(points);
          let collisionCount = 0;
          let sharedLanePenalty = 0;
          let crossingPenalty = 0;
          let pathLength = 0;

          segments.forEach((segment) => {
            pathLength += Math.abs(segment.end.x - segment.start.x) + Math.abs(segment.end.y - segment.start.y);

            occupiedRects.forEach((rect) => {
              if (segmentIntersectsRect(segment, rect, 20)) {
                collisionCount += 1;
              }
            });

            routedSegments.forEach((existing) => {
              if (segmentsShareLane(segment, existing)) {
                sharedLanePenalty += 1;
              }

              const intersection = segmentIntersection(segment, existing);
              if (
                intersection &&
                !isPointAtSegmentEndpoint(intersection, segment) &&
                !isPointAtSegmentEndpoint(intersection, existing)
              ) {
                crossingPenalty += 1;
              }
            });
          });

          const bendPenalty = Math.max(0, points.length - 2) * 6;
          const preferencePenalty = preferredBundleRoute
            ? computeBundlePreferencePenalty(points, preferredBundleRoute)
            : 0;
          const clearancePenalty = computeObstacleClearancePenalty(
            segments,
            occupiedRects,
            reverseRoute
              ? 64 * tuning.edgeDetour
              : 48 * tuning.edgeDetour,
          );

          return (
            collisionCount * 10000 +
            crossingPenalty * (reverseRoute ? 260 : 220) +
            sharedLanePenalty * (reverseRoute ? 170 : 120) * tuning.edgeDetour +
            clearancePenalty +
            bendPenalty +
            pathLength * (reverseRoute ? 0.007 : 0.01) +
            preferencePenalty
          );
        }

        function computeObstacleClearancePenalty(segments, occupiedRects, targetClearance) {
          let penalty = 0;

          segments.forEach((segment) => {
            occupiedRects.forEach((rect) => {
              const distance = distanceBetweenSegmentAndRect(segment, rect);
              if (distance < targetClearance) {
                penalty += (targetClearance - distance) * 3.5;
              }
            });
          });

          return penalty;
        }

        function distanceBetweenSegmentAndRect(segment, rect) {
          const segmentMinX = Math.min(segment.start.x, segment.end.x);
          const segmentMaxX = Math.max(segment.start.x, segment.end.x);
          const segmentMinY = Math.min(segment.start.y, segment.end.y);
          const segmentMaxY = Math.max(segment.start.y, segment.end.y);
          const deltaX = segmentMaxX < rect.minX
            ? rect.minX - segmentMaxX
            : rect.maxX < segmentMinX
              ? segmentMinX - rect.maxX
              : 0;
          const deltaY = segmentMaxY < rect.minY
            ? rect.minY - segmentMaxY
            : rect.maxY < segmentMinY
              ? segmentMinY - rect.maxY
              : 0;

          return Math.hypot(deltaX, deltaY);
        }

        function computeBundlePreferencePenalty(points, preferredBundleRoute) {
          if (points.length < 4) {
            return 0;
          }

          if (preferredBundleRoute.orientation === "vertical") {
            const trunkSegment = findSegments(points).find((segment) => segment.start.x === segment.end.x);
            return trunkSegment
              ? Math.abs(trunkSegment.start.x - preferredBundleRoute.trunkCoordinate) * 0.2
              : 60;
          }

          const trunkSegment = findSegments(points).find((segment) => segment.start.y === segment.end.y);
          return trunkSegment
            ? Math.abs(trunkSegment.start.y - preferredBundleRoute.trunkCoordinate) * 0.2
            : 60;
        }

        function segmentIntersectsRect(segment, rect, padding) {
          const minX = rect.minX - padding;
          const maxX = rect.maxX + padding;
          const minY = rect.minY - padding;
          const maxY = rect.maxY + padding;

          if (segment.start.x === segment.end.x) {
            const x = segment.start.x;
            const fromY = Math.min(segment.start.y, segment.end.y);
            const toY = Math.max(segment.start.y, segment.end.y);

            return x >= minX && x <= maxX && toY >= minY && fromY <= maxY;
          }

          const y = segment.start.y;
          const fromX = Math.min(segment.start.x, segment.end.x);
          const toX = Math.max(segment.start.x, segment.end.x);

          return y >= minY && y <= maxY && toX >= minX && fromX <= maxX;
        }

        function segmentsShareLane(left, right) {
          const leftVertical = left.start.x === left.end.x;
          const rightVertical = right.start.x === right.end.x;

          if (leftVertical !== rightVertical) {
            return false;
          }

          if (leftVertical) {
            return left.start.x === right.start.x &&
              rangesOverlap(left.start.y, left.end.y, right.start.y, right.end.y, 10);
          }

          return left.start.y === right.start.y &&
            rangesOverlap(left.start.x, left.end.x, right.start.x, right.end.x, 10);
        }

        function rangesOverlap(leftStart, leftEnd, rightStart, rightEnd, minSharedLength) {
          const start = Math.max(Math.min(leftStart, leftEnd), Math.min(rightStart, rightEnd));
          const end = Math.min(Math.max(leftStart, leftEnd), Math.max(rightStart, rightEnd));

          return end - start >= minSharedLength;
        }

        function buildBundledPathFromPorts(start, sourceSide, end, targetSide, bundleRoute) {
          const sourceExit = offsetPointBySide(start, sourceSide, 24);
          const targetExit = offsetPointBySide(end, targetSide, 24);

          if (bundleRoute.orientation === "vertical") {
            return normalizePoints([
              start,
              sourceExit,
              { x: bundleRoute.trunkCoordinate, y: sourceExit.y },
              { x: bundleRoute.trunkCoordinate, y: targetExit.y },
              targetExit,
              end,
            ]);
          }

          return normalizePoints([
            start,
            sourceExit,
            { x: sourceExit.x, y: bundleRoute.trunkCoordinate },
            { x: targetExit.x, y: bundleRoute.trunkCoordinate },
            targetExit,
            end,
          ]);
        }

        function offsetPointBySide(point, side, distance) {
          switch (side) {
            case "left":
              return { x: round2(point.x - distance), y: point.y };
            case "right":
              return { x: round2(point.x + distance), y: point.y };
            case "top":
              return { x: point.x, y: round2(point.y - distance) };
            case "bottom":
            default:
              return { x: point.x, y: round2(point.y + distance) };
          }
        }

        function pointsToAttribute(points) {
          return points.map((point) => point.x + "," + point.y).join(" ");
        }

        function round2(value) {
          return Math.round(value * 100) / 100;
        }

        function samePoint(left, right) {
          return left.x === right.x && left.y === right.y;
        }

        function toWorldPoint(event) {
          const rect = drawingCanvas.getBoundingClientRect();
          const cssX = event.clientX - rect.left;
          const cssY = event.clientY - rect.top;

          return {
            x: round2((cssX - state.viewport.panX) / state.viewport.zoom),
            y: round2((cssY - state.viewport.panY) / state.viewport.zoom),
          };
        }

        function segmentIntersection(left, right) {
          const leftHorizontal = left.start.y === left.end.y;
          const rightHorizontal = right.start.y === right.end.y;

          if (leftHorizontal === rightHorizontal) {
            return undefined;
          }

          const horizontal = leftHorizontal ? left : right;
          const vertical = leftHorizontal ? right : left;
          const x = vertical.start.x;
          const y = horizontal.start.y;

          if (
            x < Math.min(horizontal.start.x, horizontal.end.x) ||
            x > Math.max(horizontal.start.x, horizontal.end.x) ||
            y < Math.min(vertical.start.y, vertical.end.y) ||
            y > Math.max(vertical.start.y, vertical.end.y)
          ) {
            return undefined;
          }

          return {
            x: round2(x),
            y: round2(y),
          };
        }
  `;
}
