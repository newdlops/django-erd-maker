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

function render(mutatePayload) {
  const payload = structuredClone(loadPhaseOneSample());
  mutatePayload?.(payload);
  return renderDiagramDocument(payload);
}
