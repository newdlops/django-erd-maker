import { OGDF_LAYOUT_MODES } from "../../../shared/graph/layoutContract";

export function getBrowserLayoutSource(): string {
  const layoutModesJson = JSON.stringify(OGDF_LAYOUT_MODES);

  return `
        const layoutModes = ${layoutModesJson};

        function createLayoutVariants(tableMetaList) {
          return createSharedLayoutVariants(createPayloadLayout(tableMetaList));
        }

        function createCatalogPlaceholderLayoutVariants(tableMetaList) {
          return createSharedLayoutVariants(createPayloadLayout(tableMetaList));
        }

        function createSharedLayoutVariants(baseLayout) {
          return layoutModes.reduce((variants, layoutMode) => {
            variants[layoutMode] = cloneLayoutPositions(baseLayout);
            return variants;
          }, {});
        }

        function createPayloadLayout(tableMetaList) {
          const positions = {};

          for (const table of tableMetaList) {
            positions[table.modelId] = {
              x: round2(table.basePosition.x),
              y: round2(table.basePosition.y),
            };
          }

          return positions;
        }

        function cloneLayoutPositions(layout) {
          const copy = {};

          for (const [modelId, position] of Object.entries(layout)) {
            copy[modelId] = {
              x: position.x,
              y: position.y,
            };
          }

          return copy;
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
          if (
            drag &&
            drag.kind === "table" &&
            drag.modelId === modelId &&
            drag.currentPosition
          ) {
            return drag.currentPosition;
          }

          const options = getTableOptions(state, modelId);
          return options.manualPosition || getBasePosition(modelId);
        }

        function findTableAtCanvasPoint(event) {
          const point = toWorldPoint(event);
          const orderedTables = typeof queryTableMetaNearWorldPoint === "function"
            ? queryTableMetaNearWorldPoint(point)
            : Array.from(tableMetaById.values()).reverse();

          return orderedTables.find((table) => {
            const meta = table.meta || table;
            const modelId = meta.modelId;
            if (!isVisibleModel(modelId)) {
              return false;
            }

            const position = table.x !== undefined && table.y !== undefined
              ? { x: table.x, y: table.y }
              : getCurrentPosition(modelId);
            return (
              point.x >= position.x &&
              point.x <= position.x + meta.width &&
              point.y >= position.y &&
              point.y <= position.y + meta.height
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

        function buildStraightPath(sourcePosition, sourceTable, targetPosition, targetTable) {
          const sourceCenter = getCenter(sourcePosition, sourceTable);
          const targetCenter = getCenter(targetPosition, targetTable);
          const sourcePort = computeBoundaryPort(sourcePosition, sourceTable, targetCenter);
          const targetPort = computeBoundaryPort(targetPosition, targetTable, sourceCenter);
          return normalizePoints([sourcePort, targetPort]);
        }

        function buildBundledPath(
          sourcePosition,
          sourceTable,
          targetPosition,
          targetTable,
          sourceCluster,
          targetCluster,
          bundleStrength,
        ) {
          const sourceCenter = getCenter(sourcePosition, sourceTable);
          const targetCenter = getCenter(targetPosition, targetTable);
          const sourcePort = computeBoundaryPort(sourcePosition, sourceTable, targetCenter);
          const targetPort = computeBoundaryPort(targetPosition, targetTable, sourceCenter);

          const dx = targetCluster.x - sourceCluster.x;
          const dy = targetCluster.y - sourceCluster.y;
          const dist = Math.max(1, Math.sqrt(dx * dx + dy * dy));
          const ux = dx / dist;
          const uy = dy / dist;
          const sourceRadius = (sourceCluster.radius || 0) + 80;
          const targetRadius = (targetCluster.radius || 0) + 80;
          const sourceBoundary = {
            x: sourceCluster.x + ux * Math.min(sourceRadius, dist * 0.45),
            y: sourceCluster.y + uy * Math.min(sourceRadius, dist * 0.45),
          };
          const targetBoundary = {
            x: targetCluster.x - ux * Math.min(targetRadius, dist * 0.45),
            y: targetCluster.y - uy * Math.min(targetRadius, dist * 0.45),
          };
          const t = Math.max(0, Math.min(1, bundleStrength == null ? 0.85 : bundleStrength));
          const c1 = {
            x: sourcePort.x * (1 - t) + sourceBoundary.x * t,
            y: sourcePort.y * (1 - t) + sourceBoundary.y * t,
          };
          const c2 = {
            x: targetPort.x * (1 - t) + targetBoundary.x * t,
            y: targetPort.y * (1 - t) + targetBoundary.y * t,
          };
          const samples = 14;
          const points = [];
          for (let i = 0; i <= samples; i += 1) {
            const u = i / samples;
            const v = 1 - u;
            const x =
              v * v * v * sourcePort.x +
              3 * v * v * u * c1.x +
              3 * v * u * u * c2.x +
              u * u * u * targetPort.x;
            const y =
              v * v * v * sourcePort.y +
              3 * v * v * u * c1.y +
              3 * v * u * u * c2.y +
              u * u * u * targetPort.y;
            points.push({ x: round2(x), y: round2(y) });
          }
          return normalizePoints(points);
        }

        function computeAppClusterCenters(visibleEdgeEntries) {
          const accumulators = new Map();
          function accumulate(table, position) {
            if (!table || !table.appLabel) {
              return;
            }
            const key = table.appLabel;
            const center = getCenter(position, table);
            if (!accumulators.has(key)) {
              accumulators.set(key, {
                sumX: 0,
                sumY: 0,
                count: 0,
                positions: [],
              });
            }
            const acc = accumulators.get(key);
            acc.sumX += center.x;
            acc.sumY += center.y;
            acc.count += 1;
            acc.positions.push(center);
          }
          for (const entry of visibleEdgeEntries) {
            accumulate(entry.sourceTable, entry.sourcePosition);
            accumulate(entry.targetTable, entry.targetPosition);
          }
          const centers = new Map();
          for (const [key, acc] of accumulators.entries()) {
            if (acc.count > 0) {
              const cx = acc.sumX / acc.count;
              const cy = acc.sumY / acc.count;
              let radius = 0;
              for (const p of acc.positions) {
                const dx = p.x - cx;
                const dy = p.y - cy;
                const d = Math.sqrt(dx * dx + dy * dy);
                if (d > radius) radius = d;
              }
              centers.set(key, { x: cx, y: cy, radius });
            }
          }
          return centers;
        }

        function computeSpatialClusterContext(visibleEdgeEntries) {
          let minX = Infinity;
          let minY = Infinity;
          let maxX = -Infinity;
          let maxY = -Infinity;
          let count = 0;
          for (const entry of visibleEdgeEntries) {
            for (const pair of [
              [entry.sourcePosition, entry.sourceTable],
              [entry.targetPosition, entry.targetTable],
            ]) {
              const [pos, tbl] = pair;
              if (!tbl) continue;
              const center = getCenter(pos, tbl);
              if (center.x < minX) minX = center.x;
              if (center.y < minY) minY = center.y;
              if (center.x > maxX) maxX = center.x;
              if (center.y > maxY) maxY = center.y;
              count += 1;
            }
          }
          if (count === 0 || !Number.isFinite(minX)) {
            return null;
          }
          const grid = Math.max(3, Math.min(8, Math.round(Math.sqrt(count / 12))));
          const width = Math.max(1, maxX - minX);
          const height = Math.max(1, maxY - minY);
          const cellW = width / grid;
          const cellH = height / grid;
          function cellKey(position, table) {
            const center = getCenter(position, table);
            const col = Math.min(grid - 1, Math.max(0, Math.floor((center.x - minX) / cellW)));
            const row = Math.min(grid - 1, Math.max(0, Math.floor((center.y - minY) / cellH)));
            return col + ":" + row;
          }
          function cellCenter(key) {
            const [col, row] = key.split(":").map(Number);
            return {
              x: minX + (col + 0.5) * cellW,
              y: minY + (row + 0.5) * cellH,
            };
          }
          return { cellKey, cellCenter };
        }

        function applyClusterCollapse(scene, visibleEdgeEntries) {
          if (!scene.tables.length) {
            return null;
          }
          const groups = new Map();
          const clusterKeyByModelId = new Map();
          let usingSpatial = false;
          let firstClusterIdSeen = null;
          for (const record of scene.tables) {
            const explicit = record.meta && record.meta.clusterId;
            if (explicit) {
              firstClusterIdSeen = explicit;
              break;
            }
          }
          if (!firstClusterIdSeen) {
            usingSpatial = true;
          }

          let spatial = null;
          if (usingSpatial) {
            const tablePseudoEntries = scene.tables.map((record) => ({
              sourcePosition: { x: record.x, y: record.y },
              sourceTable: { width: record.width, height: record.height, appLabel: record.meta?.appLabel || "" },
              targetPosition: { x: record.x, y: record.y },
              targetTable: { width: record.width, height: record.height, appLabel: record.meta?.appLabel || "" },
            }));
            spatial = computeSpatialClusterContext(tablePseudoEntries);
          }

          for (const record of scene.tables) {
            let key = record.meta && record.meta.clusterId;
            if (!key) {
              if (spatial) {
                key = spatial.cellKey(
                  { x: record.x, y: record.y },
                  { width: record.width, height: record.height },
                );
              } else if (record.meta && record.meta.appLabel) {
                key = record.meta.appLabel;
              } else {
                key = "_default";
              }
            }
            clusterKeyByModelId.set(record.modelId, key);
            if (!groups.has(key)) {
              groups.set(key, {
                key,
                members: [],
                minX: Infinity,
                minY: Infinity,
                maxX: -Infinity,
                maxY: -Infinity,
              });
            }
            const group = groups.get(key);
            group.members.push(record);
            if (record.x < group.minX) group.minX = record.x;
            if (record.y < group.minY) group.minY = record.y;
            if (record.maxX > group.maxX) group.maxX = record.maxX;
            if (record.maxY > group.maxY) group.maxY = record.maxY;
          }

          if (groups.size < 2) {
            return null;
          }

          const groupArray = Array.from(groups.values());
          for (const group of groupArray) {
            group.memberCount = group.members.length;
          }
          groupArray.sort((a, b) => b.memberCount - a.memberCount);

          const minSuperWidth = 480;
          const maxSuperWidth = 1200;
          const minSuperHeight = 200;
          const maxSuperHeight = 480;
          const cellGap = 200;
          const totalMembers = groupArray.reduce((sum, g) => sum + g.memberCount, 0) || 1;
          for (const group of groupArray) {
            const ratio = group.memberCount / totalMembers;
            const scale = Math.sqrt(Math.max(ratio, 0.005));
            group.superWidth = Math.min(
              maxSuperWidth,
              Math.max(minSuperWidth, scale * maxSuperWidth * 6),
            );
            group.superHeight = Math.min(
              maxSuperHeight,
              Math.max(minSuperHeight, scale * maxSuperHeight * 6),
            );
          }

          const cols = Math.max(1, Math.ceil(Math.sqrt(groupArray.length)));
          const cellW = Math.max(...groupArray.map((g) => g.superWidth)) + cellGap;
          const cellH = Math.max(...groupArray.map((g) => g.superHeight)) + cellGap;

          const newTables = [];
          const newTablesById = new Map();
          const newBuckets = new Map();
          const superMetaById = new Map();
          for (let idx = 0; idx < groupArray.length; idx += 1) {
            const group = groupArray[idx];
            const row = Math.floor(idx / cols);
            const col = idx % cols;
            const cellCenterX = col * cellW + cellW / 2;
            const cellCenterY = row * cellH + cellH / 2;
            const width = group.superWidth;
            const height = group.superHeight;
            const x = cellCenterX - width / 2;
            const y = cellCenterY - height / 2;
            const superId = "_super_" + group.key;
            const memberCount = group.memberCount;
            const memberMeta = group.members[0]?.meta;
            const meta = {
              appLabel: memberMeta?.appLabel || group.key,
              clusterId: group.key,
              fieldRows: [],
              hasExplicitDatabaseTableName: false,
              height,
              methods: [],
              modelId: superId,
              modelName: group.key + " (" + memberCount + ")",
              properties: [],
              tableName: group.key,
              width,
            };
            const tableSurrogate = {
              activeMethodName: undefined,
              appLabel: meta.appLabel,
              clusterId: group.key,
              databaseTableName: meta.tableName,
              fieldRows: [],
              hasExplicitDatabaseTableName: false,
              hidden: false,
              methodAssociations: [],
              methods: [],
              modelId: superId,
              modelName: meta.modelName,
              position: { x, y },
              properties: [],
              selected: false,
              showMethodHighlights: false,
              showMethods: false,
              showProperties: false,
              size: { width, height },
            };
            const record = {
              clusterId: group.key,
              clusterMemberCount: memberCount,
              height,
              maxX: x + width,
              maxY: y + height,
              meta,
              modelId: superId,
              options: { hidden: false, manualPosition: undefined, modelId: superId, showMethodHighlights: false, showMethods: false, showProperties: false },
              table: tableSurrogate,
              width,
              x,
              y,
            };
            newTables.push(record);
            newTablesById.set(superId, record);
            superMetaById.set(group.key, record);
          }

          scene.tables = newTables;
          scene.tablesById = newTablesById;
          scene.tableBuckets = newBuckets;

          const pairCounts = new Map();
          for (const entry of visibleEdgeEntries) {
            const srcKey = clusterKeyByModelId.get(entry.meta.sourceModelId);
            const tgtKey = clusterKeyByModelId.get(entry.meta.targetModelId);
            if (!srcKey || !tgtKey || srcKey === tgtKey) {
              continue;
            }
            const ordered = srcKey < tgtKey ? [srcKey, tgtKey] : [tgtKey, srcKey];
            const pairKey = ordered[0] + "→" + ordered[1];
            if (!pairCounts.has(pairKey)) {
              pairCounts.set(pairKey, { source: ordered[0], target: ordered[1], count: 0 });
            }
            pairCounts.get(pairKey).count += 1;
          }

          const superEdges = [];
          let superIndex = 0;
          for (const pair of pairCounts.values()) {
            const src = superMetaById.get(pair.source);
            const tgt = superMetaById.get(pair.target);
            if (!src || !tgt) continue;
            const srcCenter = { x: src.x + src.width / 2, y: src.y + src.height / 2 };
            const tgtCenter = { x: tgt.x + tgt.width / 2, y: tgt.y + tgt.height / 2 };
            const sourcePort = computeBoundaryPort(
              { x: src.x, y: src.y },
              { width: src.width, height: src.height },
              tgtCenter,
            );
            const targetPort = computeBoundaryPort(
              { x: tgt.x, y: tgt.y },
              { width: tgt.width, height: tgt.height },
              srcCenter,
            );
            superIndex += 1;
            superEdges.push({
              edgeId: "_super_edge_" + superIndex,
              meta: {
                count: pair.count,
                cssKind: "super",
                edgeId: "_super_edge_" + superIndex,
                provenance: "cluster_collapse",
                sourceModelId: "_super_" + pair.source,
                targetModelId: "_super_" + pair.target,
              },
              points: [sourcePort, targetPort],
              count: pair.count,
            });
          }

          return {
            aggregates: {
              superEdges,
              clusterCount: newTables.length,
            },
            clusterKeyByModelId,
          };
        }

        function computeBoundaryPort(rectPosition, rect, towardCenter) {
          const center = getCenter(rectPosition, rect);
          let dx = towardCenter.x - center.x;
          let dy = towardCenter.y - center.y;
          if (Math.abs(dx) < 0.01 && Math.abs(dy) < 0.01) {
            dx = 1; dy = 0;
          }
          const halfW = Math.max(1, rect.width / 2);
          const halfH = Math.max(1, rect.height / 2);
          const scaleX = Math.abs(dx) < 0.01 ? Infinity : halfW / Math.abs(dx);
          const scaleY = Math.abs(dy) < 0.01 ? Infinity : halfH / Math.abs(dy);
          const scale = Math.min(scaleX, scaleY);
          return {
            x: round2(center.x + dx * scale),
            y: round2(center.y + dy * scale),
          };
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

        function getStaticEdgePath(entry) {
          const staticPoints = parseEdgePoints(entry.meta.points);
          const sourceAtBase = samePosition(entry.sourcePosition, entry.sourceTable.basePosition);
          const targetAtBase = samePosition(entry.targetPosition, entry.targetTable.basePosition);

          if (staticPoints.length >= 2 && sourceAtBase && targetAtBase) {
            return staticPoints;
          }

          return [];
        }

        function getStaticOrLiveEdgePath(entry) {
          const staticPoints = getStaticEdgePath(entry);
          if (staticPoints.length >= 2) {
            return staticPoints;
          }

          return buildStraightPath(
            entry.sourcePosition,
            entry.sourceTable,
            entry.targetPosition,
            entry.targetTable,
          );
        }

        function getStaticOrCatalogEdgePaths(edgeEntries) {
          const routedEdges = [];
          const catalogEntries = [];

          for (const entry of edgeEntries) {
            const staticPoints = getStaticEdgePath(entry);
            if (staticPoints.length >= 2) {
              routedEdges.push({
                edgeId: entry.meta.edgeId,
                meta: entry.meta,
                points: staticPoints,
              });
            } else {
              catalogEntries.push(entry);
            }
          }

          return routedEdges.concat(
            routeCatalogEdgesWithPorts(catalogEntries).map((routed) => ({
              edgeId: routed.entry.meta.edgeId,
              meta: routed.entry.meta,
              points: routed.points,
            })),
          );
        }

        function parseEdgePoints(value) {
          if (typeof value !== "string" || value.trim().length === 0) {
            return [];
          }

          return value
            .trim()
            .split(/\\s+/)
            .map((pair) => {
              const [rawX, rawY] = pair.split(",");
              const x = Number(rawX);
              const y = Number(rawY);
              if (!Number.isFinite(x) || !Number.isFinite(y)) {
                return undefined;
              }
              return { x: round2(x), y: round2(y) };
            })
            .filter((point) => point !== undefined);
        }

        function routeCatalogEdgesWithPorts(edgeEntries) {
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

            return {
              entry: route.entry,
              points: buildOrthogonalPathFromPorts(start, route.sourceSide, end, route.targetSide),
            };
          });
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

        function samePosition(left, right) {
          return (
            left &&
            right &&
            Math.abs(left.x - right.x) < 0.01 &&
            Math.abs(left.y - right.y) < 0.01
          );
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
