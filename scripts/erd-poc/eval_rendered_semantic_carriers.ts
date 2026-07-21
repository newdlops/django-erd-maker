import fs from "node:fs";

import {
  createDiagramRenderModel,
  measureRenderedEdgeNodeIntersections,
  measureRenderedVisualConflicts,
} from "../../src/webview/state/createDiagramRenderModel";

const layoutPath = process.argv[2] ?? "/private/tmp/djerd-e9-wide-rings.json";
const edgePath = process.argv[3]
  ?? "/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-fmmm-kCVZQg/edges.tsv";

const layout = JSON.parse(fs.readFileSync(layoutPath, "utf8"));
const structuralEdges = fs.readFileSync(edgePath, "utf8").trim().split(/\n/).map((line) => {
  const [id, sourceModelId, targetModelId, kind, provenance] = line.split("\t");
  return { id, kind, provenance, sourceModelId, targetModelId };
});
const nodeIds = layout.nodes.map((node: { modelId: string }) => node.modelId);
const payload = {
  analyzer: {
    diagnostics: [],
    models: nodeIds.map((modelId: string) => {
      const [appLabel, ...nameParts] = modelId.split(".");
      return {
        declaredBaseClasses: [],
        fields: [],
        identity: {
          appLabel,
          id: modelId,
          modelName: nameParts.join("."),
        },
        methods: [],
        properties: [],
      };
    }),
    summary: {},
  },
  contractVersion: "semantic-carrier-eval",
  graph: {
    diagnostics: [],
    methodAssociations: [],
    nodes: nodeIds.map((modelId: string) => {
      const [appLabel, ...nameParts] = modelId.split(".");
      return { appLabel, modelId, modelName: nameParts.join(".") };
    }),
    structuralEdges,
  },
  layout,
  layoutExecution: {
    appliedMode: layout.mode,
    engine: "ogdf",
    requestedMode: layout.mode,
    status: "applied",
  },
  view: {
    layoutMode: layout.mode,
    tableOptions: [],
  },
};

const renderStartedAt = performance.now();
const renderModel = createDiagramRenderModel(payload as never);
const renderMs = performance.now() - renderStartedAt;
const metrics = measureRenderedVisualConflicts(renderModel);
const edgeNode = measureRenderedEdgeNodeIntersections(renderModel);
const pointCounts = renderModel.edges.map((edge) =>
  edge.points.trim() ? edge.points.trim().split(/\s+/).length : 0);
const representedEdgeIds = new Set(renderModel.edges.flatMap((edge) =>
  edge.memberEdgeIds ?? [edge.edgeId]));
const missingRoutedEdgeIds = layout.routedEdges
  .map((edge: { edgeId: string }) => edge.edgeId)
  .filter((edgeId: string) => !representedEdgeIds.has(edgeId));
const bundleIds = new Set(Object.keys(renderModel.bundleLeavesByFakeId));
const bundledLeafIds = new Set(Object.values(renderModel.bundleLeavesByFakeId).flat());
const clearanceTables = renderModel.tables.filter((table) =>
  !table.hidden && !bundledLeafIds.has(table.modelId));
const tableOverlapPairs = [];
for (let leftIndex = 0; leftIndex < clearanceTables.length; leftIndex += 1) {
  const left = clearanceTables[leftIndex];
  for (let rightIndex = leftIndex + 1; rightIndex < clearanceTables.length; rightIndex += 1) {
    const right = clearanceTables[rightIndex];
    if (
      Math.min(left.position.x + left.size.width, right.position.x + right.size.width)
        - Math.max(left.position.x, right.position.x) > 0
      && Math.min(left.position.y + left.size.height, right.position.y + right.size.height)
        - Math.max(left.position.y, right.position.y) > 0
    ) {
      tableOverlapPairs.push({
        left: left.modelId,
        leftBundle: bundleIds.has(left.modelId),
        right: right.modelId,
        rightBundle: bundleIds.has(right.modelId),
      });
    }
  }
}
console.log(JSON.stringify({
  diagnostics: renderModel.semanticCarriers,
  edgeMemberReferences: renderModel.edges.reduce(
    (sum, edge) => sum + (edge.memberEdgeIds?.length ?? 1),
    0,
  ),
  maxPointsPerCarrier: Math.max(0, ...pointCounts),
  missingRoutedEdgeIds,
  metrics,
  edgeNodeHits: edgeNode.hits,
  renderedEdges: renderModel.edges.length,
  renderMs,
  tables: renderModel.tables.length,
  tableOverlapPairs,
}, null, 2));
