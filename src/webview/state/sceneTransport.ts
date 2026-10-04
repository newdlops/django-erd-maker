// Keep this factory self-contained: its emitted function is embedded into the
// browser controller, so both ends use exactly the same scene wire format.
export function createSceneTransportTools() {
  const asRecord = (value: unknown): Record<string, unknown> | undefined =>
    value !== null && typeof value === "object"
      ? value as Record<string, unknown> : undefined;

  function visitEdgeLists(scene: Record<string, unknown>, visit: (edges: unknown[]) => void): void {
    const leaf = asRecord(scene.leafCardOverview);
    const relationship = asRecord(scene.relationshipOverview);
    const individual = asRecord(scene.individualView);
    const visited = new Set<unknown>();
    for (const list of [scene.edges, leaf?.baseEdges, leaf?.individualEdges,
      relationship?.individualEdges, individual?.edges]) {
      if (!Array.isArray(list) || visited.has(list)) continue;
      visited.add(list);
      visit(list);
    }
  }

  function visitEdges(scene: Record<string, unknown>, visit: (edge: Record<string, unknown>) => void): void {
    const visited = new Set<unknown>();
    visitEdgeLists(scene, list => {
      for (const value of list) {
        if (visited.has(value)) continue;
        visited.add(value);
        const edge = asRecord(value);
        if (edge) visit(edge);
      }
    });
  }

  function prepare(scene: Record<string, unknown>) {
    const indices = new Map<string, number>();
    visitEdges(scene, edge => {
      if (!Array.isArray(edge.crossingIds)) return;
      for (const id of edge.crossingIds) {
        if (typeof id === "string" && !indices.has(id)) indices.set(id, indices.size);
      }
    });
    // Small diagrams keep the historical representation and avoid overhead.
    if (indices.size < 128) return {scene, replacer: undefined};
    const edgeIndices = new Map<unknown, number>();
    const edgeLists = new Set<unknown[]>();
    visitEdgeLists(scene, list => {
      edgeLists.add(list);
      for (const edge of list) {
        if (!edgeIndices.has(edge)) edgeIndices.set(edge, edgeIndices.size);
      }
    });
    const leaf = asRecord(scene.leafCardOverview);
    const compactLeaf = leaf && leaf.baseEdges === leaf.individualEdges
      ? {...leaf, individualEdges: undefined, individualEdgesUseBase: true} : leaf;
    const compact = {...scene,
      ...(leaf ? {leafCardOverview: compactLeaf} : {}),
      edgeCatalog: [...edgeIndices.keys()],
      crossingIdCatalog: [...indices.keys()],
    };
    return {scene: compact, replacer: (key: string, value: unknown): unknown => {
      if (Array.isArray(value) && edgeLists.has(value)) {
        return value.map(edge => edgeIndices.get(edge));
      }
      if (key !== "crossingIds" || !Array.isArray(value)) return value;
      return value.map(id => typeof id === "string" ? indices.get(id) ?? id : id);
    }};
  }

  function hydrate(scene: Record<string, unknown>): Record<string, unknown> {
    const catalog = scene.crossingIdCatalog;
    if (!Array.isArray(catalog)) return scene;
    const leaf = asRecord(scene.leafCardOverview);
    if (leaf?.individualEdgesUseBase === true) {
      leaf.individualEdges = leaf.baseEdges;
      delete leaf.individualEdgesUseBase;
    }
    const edges = scene.edgeCatalog;
    if (Array.isArray(edges)) {
      visitEdgeLists(scene, list => {
        for (let i = 0; i < list.length; ++i) {
          const id = list[i];
          if (typeof id !== "number") continue;
          if (!Number.isInteger(id) || id < 0 || id >= edges.length || !asRecord(edges[id])) {
            throw new Error("invalid edge reference");
          }
          list[i] = edges[id];
        }
      });
      delete scene.edgeCatalog;
    }
    visitEdges(scene, edge => {
      if (!Array.isArray(edge.crossingIds)) return;
      edge.crossingIds = edge.crossingIds.map(id => {
        if (typeof id !== "number") return id;
        if (!Number.isInteger(id) || id < 0 || id >= catalog.length
          || typeof catalog[id] !== "string") {
          throw new Error("invalid crossing ID reference");
        }
        return catalog[id];
      });
    });
    delete scene.crossingIdCatalog;
    return scene;
  }

  return {prepare, hydrate};
}
