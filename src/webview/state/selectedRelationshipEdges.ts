import type { EdgeRenderModel } from "./createDiagramRenderModel";

/** A selected model must be a physical endpoint, not just a member of an overview line. */
export function createSelectedRelationshipTools() {
  function pair(edge: EdgeRenderModel): string {
    return JSON.stringify([edge.sourceModelId, edge.targetModelId].sort());
  }

  function length(edge: EdgeRenderModel): number {
    const points = edge.points.trim().split(/\s+/).map(point => point.split(",").map(Number));
    return points.length === 2 && points.every(point => point.length === 2 && point.every(Number.isFinite))
      ? Math.hypot(points[1][0] - points[0][0], points[1][1] - points[0][1]) : Infinity;
  }

  function combine(members: EdgeRenderModel[], edgeId: string, role: "direct" | "overview"): EdgeRenderModel {
    const ordered = members.slice().sort((a, b) => length(a) - length(b) || a.edgeId.localeCompare(b.edgeId));
    const first = ordered[0];
    return {...first, edgeId, carrierRole: role, crossingIds: [], markerStartId: "", markerEndId: "",
      carrierFamily: new Set(members.map(edge => edge.carrierFamily)).size === 1 ? first.carrierFamily : "mixed",
      memberEdgeIds: ordered.map(edge => edge.edgeId),
      logicalEndpointModelIds: [...new Set(members.flatMap(edge => [edge.sourceModelId, edge.targetModelId]))],
      physicalEndpointModelIds: [first.sourceModelId, first.targetModelId], leafCardEndpointIds: undefined,
      preserveRouteEndpoints: true};
  }

  function focus(base: EdgeRenderModel[], individual: EdgeRenderModel[], selectedModelId?: string): EdgeRenderModel[] {
    if (!selectedModelId || !individual.length) return base;
    const selected = individual.filter(edge => edge.sourceModelId === selectedModelId || edge.targetModelId === selectedModelId);
    if (!selected.length) return base;
    const selectedIds = new Set(selected.map(edge => edge.edgeId));
    const byId = new Map(individual.map(edge => [edge.edgeId, edge]));
    const result: EdgeRenderModel[] = [];
    for (const edge of base) {
      const ids = edge.memberEdgeIds?.length ? edge.memberEdgeIds : [edge.edgeId];
      if (!ids.some(id => selectedIds.has(id))) {result.push(edge); continue;}
      const remaining = ids.filter(id => !selectedIds.has(id)).map(id => byId.get(id)).filter((member): member is EdgeRenderModel => Boolean(member));
      if (remaining.length === 1) result.push(remaining[0]);
      else if (remaining.length) result.push(combine(remaining, edge.edgeId, "overview"));
    }
    const pairs = new Map<string, EdgeRenderModel[]>();
    for (const edge of selected) {
      const key = pair(edge), members = pairs.get(key) || [];
      members.push(edge); pairs.set(key, members);
    }
    for (const [key, members] of pairs) result.push(combine(members, "selected-connection:" + key, "direct"));
    return result;
  }

  return {focus};
}
