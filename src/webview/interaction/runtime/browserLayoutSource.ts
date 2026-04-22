export function getBrowserLayoutSource(): string {
  return `
        function createLayoutVariants(tableMetaList) {
          if (tableMetaList.length > 500) {
            return createCatalogPlaceholderLayoutVariants(tableMetaList);
          }

          return {
            circular: createCircularLayout(tableMetaList),
            clustered: createClusteredLayout(tableMetaList),
            hierarchical: createHierarchicalLayout(tableMetaList),
          };
        }

        function createCatalogPlaceholderLayoutVariants(tableMetaList) {
          const config = createCatalogPlaceholderLayoutConfig(tableMetaList);
          return {
            circular: createCatalogCircularLayout(tableMetaList, config),
            clustered: createCatalogClusteredLayout(tableMetaList, config),
            hierarchical: createGridLayout(tableMetaList),
          };
        }

        function createCatalogPlaceholderLayoutConfig(tableMetaList) {
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);
          return {
            cellHeight: Math.max(1, maxHeight + 28),
            cellWidth: Math.max(1, maxWidth + 48),
            clusterGapX: 120,
            clusterGapY: 120,
            margin: 24,
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
          const groups = Array.from(groupCatalogTables(tableMetaList).entries())
            .sort((left, right) => left[0].localeCompare(right[0]));
          const positions = {};
          const maxShelfWidth = Math.max(config.cellWidth * 8, Math.sqrt(tableMetaList.length) * config.cellWidth * 1.4);
          let cursorX = config.margin;
          let cursorY = config.margin;
          let shelfHeight = 0;

          for (const [, group] of groups) {
            const ordered = group.slice().sort((left, right) => left.modelId.localeCompare(right.modelId));
            const columns = computeGridColumnCount(ordered.length, config.cellWidth - 48, config.cellHeight - 28);
            const rows = Math.ceil(ordered.length / columns);
            const groupWidth = columns * config.cellWidth;
            const groupHeight = rows * config.cellHeight;

            if (cursorX > config.margin && cursorX + groupWidth > maxShelfWidth) {
              cursorX = config.margin;
              cursorY += shelfHeight + config.clusterGapY;
              shelfHeight = 0;
            }

            ordered.forEach((table, index) => {
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

        function groupCatalogTables(tableMetaList) {
          return tableMetaList.reduce((groups, table) => {
            const key = getCatalogClusterKey(table);
            if (!groups.has(key)) {
              groups.set(key, []);
            }
            groups.get(key).push(table);
            return groups;
          }, new Map());
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

        function createCircularLayout(tableMetaList) {
          const ordered = tableMetaList
            .slice()
            .sort((left, right) => left.modelId.localeCompare(right.modelId));
          const maxWidth = ordered.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = ordered.reduce((largest, table) => Math.max(largest, table.height), 0);
          const radius = Math.max(260, ordered.length * 74 + Math.max(maxWidth, maxHeight) / 2);
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

        function createClusteredLayout(tableMetaList) {
          const positions = {};
          const groups = Array.from(
            tableMetaList.reduce((map, table) => {
              if (!map.has(table.appLabel)) {
                map.set(table.appLabel, []);
              }
              map.get(table.appLabel).push(table);
              return map;
            }, new Map()),
          ).sort((left, right) => left[0].localeCompare(right[0]));
          let cursorX = 80;

          for (const [, group] of groups) {
            const ordered = group.slice().sort((left, right) => left.modelId.localeCompare(right.modelId));
            let cursorY = 96;
            let maxWidth = 0;

            for (const table of ordered) {
              positions[table.modelId] = {
                x: cursorX,
                y: cursorY,
              };
              cursorY += table.height + 92;
              maxWidth = Math.max(maxWidth, table.width);
            }

            cursorX += maxWidth + 140;
          }

          return positions;
        }

        function createHierarchicalLayout(tableMetaList) {
          const positions = {};

          for (const table of tableMetaList) {
            positions[table.modelId] = {
              x: table.basePosition.x,
              y: table.basePosition.y,
            };
          }

          return positions;
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
          return (
            layoutVariants[state.layoutMode]?.[modelId] ||
            layoutVariants.hierarchical[modelId] ||
            { x: 0, y: 0 }
          );
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
