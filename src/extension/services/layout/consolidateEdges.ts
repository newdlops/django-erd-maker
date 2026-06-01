import type { StructuralGraphEdge } from "../../../shared/graph/diagramGraph";

export interface ConsolidationGroup {
  count: number;
  representativeEdgeId: string;
  underlyingIds: string[];
  underlyingKinds: string[];
}

export interface ConsolidationResult {
  groupByRepresentativeId: Map<string, ConsolidationGroup>;
  groups: ConsolidationGroup[];
  layoutEdges: StructuralGraphEdge[];
}

export function consolidateEdges(
  edges: readonly StructuralGraphEdge[],
): ConsolidationResult {
  // Unordered pair key: {min, max} so that A→B (declared) and B→A
  // (derived_reverse) collapse into one visual edge. Multiple FKs in
  // the same direction also merge here.
  const grouped = new Map<string, StructuralGraphEdge[]>();
  for (const edge of edges) {
    const a = edge.sourceModelId;
    const b = edge.targetModelId;
    const key = a < b ? `${a}::${b}` : `${b}::${a}`;
    let bucket = grouped.get(key);
    if (!bucket) {
      bucket = [];
      grouped.set(key, bucket);
    }
    bucket.push(edge);
  }
  const groups: ConsolidationGroup[] = [];
  const layoutEdges: StructuralGraphEdge[] = [];
  for (const bucket of grouped.values()) {
    // Inheritance ("is-a") is the most structurally significant tie. When a
    // node pair also carries a data relation (e.g. a model both inherits a
    // parent and declares a FK back to it), surface the inheritance edge so
    // the class hierarchy stays visible instead of being masked by the FK.
    // Otherwise prefer a declared edge to keep FK direction meaningful, then
    // fall back to the first edge.
    const representative =
      bucket.find((edge) => edge.kind === "inheritance") ??
      bucket.find((edge) => edge.provenance === "declared") ??
      bucket[0]!;
    layoutEdges.push(representative);
    groups.push({
      count: bucket.length,
      representativeEdgeId: representative.id,
      underlyingIds: bucket.map((edge) => edge.id),
      underlyingKinds: bucket.map((edge) => edge.kind),
    });
  }
  const groupByRepresentativeId = new Map<string, ConsolidationGroup>();
  for (const group of groups) {
    groupByRepresentativeId.set(group.representativeEdgeId, group);
  }
  return { groupByRepresentativeId, groups, layoutEdges };
}
