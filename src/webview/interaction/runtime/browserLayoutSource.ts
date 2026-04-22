export function getBrowserLayoutSource(): string {
  return `
        function createLayoutVariants(tableMetaList) {
          if (tableMetaList.length > 500) {
            return createCatalogPlaceholderLayoutVariants(tableMetaList);
          }

          return {
            circular: createCircularLayout(tableMetaList),
            clustered: createClusteredLayout(tableMetaList),
            flow: createFlowLayout(tableMetaList),
            graph: createRelationGraphLayout(tableMetaList),
            hierarchical: createHierarchicalLayout(tableMetaList),
            neural: createNeuralLayout(tableMetaList),
            radial: createRadialLayout(tableMetaList),
          };
        }

        function createCatalogPlaceholderLayoutVariants(tableMetaList) {
          const config = createCatalogPlaceholderLayoutConfig(tableMetaList);
          return {
            circular: createCatalogCircularLayout(tableMetaList, config),
            clustered: createCatalogClusteredLayout(tableMetaList, config),
            flow: createCatalogFlowLayout(tableMetaList, config),
            graph: createCatalogRelationGraphLayout(tableMetaList, config),
            hierarchical: createGridLayout(tableMetaList),
            neural: createCatalogNeuralLayout(tableMetaList, config),
            radial: createCatalogRadialLayout(tableMetaList, config),
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
            maxShelfWidth: Math.max(
              Math.max(1, maxWidth + 48) * 8,
              Math.sqrt(Math.max(tableMetaList.length, 1)) * Math.max(1, maxWidth + 48) * 1.4,
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
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);

          return createRelationAwareClusteredLayout(
            groupTablesByKey(tableMetaList, (table) => table.appLabel),
            {
              cellHeight: Math.max(1, maxHeight + 92),
              cellWidth: Math.max(1, maxWidth + 56),
              clusterGapX: 140,
              clusterGapY: 120,
              margin: 80,
              maxShelfWidth: Math.max(
                960,
                Math.sqrt(Math.max(tableMetaList.length, 1)) * Math.max(1, maxWidth + 56) * 1.8,
              ),
            },
          );
        }

        function createRelationGraphLayout(tableMetaList) {
          return createForceDirectedRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 160,
              componentGapY: 140,
              layerGapX: 0,
              layerGapY: 0,
              margin: 88,
              ringStep: Math.max(
                140,
                tableMetaList.reduce((largest, table) => Math.max(largest, table.width, table.height), 0) + 72,
              ),
            }),
          );
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

        function createFlowLayout(tableMetaList) {
          return createLayeredRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 180,
              componentGapY: 140,
              layerGapX: tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0) + 40,
              layerGapY: tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0) + 124,
              margin: 88,
              orientation: "vertical",
              ringStep: 0,
              sweepIterations: 7,
            }),
          );
        }

        function createNeuralLayout(tableMetaList) {
          return createLayeredRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 180,
              componentGapY: 140,
              layerGapX: tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0) + 132,
              layerGapY: tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0) + 30,
              margin: 88,
              orientation: "horizontal",
              ringStep: 0,
              sweepIterations: 7,
            }),
          );
        }

        function createRadialLayout(tableMetaList) {
          return createRadialRelationLayout(
            tableMetaList,
            createRelationLayoutConfig(tableMetaList, {
              componentGapX: 160,
              componentGapY: 140,
              layerGapX: 0,
              layerGapY: 0,
              margin: 88,
              ringStep: Math.max(
                150,
                tableMetaList.reduce((largest, table) => Math.max(largest, table.width, table.height), 0) + 84,
              ),
              sweepIterations: 6,
            }),
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

        function createRelationLayoutConfig(tableMetaList, overrides) {
          const maxWidth = tableMetaList.reduce((largest, table) => Math.max(largest, table.width), 0);
          const maxHeight = tableMetaList.reduce((largest, table) => Math.max(largest, table.height), 0);

          return {
            componentGapX: overrides.componentGapX,
            componentGapY: overrides.componentGapY,
            layerGapX: overrides.layerGapX,
            layerGapY: overrides.layerGapY,
            margin: overrides.margin,
            maxShelfWidth: Math.max(
              1080,
              Math.sqrt(Math.max(tableMetaList.length, 1)) * Math.max(1, maxWidth + 72) * 2,
            ),
            orientation: overrides.orientation || "horizontal",
            ringStep: overrides.ringStep || Math.max(maxWidth, maxHeight) + 72,
            sweepIterations: overrides.sweepIterations || 0,
          };
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
          const relationState = createRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            createConcentricComponentPlan(componentIds, relationState, config, componentIndex),
          );

          return placeRelationComponentPlans(componentPlans, config);
        }

        function createForceDirectedRelationLayout(tableMetaList, config) {
          const relationState = createRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            createForceDirectedComponentPlan(componentIds, relationState, config, componentIndex),
          );

          return placeRelationComponentPlans(componentPlans, config);
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
            centerStrength: nodeCount >= 24 ? 0.014 : 0.02,
            collisionPadding: Math.max(28, averageSpan * 0.2),
            collisionStrength: 0.18,
            cooling: nodeCount >= 64 ? 0.93 : 0.95,
            degreeDistanceStep: Math.max(4, averageSpan * 0.05),
            iterations: Math.min(84, Math.max(28, Math.round(24 + Math.sqrt(nodeCount) * 5))),
            maxStep: Math.max(18, averageSpan * 0.24),
            preferredEdgeLength: Math.max(
              132,
              (config.ringStep || averageSpan) * (density >= 1.6 ? 0.8 : 0.95),
            ),
            repulsionStrength: Math.max(
              2200,
              averageSpan * averageSpan * (density >= 1.6 ? 0.9 : 1.15),
            ),
            rootCenterStrength: nodeCount >= 24 ? 0.08 : 0.12,
            springStrength: density >= 1.6 ? 0.055 : 0.07,
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
          const relationState = createRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds) =>
            createLayeredComponentPlan(componentIds, relationState, config),
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
          const relationState = createRelationLayoutState(tableMetaList);
          const componentPlans = relationState.components.map((componentIds, componentIndex) =>
            createRadialComponentPlan(componentIds, relationState, config, componentIndex),
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

            const radius =
              layerIndex === 0
                ? layerIds.length === 1
                  ? 0
                  : Math.max(config.ringStep * 0.45, maxTableSpan * 0.9)
                : layerIndex * config.ringStep;
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
          }

          return orderedLayers;
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

        function placeRelationComponentPlans(componentPlans, config) {
          const positions = {};
          const maxShelfWidth = Math.max(config.margin + 1, config.maxShelfWidth || 0);
          let cursorX = config.margin;
          let cursorY = config.margin;
          let shelfHeight = 0;

          componentPlans.forEach((componentPlan) => {
            if (
              cursorX > config.margin &&
              cursorX + componentPlan.width > maxShelfWidth
            ) {
              cursorX = config.margin;
              cursorY += shelfHeight + config.componentGapY;
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

        function routeCatalogEdgesWithPorts(edgeEntries) {
          const endpointRefsByKey = new Map();
          const routes = [];
          const bundleState = state.layoutMode === "clustered"
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
              points: route.bundleRoute
                ? buildCatalogBundledPathFromPorts(
                    start,
                    route.sourceSide,
                    end,
                    route.targetSide,
                    route.bundleRoute,
                  )
                : buildOrthogonalPathFromPorts(start, route.sourceSide, end, route.targetSide),
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

        function buildCatalogBundledPathFromPorts(start, sourceSide, end, targetSide, bundleRoute) {
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
