import type { InspectorModelRenderModel, InspectorRelationshipRenderModel } from "./createDiagramRenderModel";

type CircularModel = Pick<InspectorModelRenderModel, "modelId" | "modelName" | "appLabel" | "databaseTableName" | "relationships">;
interface CircularPoint { x: number; y: number }
export interface CircularNode {
  modelId: CircularModel["modelId"]; modelName: string; appLabel: string; databaseTableName: string;
  angle: number; x: number; y: number; degree: number;
}
export interface CircularEdge extends Pick<InspectorRelationshipRenderModel, "edgeId" | "sourceModelId" | "targetModelId" | "kind" | "fieldName"> {
  points: [CircularPoint, CircularPoint, CircularPoint, CircularPoint];
}

/** Self-contained so the same semantic graph and hit testing run in the webview. */
export function createCircularDiagramTools() {
  function build(models: Iterable<CircularModel>) {
    const ordered = Array.from(models).filter(model => !model.modelId.startsWith("__leafbundle."))
      .sort((a, b) => a.appLabel.localeCompare(b.appLabel) || a.modelId.localeCompare(b.modelId));
    const radius = Math.max(200, ordered.length * 16 / (2 * Math.PI));
    const step = 2 * Math.PI / Math.max(1, ordered.length);
    const nodes: CircularNode[] = ordered.map((model, index) => {
      const angle = -Math.PI / 2 + step * index;
      return {modelId: model.modelId, modelName: model.modelName, appLabel: model.appLabel,
        databaseTableName: model.databaseTableName, angle, x: Math.cos(angle) * radius,
        y: Math.sin(angle) * radius, degree: 0};
    });
    const byId = new Map(nodes.map(node => [node.modelId, node]));
    const seen = new Set<string>();
    const declared: Array<Omit<CircularEdge, "points">> = [];
    let unavailableCount = 0;
    for (const model of ordered) for (const relationship of model.relationships || []) {
      if (!relationship.edgeId || seen.has(relationship.edgeId)) continue;
      seen.add(relationship.edgeId);
      const source = byId.get(relationship.sourceModelId), target = byId.get(relationship.targetModelId);
      if (!source || !target) {unavailableCount++; continue;}
      source.degree++; if (source !== target) target.degree++;
      declared.push({edgeId: relationship.edgeId, sourceModelId: source.modelId, targetModelId: target.modelId,
        kind: relationship.kind, fieldName: relationship.fieldName});
    }
    declared.sort((a, b) => a.edgeId.localeCompare(b.edgeId));
    const groups = new Map<string, Array<Omit<CircularEdge, "points">>>();
    for (const edge of declared) {
      const key = JSON.stringify([edge.sourceModelId, edge.targetModelId].sort());
      const group = groups.get(key) || []; group.push(edge); groups.set(key, group);
    }
    const edges: CircularEdge[] = [];
    for (const group of groups.values()) for (let index = 0; index < group.length; index++) {
      const edge = group[index], source = byId.get(edge.sourceModelId)!, target = byId.get(edge.targetModelId)!;
      const inset = Math.max(8, radius * 0.012);
      const from = {x: source.x * (radius - inset) / radius, y: source.y * (radius - inset) / radius};
      const to = {x: target.x * (radius - inset) / radius, y: target.y * (radius - inset) / radius};
      let points: CircularEdge["points"];
      if (source === target) {
        const spread = 18 + index * 8, depth = 45 + index * 14;
        const nx = Math.cos(source.angle), ny = Math.sin(source.angle);
        points = [from, {x: from.x - nx * depth - ny * spread, y: from.y - ny * depth + nx * spread},
          {x: to.x - nx * depth + ny * spread, y: to.y - ny * depth - nx * spread}, to];
      } else {
        // Use the same normal for reverse edges, retaining distinct parallel curves.
        const first = source.modelId < target.modelId ? source : target;
        const last = first === source ? target : source;
        const length = Math.hypot(last.x - first.x, last.y - first.y) || 1;
        const offset = (index - (group.length - 1) / 2) * 10;
        const dx = -(last.y - first.y) / length * offset, dy = (last.x - first.x) / length * offset;
        points = [from, {x: from.x * 0.25 + dx, y: from.y * 0.25 + dy},
          {x: to.x * 0.25 + dx, y: to.y * 0.25 + dy}, to];
      }
      edges.push({...edge, points});
    }
    const apps: Array<{appLabel: string; start: number; end: number; modelCount: number}> = [];
    for (const node of nodes) {
      const previous = apps[apps.length - 1];
      if (previous?.appLabel === node.appLabel) {previous.end = node.angle + step / 2; previous.modelCount++;}
      else apps.push({appLabel: node.appLabel, start: node.angle - step / 2, end: node.angle + step / 2, modelCount: 1});
    }
    return {nodes, edges, apps, radius, relationshipCount: seen.size, unavailableCount};
  }

  function point(points: CircularEdge["points"], t: number): CircularPoint {
    const s = 1 - t;
    return {x: s ** 3 * points[0].x + 3 * s * s * t * points[1].x + 3 * s * t * t * points[2].x + t ** 3 * points[3].x,
      y: s ** 3 * points[0].y + 3 * s * s * t * points[1].y + 3 * s * t * t * points[2].y + t ** 3 * points[3].y};
  }

  function hitEdge(edges: CircularEdge[], location: CircularPoint, tolerance: number): CircularEdge | undefined {
    let best: CircularEdge | undefined, distance = tolerance;
    for (const edge of edges) {
      let previous = edge.points[0];
      for (let i = 1; i <= 32; i++) {
        const next = point(edge.points, i / 32), dx = next.x - previous.x, dy = next.y - previous.y;
        const length = dx * dx + dy * dy;
        const t = length ? Math.max(0, Math.min(1, ((location.x - previous.x) * dx + (location.y - previous.y) * dy) / length)) : 0;
        const current = Math.hypot(location.x - previous.x - t * dx, location.y - previous.y - t * dy);
        if (current < distance) {distance = current; best = edge;}
        previous = next;
      }
    }
    return best;
  }
  return {build, point, hitEdge};
}
