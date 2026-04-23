import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const helperModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/diagram/restoreRefreshViewState.js",
);
const { restoreRefreshViewState } = require(helperModulePath);

test("phase11 layout refresh recenters the selected model in the new layout", () => {
  const previous = createLiveDiagramResult({
    mode: "hierarchical",
    nodes: [
      createNode("app.Post", 0, 0),
      createNode("app.Author", 400, 0),
    ],
  });
  const next = createLiveDiagramResult({
    mode: "circular",
    nodes: [
      createNode("app.Post", 1000, 600),
      createNode("app.Author", 1600, 600),
    ],
  });
  const restored = restoreRefreshViewState(
    next,
    previous,
    {
      layoutMode: "hierarchical",
      selectedMethodContext: {
        methodName: "publish",
        modelId: "app.Post",
      },
      selectedModelId: "app.Post",
      tableOptions: [defaultTableOptions("app.Post"), defaultTableOptions("app.Author")],
      viewport: {
        panX: -50,
        panY: -60,
        zoom: 1,
      },
      viewportRect: {
        height: 400,
        width: 500,
      },
    },
    "layout",
  );

  assert.equal(restored.payload.view.layoutMode, "circular");
  assert.equal(restored.payload.view.selectedModelId, "app.Post");
  assert.deepEqual(restored.payload.view.selectedMethodContext, {
    methodName: "publish",
    modelId: "app.Post",
  });
  assert.deepEqual(restored.payload.view.viewport, {
    panX: -800,
    panY: -450,
    zoom: 1,
  });
});

test("phase11 full refresh maps the previous viewport center proportionally onto the new bounds", () => {
  const previous = createLiveDiagramResult({
    mode: "hierarchical",
    nodes: [
      createNode("app.Post", 0, 0),
      createNode("app.Author", 900, 900),
    ],
  });
  const next = createLiveDiagramResult({
    mode: "hierarchical",
    nodes: [
      createNode("app.Post", 100, 100),
      createNode("app.Author", 1000, 1000),
    ],
  });
  const restored = restoreRefreshViewState(
    next,
    previous,
    {
      layoutMode: "hierarchical",
      selectedMethodContext: undefined,
      selectedModelId: undefined,
      tableOptions: [defaultTableOptions("app.Post"), defaultTableOptions("app.Author")],
      viewport: {
        panX: 0,
        panY: -50,
        zoom: 1,
      },
      viewportRect: {
        height: 400,
        width: 500,
      },
    },
    "full",
  );

  assert.deepEqual(restored.payload.view.viewport, {
    panX: -100,
    panY: -150,
    zoom: 1,
  });
});

function createLiveDiagramResult({ mode, nodes }) {
  return {
    discovery: {
      apps: [],
      candidateModelFiles: [],
      candidateModules: [],
      diagnostics: [],
      selectedRoot: "/workspace",
      strategy: "manual",
    },
    payload: {
      analyzer: {
        contractVersion: "1.0.0",
        diagnostics: [],
        models: [
          {
            databaseTableName: "app_post",
            declaredBaseClasses: [],
            fields: [],
            hasExplicitDatabaseTableName: false,
            identity: {
              appLabel: "app",
              id: "app.Post",
              modelName: "Post",
            },
            methods: [
              {
                name: "publish",
                relatedModels: [],
                visibility: "public",
              },
            ],
            properties: [],
          },
          {
            databaseTableName: "app_author",
            declaredBaseClasses: [],
            fields: [],
            hasExplicitDatabaseTableName: false,
            identity: {
              appLabel: "app",
              id: "app.Author",
              modelName: "Author",
            },
            methods: [],
            properties: [],
          },
        ],
        summary: {
          diagnosticCount: 0,
          discoveredAppCount: 1,
          discoveredModelCount: 2,
          workspaceRoot: "/workspace",
        },
      },
      contractVersion: "1.0.0",
      graph: {
        diagnostics: [],
        methodAssociations: [],
        nodes: [],
        structuralEdges: [],
      },
      layout: {
        crossings: [],
        mode,
        nodes,
        routedEdges: [],
      },
      view: {
        layoutMode: mode,
        tableOptions: [],
      },
    },
  };
}

function createNode(modelId, x, y) {
  return {
    modelId,
    position: { x, y },
    size: {
      height: 100,
      width: 100,
    },
  };
}

function defaultTableOptions(modelId) {
  return {
    hidden: false,
    modelId,
    showMethodHighlights: true,
    showMethods: true,
    showProperties: true,
  };
}
