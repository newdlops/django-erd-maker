import type { EdgeRenderModel } from "./createDiagramRenderModel";
import type { LeafCardRenderModel } from "./createLeafCards";

export interface LeafConnectionTable {
  modelId: string;
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface LeafConnectionBounds extends LeafConnectionTable {
  id: string;
  memberModelIds: string[];
  parentModelId: string;
  parentName: string;
  appLabel: string;
  kind: "leaf-card";
  leafCount: number;
  totalLeafCount: number;
  selected: boolean;
}

/** Self-contained so the server audit and GPU canvas execute the same code. */
export function createLeafCardConnectionTools() {
  function bounds(cards: LeafCardRenderModel[], tables: Map<string, LeafConnectionTable>, selectedModelId = ""): LeafConnectionBounds[] {
    return cards.flatMap(card => {
      const members = card.memberModelIds.map(id => tables.get(id)).filter((node): node is LeafConnectionTable => Boolean(node));
      if (!members.length) return [];
      const x = Math.min(...members.map(node => node.x)) - card.padding;
      const y = Math.min(...members.map(node => node.y)) - card.padding;
      const right = Math.max(...members.map(node => node.x + node.width)) + card.padding;
      const bottom = Math.max(...members.map(node => node.y + node.height)) + card.padding;
      const area = members.reduce((sum, node) => sum + node.width * node.height, 0);
      if (![x, y, right, bottom, area].every(Number.isFinite) || area <= 0 || (right - x) * (bottom - y) > area * 6) return [];
      return [{id: card.id, modelId: card.id, x, y, width: right - x, height: bottom - y,
        memberModelIds: card.memberModelIds, parentModelId: card.parentModelId, parentName: card.label,
        appLabel: card.appLabel, kind: "leaf-card" as const, leafCount: members.length,
        totalLeafCount: card.memberModelIds.length, selected: card.memberModelIds.some(id => id === selectedModelId)}];
    });
  }

  function points(edge: EdgeRenderModel) {
    return edge.points.trim().split(/\s+/).map(pair => {
      const [x, y] = pair.split(",").map(Number);
      return {x, y};
    });
  }

  function length(edge: EdgeRenderModel) {
    const p = points(edge);
    return p.length === 2 && p.every(point => Number.isFinite(point.x) && Number.isFinite(point.y))
      ? Math.hypot(p[1].x - p[0].x, p[1].y - p[0].y) : Infinity;
  }

  function representative(members: EdgeRenderModel[]) {
    return members.slice().sort((a, b) => length(a) - length(b) || a.edgeId.localeCompare(b.edgeId))[0];
  }

  function merge(members: EdgeRenderModel[], edgeId: string): EdgeRenderModel {
    const first = representative(members);
    return {...first, edgeId, carrierRole: "overview", crossingIds: [], markerStartId: "", markerEndId: "",
      carrierFamily: new Set(members.map(edge => edge.carrierFamily)).size === 1 ? first.carrierFamily : "mixed",
      memberEdgeIds: members.flatMap(edge => edge.memberEdgeIds?.length ? edge.memberEdgeIds : [edge.edgeId]),
      logicalEndpointModelIds: [...new Set(members.flatMap(edge => edge.logicalEndpointModelIds?.length
        ? edge.logicalEndpointModelIds : [edge.sourceModelId, edge.targetModelId]))],
      physicalEndpointModelIds: [first.sourceModelId, first.targetModelId], preserveRouteEndpoints: true};
  }

  function boundaryParameter(start: {x: number; y: number}, end: {x: number; y: number}, rect: LeafConnectionTable, exiting: boolean) {
    let enter = 0, exit = 1;
    for (const [origin, delta, min, max] of [
      [start.x, end.x - start.x, rect.x, rect.x + rect.width],
      [start.y, end.y - start.y, rect.y, rect.y + rect.height],
    ]) {
      if (Math.abs(delta) < 1e-12) continue;
      const a = (min - origin) / delta, b = (max - origin) / delta;
      enter = Math.max(enter, Math.min(a, b));
      exit = Math.min(exit, Math.max(a, b));
    }
    return exiting ? exit : enter;
  }

  function project(baseEdges: EdgeRenderModel[], individualEdges: EdgeRenderModel[], cards: LeafConnectionBounds[]) {
    const owner = new Map(cards.flatMap(card => card.memberModelIds.map(id => [id, card] as const)));
    const individual = new Map(individualEdges.map(edge => [edge.edgeId, edge]));
    const grouped = new Map<string, EdgeRenderModel[]>();
    const internalEdgeIds: string[] = [];
    const edges: EdgeRenderModel[] = [];
    for (const base of baseEdges) {
      const ids = base.memberEdgeIds?.length ? base.memberEdgeIds : [base.edgeId];
      const visible = ids.map(id => individual.get(id)).filter((edge): edge is EdgeRenderModel => Boolean(edge));
      if (!visible.length) continue;
      // Preserve the currently selected relationship overview. Projecting a
      // node group must not implicitly expand existing June relationship groups.
      let edge = visible.length === ids.length ? base : visible.length === 1 ? visible[0] : merge(visible, base.edgeId);
      let source = owner.get(edge.sourceModelId), target = owner.get(edge.targetModelId);
      if (!source && !target) {edges.push(edge); continue;}
      if (source && source.id === target?.id) {
        const external = visible.find(member => owner.get(member.sourceModelId)?.id !== owner.get(member.targetModelId)?.id);
        if (!external) {internalEdgeIds.push(...visible.map(member => member.edgeId)); continue;}
        edge = {...edge, sourceModelId: external.sourceModelId, targetModelId: external.targetModelId, points: external.points};
        source = owner.get(edge.sourceModelId); target = owner.get(edge.targetModelId);
      }
      const pair = [source?.id ?? edge.sourceModelId, target?.id ?? edge.targetModelId].sort();
      const key = "leaf-connection:" + JSON.stringify(pair);
      const members = grouped.get(key) ?? [];
      members.push(edge);
      grouped.set(key, members);
    }
    for (const [id, members] of grouped) {
      const edge = merge(members, id);
      const source = owner.get(edge.sourceModelId), target = owner.get(edge.targetModelId);
      const [start, end] = points(edge);
      const a = source ? boundaryParameter(start, end, source, true) : 0;
      const b = target ? boundaryParameter(start, end, target, false) : 1;
      // Clip an existing straight connection at the enclosing card. This
      // removes the fanout inside the card without adding a bend or trunk.
      edge.points = [a, b].map(t => [start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t]
        .map(value => Math.round(value * 100) / 100).join(",")).join(" ");
      edge.leafCardEndpointIds = [source?.id ?? null, target?.id ?? null];
      edge.physicalEndpointModelIds = [...(source ? [] : [edge.sourceModelId]), ...(target ? [] : [edge.targetModelId])];
      edges.push(edge);
    }
    return {edges, internalEdgeIds, connectionCount: grouped.size};
  }
  return {bounds, project};
}
