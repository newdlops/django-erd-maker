export function getBrowserEdgePathSource(): string {
  return `
        function getStaticEdgePath(entry) {
          const staticPoints = parseEdgePoints(entry.meta.points);
          const sourceAtBase = samePosition(entry.sourcePosition, entry.sourceTable.basePosition);
          const targetAtBase = samePosition(entry.targetPosition, entry.targetTable.basePosition);

          if (staticPoints.length >= 2 && sourceAtBase && targetAtBase) {
            return entry.meta.preserveRouteEndpoints
              ? normalizePoints(staticPoints)
              : attachPathEndpointsToRenderedTables(entry, staticPoints);
          }

          if (entry.meta.preserveRouteEndpoints && staticPoints.length === 2) {
            return movePreservedStraightEndpoints(entry, staticPoints);
          }

          return [];
        }

        function movePreservedStraightEndpoints(entry, staticPoints) {
          const positions = [entry.sourcePosition, entry.targetPosition];
          const tables = [entry.sourceTable, entry.targetTable];
          const points = staticPoints.map((point, end) => ({
            x: round2(point.x + positions[end].x - tables[end].basePosition.x),
            y: round2(point.y + positions[end].y - tables[end].basePosition.y),
          }));

          // A hand drag keeps each port attached to its card. In particular,
          // the other card's port must not be reassigned without its neighbors.
          // Reattach only an endpoint which now faces into its own rectangle.
          for (let pass = 0; pass < 2; pass += 1) {
            for (let end = 0; end < 2; end += 1) {
              if (!isOutwardPreservedPort(points[end], points[1 - end], positions[end], tables[end])) {
                points[end] = computeBoundaryPort(positions[end], tables[end], points[1 - end]);
              }
            }
          }
          if (points.every((point, end) =>
            isOutwardPreservedPort(point, points[1 - end], positions[end], tables[end]))) {
            return normalizePoints(points);
          }
          return buildStraightPath(entry.sourcePosition, entry.sourceTable, entry.targetPosition, entry.targetTable);
        }

        function isOutwardPreservedPort(point, peer, position, table) {
          const epsilon = 0.011;
          const left = position.x, right = left + table.width;
          const top = position.y, bottom = top + table.height;
          if (point.x < left - epsilon || point.x > right + epsilon ||
              point.y < top - epsilon || point.y > bottom + epsilon) return false;
          return (Math.abs(point.x - left) <= epsilon && peer.x <= point.x + epsilon) ||
            (Math.abs(point.x - right) <= epsilon && peer.x >= point.x - epsilon) ||
            (Math.abs(point.y - top) <= epsilon && peer.y <= point.y + epsilon) ||
            (Math.abs(point.y - bottom) <= epsilon && peer.y >= point.y - epsilon);
        }

        function getStaticOrLiveEdgePath(entry) {
          const staticPoints = getStaticEdgePath(entry);
          if (staticPoints.length >= 2) return staticPoints;
          return attachPathEndpointsToRenderedTables(entry, buildStraightPath(
            entry.sourcePosition, entry.sourceTable, entry.targetPosition, entry.targetTable,
          ));
        }

        function getStaticOrCatalogEdgePaths(edgeEntries) {
          const routedEdges = [];
          const catalogEntries = [];
          for (const entry of edgeEntries) {
            const staticPoints = getStaticEdgePath(entry);
            if (staticPoints.length >= 2) {
              routedEdges.push({ edgeId: entry.meta.edgeId, meta: entry.meta, points: staticPoints });
            } else {
              catalogEntries.push(entry);
            }
          }
          return routedEdges.concat(routeCatalogEdgesWithPorts(catalogEntries).map((routed) => ({
            edgeId: routed.entry.meta.edgeId, meta: routed.entry.meta, points: routed.points,
          })));
        }
  `;
}
