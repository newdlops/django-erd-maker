import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const renderModulePath = path.resolve(
  __dirname,
  "../../out/webview/app/renderDiagramDocument.js",
);
const sampleModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/loadPhaseOneSample.js",
);
const { renderDiagramDocument } = require(renderModulePath);
const { loadPhaseOneSample } = require(sampleModulePath);

test("phase8 document renders canvas scene metadata, routed edges, crossings, choices, properties, and methods", () => {
  const html = render();

  assert.match(html, /<canvas[\s\S]*data-erd-drawing-canvas/);
  assert.match(html, /aria-label="Django ERD diagram"/);
  assert.match(html, /id="erd-render-model"/);
  assert.match(html, /data-table-name="blog_post"/);
  assert.match(html, /data-edge-id="edge-post-tags"/);
  assert.match(html, /data-crossing-id="crossing-1"/);
  assert.match(html, /Draft = draft/);
  assert.match(html, /@ display_title -&gt; str/);
  assert.match(html, /fn publish/);
  assert.match(html, /erd-relation-chip--high[\s\S]*accounts\.Author/);
  assert.match(html, /data-layout-mode="hierarchical"/);
  assert.match(html, /id="erd-initial-state"/);
});

test("phase8 document respects method and property visibility state in the inspector", () => {
  const taxonomyHtml = render((payload) => {
    payload.view.selectedModelId = "taxonomy.Tag";
    payload.view.selectedMethodContext = undefined;
  });
  const auditHtml = render((payload) => {
    payload.view.selectedModelId = "audit.AuditLog";
    payload.view.selectedMethodContext = undefined;
  });

  assert.match(taxonomyHtml, /Methods are hidden by the current table view state\./);
  assert.match(auditHtml, /Properties are hidden by the current table view state\./);
});

test("phase8 canvas scene keeps hidden table metadata in the DOM with hidden state", () => {
  const html = render((payload) => {
    const taxonomy = payload.view.tableOptions.find(
      (options) => options.modelId === "taxonomy.Tag",
    );

    if (!taxonomy) {
      throw new Error("taxonomy.Tag table options fixture is missing.");
    }

    taxonomy.hidden = true;
  });

  assert.match(
    html,
    /<div[\s\S]*class="erd-table[^"]*"[\s\S]*data-model-id="taxonomy\.Tag"[\s\S]*hidden/,
  );
  assert.match(html, /data-edge-id="edge-post-author"/);
});

test("phase8 catalog mode expands high-degree tables for relation ports", () => {
  const payload = createCatalogPayload();
  const html = renderDiagramDocument(payload);
  const hubSize = readTableSize(html, "catalog.Hub");
  const leafSize = readTableSize(html, "catalog.Leaf1");

  assert.match(html, /Model catalog mode: model and DB table names only\./);
  assert.ok(hubSize.height > leafSize.height, "hub table should be taller than leaf tables");
  assert.ok(hubSize.width > leafSize.width, "hub table should be wider than leaf tables");
});

function render(mutatePayload) {
  const payload = structuredClone(loadPhaseOneSample());
  mutatePayload?.(payload);
  return renderDiagramDocument(payload);
}

function createCatalogPayload() {
  const payload = structuredClone(loadPhaseOneSample());
  const modelCount = 501;
  const models = Array.from({ length: modelCount }, (_, index) => {
    const modelName = index === 0 ? "Hub" : `Leaf${index}`;

    return {
      declaredBaseClasses: ["models.Model"],
      databaseTableName: `catalog_${modelName.toLowerCase()}`,
      fields: [],
      hasExplicitDatabaseTableName: true,
      identity: {
        appLabel: "catalog",
        id: `catalog.${modelName}`,
        modelName,
        modulePath: "catalog/models.py",
      },
      methods: [],
      properties: [],
    };
  });
  const layoutNodes = models.map((model, index) => ({
    modelId: model.identity.id,
    position: {
      x: 24 + (index % 24) * 260,
      y: 24 + Math.floor(index / 24) * 104,
    },
    size: {
      height: 74,
      width: 236,
    },
  }));
  const structuralEdges = Array.from({ length: 80 }, (_, index) => ({
    id: `edge-leaf-${index + 1}-hub`,
    kind: "foreign_key",
    provenance: "declared",
    sourceModelId: `catalog.Leaf${index + 1}`,
    targetModelId: "catalog.Hub",
  }));

  payload.analyzer.models = models;
  payload.analyzer.summary.discoveredModelCount = models.length;
  payload.graph.methodAssociations = [];
  payload.graph.nodes = models.map((model) => ({
    appLabel: model.identity.appLabel,
    modelId: model.identity.id,
    modelName: model.identity.modelName,
  }));
  payload.graph.structuralEdges = structuralEdges;
  payload.layout.crossings = [];
  payload.layout.nodes = layoutNodes;
  payload.layout.routedEdges = [];
  payload.view.selectedMethodContext = undefined;
  payload.view.selectedModelId = "catalog.Hub";
  payload.view.tableOptions = models.map((model) => ({
    hidden: false,
    modelId: model.identity.id,
    showMethodHighlights: false,
    showMethods: false,
    showProperties: false,
  }));

  return payload;
}

function readTableSize(html, modelId) {
  const tableMarkup = Array.from(
    html.matchAll(/<div\s+class="erd-table[^"]*"[\s\S]*?>/g),
  )
    .map((match) => match[0])
    .find((markup) => markup.includes(`data-model-id="${modelId}"`));
  const match = tableMarkup?.match(
    /data-width="(?<width>\d+)"[\s\S]*?data-height="(?<height>\d+)"/,
  );

  assert.ok(match?.groups, `missing table metadata for ${modelId}`);
  return {
    height: Number(match.groups.height),
    width: Number(match.groups.width),
  };
}
