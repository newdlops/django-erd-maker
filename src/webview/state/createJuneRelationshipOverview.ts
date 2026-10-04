import type { RenderedCarrierRoute } from "../../shared/graph/layoutContract";
import type { EdgeRenderModel } from "./createDiagramRenderModel";

/**
 * An explicit relationship overview, using the June leaf/hub memberships.
 * Each line is a real member's straight route. Every canonical relationship
 * belongs to exactly one group and remains available in the expanded view.
 * This is not the June native scorer or a claim of individual connectivity.
 */
export function createJuneRelationshipOverview(
  individualEdges: EdgeRenderModel[],
  groups: RenderedCarrierRoute[],
): { edges: EdgeRenderModel[]; groupCount: number } | undefined {
  const byId = new Map(individualEdges.map((edge) => [edge.edgeId, edge]));
  const represented = new Set<string>();
  const groupIds = new Set<string>();
  const edges: EdgeRenderModel[] = [];
  let groupCount = 0;
  for (const group of groups) {
    if (!group.carrierId || groupIds.has(group.carrierId) || group.memberEdgeIds.length === 0) {
      return undefined;
    }
    groupIds.add(group.carrierId);
    const members: EdgeRenderModel[] = [];
    for (const id of group.memberEdgeIds) {
      const edge = byId.get(id);
      if (!edge || represented.has(id) || !Number.isFinite(straightLength(edge))) {
        return undefined;
      }
      represented.add(id);
      members.push(edge);
    }
    // A short existing route is readable and preserves real boundary ports.
    // Do not average unrelated endpoints into a floating line as June did.
    members.sort((left, right) => straightLength(left) - straightLength(right)
      || left.edgeId.localeCompare(right.edgeId));
    const representative = members[0];
    if (members.length === 1) {
      edges.push(representative);
      continue;
    }
    groupCount += 1;
    const families = new Set(members.map((edge) => edge.carrierFamily));
    edges.push({
      ...representative,
      carrierFamily: families.size === 1 ? representative.carrierFamily : "mixed",
      carrierRole: "overview",
      crossingIds: [],
      edgeId: group.carrierId,
      logicalEndpointModelIds: [...new Set(members.flatMap((edge) =>
        [edge.sourceModelId, edge.targetModelId]))],
      markerEndId: "",
      markerStartId: "",
      memberEdgeIds: members.map((edge) => edge.edgeId),
      physicalEndpointModelIds: [representative.sourceModelId, representative.targetModelId],
      preserveRouteEndpoints: true,
    });
  }
  // Reject incomplete/ambiguous saved groups as a whole: use individual lines.
  if (represented.size !== byId.size || groupCount === 0
    || new Set(edges.map((edge) => edge.edgeId)).size !== edges.length) {
    return undefined;
  }
  return { edges, groupCount };
}

function straightLength(edge: EdgeRenderModel): number {
  const points = edge.points.trim().split(/\s+/).map((point) => point.split(",").map(Number));
  if (points.length !== 2 || points.some((point) => point.length !== 2 || !point.every(Number.isFinite))) {
    return Number.POSITIVE_INFINITY;
  }
  return Math.hypot(points[0][0] - points[1][0], points[0][1] - points[1][1]);
}
