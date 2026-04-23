const assert = require("node:assert/strict");
const vscode = require("vscode");

const scenarioId = process.env.DJANGO_ERD_E2E_SCENARIO;

exports.run = async function run() {
  if (!scenarioId) {
    throw new Error("DJANGO_ERD_E2E_SCENARIO is required.");
  }

  const scenario = scenarioHandlers[scenarioId];
  if (!scenario) {
    throw new Error(`Unsupported E2E scenario: ${scenarioId}`);
  }

  await sleep(1000);
  await vscode.commands.executeCommand("djangoErd.openDiagram");
  const initialState = await waitForPanelState();

  await scenario(initialState);
};

const scenarioHandlers = {
  "E2E-01": async (state) => {
    assert.match(state.html, /<canvas[\s\S]*Django ERD diagram/);
    assert.match(state.html, /data-table-name="blog_post"/);
    assert.equal(state.payload.analyzer.models.length, 1);
    assert.equal(state.payload.analyzer.models[0].databaseTableName, "blog_post");
    assert.deepEqual(modelIds(state), ["blog.Post"]);
  },
  "E2E-02": async (state) => {
    assert.deepEqual(modelIds(state), [
      "audit.AuditLog",
      "crm.Lead",
      "sales.Receipt",
      "sales.Store",
    ]);
    assert.equal(state.payload.graph.structuralEdges.length, 2);
    assert.ok(state.html.includes("audit.AuditLog"));
    assert.ok(state.html.includes("crm.Lead"));
  },
  "E2E-03": async (state) => {
    assert.ok(edgeKinds(state).includes("foreign_key"));
    assert.ok(edgeKinds(state).includes("many_to_many"));
    assert.ok(edgeKinds(state).includes("reverse_foreign_key"));
    assert.ok(state.html.includes("data-edge-id"));
  },
  "E2E-04": async (state) => {
    const postModel = requireModel(state, "blog.Post");
    const statusField = postModel.fields.find((field) => field.name === "status");

    assert.equal(statusField.choiceMetadata.isChoiceField, true);
    assert.deepEqual(
      statusField.choiceMetadata.options.map((option) => option.label),
      ["Draft", "Published"],
    );
    assert.match(state.html, /Draft = draft/);
  },
  "E2E-05": async (state) => {
    const postModel = requireModel(state, "blog.Post");

    assert.deepEqual(
      postModel.properties.map((property) => property.name),
      ["display_title"],
    );
    assert.match(state.html, /@ display_title -&gt; str/);
  },
  "E2E-06": async (state) => {
    const postModel = requireModel(state, "blog.Post");

    assert.deepEqual(
      postModel.methods.map((method) => method.name).sort(),
      ["publish", "tag_names"],
    );
    assert.match(state.html, /fn publish/);
    assert.match(state.html, /fn tag_names/);
  },
  "E2E-07": async () => {
    await runWebviewAction({ modelId: "blog.Post", type: "clickTable" });
    const snapshot = await runWebviewAction({
      methodName: "publish",
      modelId: "blog.Post",
      type: "clickMethod",
    });

    assert.equal(snapshot.state.selectedModelId, "blog.Post");
    assert.deepEqual(snapshot.state.selectedMethodContext, {
      methodName: "publish",
      modelId: "blog.Post",
    });
    assert.deepEqual(
      activeOverlayTargets(snapshot, "blog.Post", "publish"),
      ["accounts.Author"],
    );
    assert.equal(requireTableSnapshot(snapshot, "accounts.Author").isMethodTarget, true);
    assert.equal(requireTableSnapshot(snapshot, "taxonomy.Tag").isMethodTarget, false);
    assert.deepEqual(
      requirePanelSnapshot(snapshot, "blog.Post").activeMethodNames,
      ["publish"],
    );
  },
  "E2E-08": async () => {
    await runWebviewAction({ modelId: "blog.Post", type: "clickTable" });
    const postHiddenSnapshot = await runWebviewAction({
      modelId: "blog.Post",
      toggle: "showMethods",
      type: "clickTableToggle",
    });

    assert.equal(requireTableSnapshot(postHiddenSnapshot, "blog.Post").showMethods, false);
    assert.equal(requirePanelSnapshot(postHiddenSnapshot, "blog.Post").methodListHidden, true);

    await runWebviewAction({ modelId: "accounts.Author", type: "clickTable" });
    const authorSnapshot = await runWebviewAction({
      type: "snapshot",
    });

    assert.equal(requireTableSnapshot(authorSnapshot, "blog.Post").showMethods, false);
    assert.equal(requireTableSnapshot(authorSnapshot, "accounts.Author").showMethods, true);
    assert.equal(requirePanelSnapshot(authorSnapshot, "accounts.Author").methodListHidden, false);
  },
  "E2E-09": async () => {
    await runWebviewAction({ modelId: "blog.Post", type: "clickTable" });
    const postHiddenSnapshot = await runWebviewAction({
      modelId: "blog.Post",
      toggle: "showProperties",
      type: "clickTableToggle",
    });

    assert.equal(requireTableSnapshot(postHiddenSnapshot, "blog.Post").showProperties, false);
    assert.equal(requirePanelSnapshot(postHiddenSnapshot, "blog.Post").propertyListHidden, true);

    await runWebviewAction({ modelId: "accounts.Author", type: "clickTable" });
    const authorSnapshot = await runWebviewAction({ type: "snapshot" });

    assert.equal(requireTableSnapshot(authorSnapshot, "blog.Post").showProperties, false);
    assert.equal(requireTableSnapshot(authorSnapshot, "accounts.Author").showProperties, true);
    assert.equal(requirePanelSnapshot(authorSnapshot, "accounts.Author").propertyListHidden, false);
  },
  "E2E-10": async () => {
    await runWebviewAction({ modelId: "blog.Post", type: "clickTable" });
    const activeSnapshot = await runWebviewAction({
      methodName: "publish",
      modelId: "blog.Post",
      type: "clickMethod",
    });

    assert.deepEqual(
      activeOverlayTargets(activeSnapshot, "blog.Post", "publish"),
      ["accounts.Author"],
    );
    assert.equal(requireTableSnapshot(activeSnapshot, "accounts.Author").isMethodTarget, true);

    const mutedSnapshot = await runWebviewAction({
      modelId: "blog.Post",
      toggle: "showMethodHighlights",
      type: "clickTableToggle",
    });

    assert.equal(requireTableSnapshot(mutedSnapshot, "blog.Post").showMethodHighlights, false);
    assert.equal(mutedSnapshot.state.selectedMethodContext, undefined);
    assert.deepEqual(
      activeOverlayTargets(mutedSnapshot, "blog.Post", "publish"),
      [],
    );
    assert.equal(requireTableSnapshot(mutedSnapshot, "accounts.Author").isMethodTarget, false);
    assert.equal(requirePanelSnapshot(mutedSnapshot, "blog.Post").methodListHidden, false);
    assert.equal(requireEdgeSnapshot(
      mutedSnapshot,
      "blog.Post",
      "accounts.Author",
    ).hidden, false);

    await runWebviewAction({ modelId: "accounts.Author", type: "clickTable" });
    const authorSnapshot = await runWebviewAction({
      methodName: "featured_posts",
      modelId: "accounts.Author",
      type: "clickMethod",
    });

    assert.equal(requireTableSnapshot(authorSnapshot, "accounts.Author").showMethodHighlights, true);
    assert.deepEqual(
      activeOverlayTargets(authorSnapshot, "accounts.Author", "featured_posts"),
      ["blog.Post"],
    );
  },
  "E2E-11": async () => {
    const initialSnapshot = await runWebviewAction({ type: "snapshot" });
    const initialPositions = tableTransformsByModel(initialSnapshot);
    const initialEdgePoints = requireEdgeSnapshot(
      initialSnapshot,
      "blog.Post",
      "accounts.Author",
    ).points;

    const circularSnapshot = await runWebviewAction({
      layoutMode: "circular",
      type: "clickLayoutMode",
    });

    assert.equal(circularSnapshot.state.layoutMode, "circular");
    assert.deepEqual(snapshotModelIds(circularSnapshot), snapshotModelIds(initialSnapshot));
    assert.notDeepEqual(tableTransformsByModel(circularSnapshot), initialPositions);
    assert.notEqual(
      requireEdgeSnapshot(circularSnapshot, "blog.Post", "accounts.Author").points,
      initialEdgePoints,
    );
    assert.equal(requireTableSnapshot(circularSnapshot, "blog.Post").hidden, false);

    const graphSnapshot = await runWebviewAction({
      layoutMode: "graph",
      type: "clickLayoutMode",
    });

    assert.equal(graphSnapshot.state.layoutMode, "graph");
    assert.deepEqual(snapshotModelIds(graphSnapshot), snapshotModelIds(initialSnapshot));
    assert.notDeepEqual(
      tableTransformsByModel(graphSnapshot),
      tableTransformsByModel(circularSnapshot),
    );
    assert.notEqual(
      requireEdgeSnapshot(graphSnapshot, "blog.Post", "accounts.Author").points,
      requireEdgeSnapshot(circularSnapshot, "blog.Post", "accounts.Author").points,
    );
    const graphPositions = tablePositionsByModel(graphSnapshot);
    const authorTagMidpoint = midpoint(
      graphPositions["accounts.Author"],
      graphPositions["taxonomy.Tag"],
    );

    assert.ok(
      distanceBetweenPoints(graphPositions["blog.Post"], authorTagMidpoint) <
        distanceBetweenPoints(graphPositions["accounts.Author"], authorTagMidpoint),
      "graph layout should pull blog.Post closer to the midpoint of its neighbors",
    );
    assert.ok(
      distanceBetweenPoints(graphPositions["blog.Post"], authorTagMidpoint) <
        distanceBetweenPoints(graphPositions["taxonomy.Tag"], authorTagMidpoint),
      "graph layout should keep blog.Post between connected neighbors",
    );

    const neuralSnapshot = await runWebviewAction({
      layoutMode: "neural",
      type: "clickLayoutMode",
    });

    assert.equal(neuralSnapshot.state.layoutMode, "neural");
    assert.deepEqual(snapshotModelIds(neuralSnapshot), snapshotModelIds(initialSnapshot));
    assert.notDeepEqual(
      tableTransformsByModel(neuralSnapshot),
      tableTransformsByModel(graphSnapshot),
    );
    assert.notEqual(
      requireEdgeSnapshot(neuralSnapshot, "blog.Post", "accounts.Author").points,
      requireEdgeSnapshot(graphSnapshot, "blog.Post", "accounts.Author").points,
    );

    const flowSnapshot = await runWebviewAction({
      layoutMode: "flow",
      type: "clickLayoutMode",
    });

    assert.equal(flowSnapshot.state.layoutMode, "flow");
    assert.deepEqual(snapshotModelIds(flowSnapshot), snapshotModelIds(initialSnapshot));
    assert.notDeepEqual(
      tableTransformsByModel(flowSnapshot),
      tableTransformsByModel(neuralSnapshot),
    );
    assert.notEqual(
      requireEdgeSnapshot(flowSnapshot, "blog.Post", "accounts.Author").points,
      requireEdgeSnapshot(neuralSnapshot, "blog.Post", "accounts.Author").points,
    );

    const radialSnapshot = await runWebviewAction({
      layoutMode: "radial",
      type: "clickLayoutMode",
    });

    assert.equal(radialSnapshot.state.layoutMode, "radial");
    assert.deepEqual(snapshotModelIds(radialSnapshot), snapshotModelIds(initialSnapshot));
    assert.notDeepEqual(
      tableTransformsByModel(radialSnapshot),
      tableTransformsByModel(flowSnapshot),
    );
    assert.notEqual(
      requireEdgeSnapshot(radialSnapshot, "blog.Post", "accounts.Author").points,
      requireEdgeSnapshot(flowSnapshot, "blog.Post", "accounts.Author").points,
    );

    const clusteredSnapshot = await runWebviewAction({
      layoutMode: "clustered",
      type: "clickLayoutMode",
    });

    assert.equal(clusteredSnapshot.state.layoutMode, "clustered");
    assert.deepEqual(snapshotModelIds(clusteredSnapshot), snapshotModelIds(initialSnapshot));
    assert.notDeepEqual(
      tableTransformsByModel(clusteredSnapshot),
      tableTransformsByModel(radialSnapshot),
    );
    assert.notEqual(
      requireEdgeSnapshot(clusteredSnapshot, "blog.Post", "accounts.Author").points,
      requireEdgeSnapshot(radialSnapshot, "blog.Post", "accounts.Author").points,
    );
  },
  "E2E-12": async () => {
    await runWebviewAction({ modelId: "blog.Post", type: "clickTable" });
    const initialSnapshot = await runWebviewAction({ type: "snapshot" });
    const initialPostTransform = requireTableSnapshot(initialSnapshot, "blog.Post").transform;
    const initialAuthorTransform = requireTableSnapshot(initialSnapshot, "accounts.Author").transform;
    const initialTagTransform = requireTableSnapshot(initialSnapshot, "taxonomy.Tag").transform;
    const initialForwardEdge = requireEdgeSnapshot(
      initialSnapshot,
      "blog.Post",
      "accounts.Author",
    ).points;
    const initialReverseEdge = requireEdgeSnapshot(
      initialSnapshot,
      "accounts.Author",
      "blog.Post",
    ).points;

    const draggedSnapshot = await runWebviewAction({
      modelId: "blog.Post",
      position: { x: 520, y: 440 },
      type: "dragTableTo",
    });

    assert.equal(draggedSnapshot.state.selectedModelId, "blog.Post");
    assert.deepEqual(
      tableViewOptions(draggedSnapshot, "blog.Post").manualPosition,
      { x: 520, y: 440 },
    );
    assert.equal(requireTableSnapshot(draggedSnapshot, "blog.Post").transform, "translate(520 440)");
    assert.notEqual(requireTableSnapshot(draggedSnapshot, "blog.Post").transform, initialPostTransform);
    assert.equal(requireTableSnapshot(draggedSnapshot, "accounts.Author").transform, initialAuthorTransform);
    assert.equal(requireTableSnapshot(draggedSnapshot, "taxonomy.Tag").transform, initialTagTransform);
    assert.notEqual(
      requireEdgeSnapshot(draggedSnapshot, "blog.Post", "accounts.Author").points,
      initialForwardEdge,
    );
    assert.notEqual(
      requireEdgeSnapshot(draggedSnapshot, "accounts.Author", "blog.Post").points,
      initialReverseEdge,
    );
  },
  "E2E-13": async () => {
    const selectedSnapshot = await runWebviewAction({
      modelId: "blog.Post",
      type: "pointerSelectTable",
    });

    assert.equal(selectedSnapshot.state.selectedModelId, "blog.Post");
    assert.equal(requireTableSnapshot(selectedSnapshot, "blog.Post").selected, true);

    const initialPosition = parseTranslate(requireTableSnapshot(selectedSnapshot, "blog.Post").transform);
    const draggedSnapshot = await runWebviewAction({
      delta: { x: 120, y: 80 },
      modelId: "blog.Post",
      type: "pointerDragTableBy",
    });
    const draggedPosition = parseTranslate(requireTableSnapshot(draggedSnapshot, "blog.Post").transform);

    assert.ok(draggedPosition.x > initialPosition.x + 80);
    assert.ok(draggedPosition.y > initialPosition.y + 40);
  },
  "E2E-15": async (state) => {
    assert.equal(state.payload.graph.nodes.length, 3);
    await vscode.commands.executeCommand("djangoErd.refreshDiagram");
    const refreshedState = await waitForPanelState();

    assert.deepEqual(modelIds(refreshedState), modelIds(state));
    assert.equal(refreshedState.payload.graph.structuralEdges.length, state.payload.graph.structuralEdges.length);
  },
  "E2E-16": async (state) => {
    assert.deepEqual(modelIds(state), ["orphan.Comment"]);
    assert.equal(state.payload.graph.structuralEdges.length, 0);
    assert.deepEqual(
      state.payload.graph.diagnostics.map((diagnostic) => diagnostic.code),
      ["unresolved_reference"],
    );
    assert.ok(state.html.includes("unresolved_reference"));
  },
  "E2E-17": async (state) => {
    assert.deepEqual(modelIds(state), [
      "accounts.Author",
      "blog.Post",
      "taxonomy.Tag",
    ]);
    assert.equal(state.discovery.apps.length, 3);
    assert.equal(state.payload.analyzer.summary.discoveredAppCount, 3);
  },
  "E2E-18": async (state) => {
    assert.deepEqual(modelIds(state), ["catalog.BaseRecord", "catalog.Product"]);
    assert.equal(requireModel(state, "catalog.BaseRecord").databaseTableName, "catalog_baserecord");
    assert.equal(requireModel(state, "catalog.Product").databaseTableName, "catalog_product_entity");
    assert.match(state.html, /data-table-name="catalog_product_entity"/);
  },
};

function edgeKinds(state) {
  return state.payload.graph.structuralEdges.map((edge) => edge.kind);
}

function modelIds(state) {
  return state.payload.graph.nodes.map((node) => node.modelId).sort();
}

function requireModel(state, modelId) {
  const model = state.payload.analyzer.models.find(
    (candidate) => candidate.identity.id === modelId,
  );

  assert.ok(model, `Expected model ${modelId} to be present.`);
  return model;
}

function requireEdgeSnapshot(snapshot, sourceModelId, targetModelId) {
  const edge = snapshot.edges.find((candidate) =>
    candidate.sourceModelId === sourceModelId &&
    candidate.targetModelId === targetModelId,
  );

  assert.ok(
    edge,
    `Expected edge ${sourceModelId} -> ${targetModelId} to be present.`,
  );
  return edge;
}

function snapshotModelIds(snapshot) {
  return snapshot.tables.map((table) => table.modelId).sort();
}

function requirePanelSnapshot(snapshot, modelId) {
  const panel = snapshot.panels.find((candidate) => candidate.modelId === modelId);

  assert.ok(panel, `Expected panel ${modelId} to be present.`);
  return panel;
}

function requireTableSnapshot(snapshot, modelId) {
  const table = snapshot.tables.find((candidate) => candidate.modelId === modelId);

  assert.ok(table, `Expected table ${modelId} to be present.`);
  return table;
}

function tableTransformsByModel(snapshot) {
  return Object.fromEntries(
    snapshot.tables
      .map((table) => [table.modelId, table.transform])
      .sort(([left], [right]) => left.localeCompare(right)),
  );
}

function tablePositionsByModel(snapshot) {
  return Object.fromEntries(
    snapshot.tables
      .map((table) => [table.modelId, parseTranslate(table.transform)])
      .sort(([left], [right]) => left.localeCompare(right)),
  );
}

function parseTranslate(transform) {
  const match = transform.match(
    /^translate\((?<x>-?\d+(?:\.\d+)?) (?<y>-?\d+(?:\.\d+)?)\)$/,
  );

  assert.ok(match?.groups, `Expected translate transform, got ${transform}`);
  return {
    x: Number(match.groups.x),
    y: Number(match.groups.y),
  };
}

function midpoint(left, right) {
  return {
    x: (left.x + right.x) / 2,
    y: (left.y + right.y) / 2,
  };
}

function distanceBetweenPoints(left, right) {
  return Math.hypot(left.x - right.x, left.y - right.y);
}

function tableViewOptions(snapshot, modelId) {
  const options = snapshot.state.tableOptions.find((candidate) => candidate.modelId === modelId);

  assert.ok(options, `Expected view options ${modelId} to be present.`);
  return options;
}

function activeOverlayTargets(snapshot, sourceModelId, methodName) {
  return snapshot.overlays
    .filter((overlay) =>
      overlay.active &&
      overlay.sourceModelId === sourceModelId &&
      overlay.methodName === methodName,
    )
    .map((overlay) => overlay.targetModelId)
    .sort();
}

async function sleep(durationMs) {
  await new Promise((resolve) => setTimeout(resolve, durationMs));
}

async function waitForPanelState() {
  for (let attempt = 0; attempt < 120; attempt += 1) {
    const state = await vscode.commands.executeCommand("djangoErd.__test.getPanelState");

    if (state && state.html && state.payload) {
      return state;
    }

    await sleep(250);
  }

  throw new Error("Timed out waiting for ERD panel state.");
}

async function runWebviewAction(action) {
  const snapshot = await vscode.commands.executeCommand(
    "djangoErd.__test.runWebviewAction",
    action,
  );

  assert.ok(snapshot, `Expected webview action ${action.type} to return a snapshot.`);
  return snapshot;
}
