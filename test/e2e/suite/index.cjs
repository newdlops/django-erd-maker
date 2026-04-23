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
    for (const layoutMode of [
      "circular",
      "graph",
      "neural",
      "flow",
      "radial",
      "clustered",
    ]) {
      const layoutSnapshot = await runWebviewAction({
        layoutMode,
        type: "clickLayoutMode",
      });

      assert.equal(layoutSnapshot.state.layoutMode, layoutMode);
      assert.equal(activeLayoutMode(layoutSnapshot), layoutMode);
      assert.deepEqual(snapshotModelIds(layoutSnapshot), snapshotModelIds(initialSnapshot));
      assert.equal(requireTableSnapshot(layoutSnapshot, "blog.Post").hidden, false);
      assert.notEqual(
        requireEdgeSnapshot(layoutSnapshot, "blog.Post", "accounts.Author").points,
        "",
      );
      assertRenderedSceneVisible(layoutSnapshot);
    }
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
  "E2E-14": async () => {
    const initialSnapshot = await runWebviewAction({ type: "snapshot" });

    assertRenderedSceneVisible(initialSnapshot);
    assertMinimapViewportMatchesState(initialSnapshot);
    assert.equal(activeLayoutMode(initialSnapshot), initialSnapshot.state.layoutMode);

    const pannedSnapshot = await runWebviewAction({
      delta: { x: 96, y: 64 },
      type: "pointerPanBy",
    });

    assert.notEqual(pannedSnapshot.state.viewport.panX, initialSnapshot.state.viewport.panX);
    assert.notEqual(pannedSnapshot.state.viewport.panY, initialSnapshot.state.viewport.panY);
    assertRenderedSceneVisible(pannedSnapshot);
    assertMinimapViewportMatchesState(pannedSnapshot);

    const zoomInSnapshot = await runWebviewAction({
      type: "clickZoomAction",
      zoomAction: "in",
    });

    assert.ok(
      zoomInSnapshot.state.viewport.zoom > pannedSnapshot.state.viewport.zoom,
      "Zoom In should increase viewport zoom.",
    );

    const zoomOutSnapshot = await runWebviewAction({
      type: "clickZoomAction",
      zoomAction: "out",
    });

    assert.ok(
      zoomOutSnapshot.state.viewport.zoom < zoomInSnapshot.state.viewport.zoom,
      "Zoom Out should decrease viewport zoom.",
    );

    const fitSnapshot = await runWebviewAction({
      type: "clickZoomAction",
      zoomAction: "fit",
    });

    assertVisibleTablesInsideViewport(fitSnapshot);
    assertMinimapViewportMatchesState(fitSnapshot);

    const centeredSnapshot = await runWebviewAction({
      type: "clickZoomAction",
      zoomAction: "center",
    });

    assertComponentCenterNearViewportCenter(centeredSnapshot);
    assertMinimapViewportMatchesState(centeredSnapshot);

    for (const layoutMode of [
      "hierarchical",
      "graph",
      "radial",
      "neural",
      "flow",
      "circular",
      "clustered",
    ]) {
      const layoutSnapshot = await runWebviewAction({
        layoutMode,
        type: "clickLayoutMode",
      });

      assert.equal(layoutSnapshot.state.layoutMode, layoutMode);
      assert.equal(activeLayoutMode(layoutSnapshot), layoutMode);
      assertRenderedSceneVisible(layoutSnapshot);
    }
  },
  "E2E-15": async (state) => {
    assert.equal(state.payload.graph.nodes.length, 3);
    await vscode.commands.executeCommand("djangoErd.refreshDiagram");
    const refreshedState = await waitForPanelState();

    assert.deepEqual(modelIds(refreshedState), modelIds(state));
    assert.equal(refreshedState.payload.graph.structuralEdges.length, state.payload.graph.structuralEdges.length);
  },
  "E2E-19": async () => {
    await runWebviewAction({
      key: "nodeSpacing",
      type: "setSetupControl",
      value: 2.35,
    });
    await runWebviewAction({
      key: "edgeDetour",
      type: "setSetupControl",
      value: 2.4,
    });

    await vscode.commands.executeCommand("djangoErd.refreshDiagram");
    await waitForPanelState();

    const refreshedSnapshot = await runWebviewAction({ type: "snapshot" });

    assert.equal(refreshedSnapshot.state.settings.nodeSpacing, 2.35);
    assert.equal(refreshedSnapshot.state.settings.edgeDetour, 2.4);
    assertRenderedSceneVisible(refreshedSnapshot);
    assertVisibleTablesInsideViewport(refreshedSnapshot);
    assertMinimapViewportMatchesState(refreshedSnapshot);
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

function assertRenderedSceneVisible(snapshot) {
  assert.ok(snapshot.scene.canvasRect.width > 320, "Expected webview canvas to have visible width.");
  assert.ok(snapshot.scene.canvasRect.height > 320, "Expected webview canvas to have visible height.");
  assert.ok(snapshot.scene.drawingCanvas.width > 0, "Expected drawing canvas backing width.");
  assert.ok(snapshot.scene.drawingCanvas.height > 0, "Expected drawing canvas backing height.");
  assert.ok(snapshot.scene.canvasInkSample.sampledPixels > 0, "Expected canvas pixels to be sampled.");
  assert.ok(snapshot.scene.canvasInkSample.inkPixels > 0, "Expected rendered canvas to contain drawn pixels.");
  assert.ok(snapshot.scene.visibleTableIds.length > 0, "Expected at least one table shape in the viewport.");
  assert.ok(visibleTableSnapshots(snapshot).length > 0, "Expected visible table metadata.");
}

function assertVisibleTablesInsideViewport(snapshot) {
  const tolerance = 8;
  const { height, width } = snapshot.scene.canvasRect;

  for (const table of visibleTableSnapshots(snapshot)) {
    assert.ok(
      table.screenRect.left >= -tolerance,
      `Expected ${table.modelId} left edge to fit viewport.`,
    );
    assert.ok(
      table.screenRect.top >= -tolerance,
      `Expected ${table.modelId} top edge to fit viewport.`,
    );
    assert.ok(
      table.screenRect.right <= width + tolerance,
      `Expected ${table.modelId} right edge to fit viewport.`,
    );
    assert.ok(
      table.screenRect.bottom <= height + tolerance,
      `Expected ${table.modelId} bottom edge to fit viewport.`,
    );
  }
}

function assertComponentCenterNearViewportCenter(snapshot) {
  const bounds = visibleTableScreenBounds(snapshot);
  const componentCenter = {
    x: (bounds.left + bounds.right) / 2,
    y: (bounds.top + bounds.bottom) / 2,
  };
  const viewportCenter = {
    x: snapshot.scene.canvasRect.width / 2,
    y: snapshot.scene.canvasRect.height / 2,
  };

  assert.ok(
    Math.abs(componentCenter.x - viewportCenter.x) <= 4,
    "Move To Center should align component center with viewport center on the x axis.",
  );
  assert.ok(
    Math.abs(componentCenter.y - viewportCenter.y) <= 4,
    "Move To Center should align component center with viewport center on the y axis.",
  );
}

function assertMinimapViewportMatchesState(snapshot) {
  assert.equal(snapshot.scene.minimap.visible, true, "Expected minimap to be visible.");
  const bounds = visibleTableWorldBounds(snapshot);
  const minimap = snapshot.scene.minimap;
  const expected = expectedMinimapViewportRect(snapshot, bounds, minimap.canvasRect);
  const actual = minimap.viewport;
  const tolerance = 1.5;

  assert.ok(
    Math.abs(actual.x - expected.x) <= tolerance,
    `Expected minimap viewport x=${actual.x} to match ${expected.x}.`,
  );
  assert.ok(
    Math.abs(actual.y - expected.y) <= tolerance,
    `Expected minimap viewport y=${actual.y} to match ${expected.y}.`,
  );
  assert.ok(
    Math.abs(actual.width - expected.width) <= tolerance,
    `Expected minimap viewport width=${actual.width} to match ${expected.width}.`,
  );
  assert.ok(
    Math.abs(actual.height - expected.height) <= tolerance,
    `Expected minimap viewport height=${actual.height} to match ${expected.height}.`,
  );
}

function expectedMinimapViewportRect(snapshot, bounds, minimapCanvasRect) {
  const worldWidth = Math.max(1, bounds.right - bounds.left);
  const worldHeight = Math.max(1, bounds.bottom - bounds.top);
  const padding = 10;
  const scale = Math.max(
    0.0001,
    Math.min(
      (minimapCanvasRect.width - padding * 2) / worldWidth,
      (minimapCanvasRect.height - padding * 2) / worldHeight,
    ),
  );
  const offsetX = (minimapCanvasRect.width - worldWidth * scale) / 2;
  const offsetY = (minimapCanvasRect.height - worldHeight * scale) / 2;
  const zoom = Math.max(snapshot.state.viewport.zoom, 0.005);
  const viewportWorldRect = {
    maxX: (snapshot.scene.drawingCanvas.rectWidth - snapshot.state.viewport.panX) / zoom,
    maxY: (snapshot.scene.drawingCanvas.rectHeight - snapshot.state.viewport.panY) / zoom,
    minX: -snapshot.state.viewport.panX / zoom,
    minY: -snapshot.state.viewport.panY / zoom,
  };
  const rawRect = {
    height: Math.max(2, (viewportWorldRect.maxY - viewportWorldRect.minY) * scale),
    width: Math.max(2, (viewportWorldRect.maxX - viewportWorldRect.minX) * scale),
    x: offsetX + (viewportWorldRect.minX - bounds.left) * scale,
    y: offsetY + (viewportWorldRect.minY - bounds.top) * scale,
  };

  return fitMinimapCursorRect(rawRect, minimapCanvasRect);
}

function fitMinimapCursorRect(rect, minimapCanvasRect) {
  const width = Math.min(minimapCanvasRect.width, Math.max(8, rect.width));
  const height = Math.min(minimapCanvasRect.height, Math.max(8, rect.height));
  const x = rect.x - Math.max(0, width - rect.width) / 2;
  const y = rect.y - Math.max(0, height - rect.height) / 2;

  return {
    height,
    width,
    x: Math.max(0, Math.min(Math.max(0, minimapCanvasRect.width - width), x)),
    y: Math.max(0, Math.min(Math.max(0, minimapCanvasRect.height - height), y)),
  };
}

function visibleTableScreenBounds(snapshot) {
  const tables = visibleTableSnapshots(snapshot);

  assert.ok(tables.length > 0, "Expected visible tables for screen bounds.");
  return tables.reduce(
    (bounds, table) => ({
      bottom: Math.max(bounds.bottom, table.screenRect.bottom),
      left: Math.min(bounds.left, table.screenRect.left),
      right: Math.max(bounds.right, table.screenRect.right),
      top: Math.min(bounds.top, table.screenRect.top),
    }),
    {
      bottom: Number.NEGATIVE_INFINITY,
      left: Number.POSITIVE_INFINITY,
      right: Number.NEGATIVE_INFINITY,
      top: Number.POSITIVE_INFINITY,
    },
  );
}

function visibleTableWorldBounds(snapshot) {
  const tables = visibleTableSnapshots(snapshot);

  assert.ok(tables.length > 0, "Expected visible tables for world bounds.");
  return tables.reduce(
    (bounds, table) => {
      const position = parseTranslate(table.transform);

      return {
        bottom: Math.max(bounds.bottom, position.y + table.height),
        left: Math.min(bounds.left, position.x),
        right: Math.max(bounds.right, position.x + table.width),
        top: Math.min(bounds.top, position.y),
      };
    },
    {
      bottom: Number.NEGATIVE_INFINITY,
      left: Number.POSITIVE_INFINITY,
      right: Number.NEGATIVE_INFINITY,
      top: Number.POSITIVE_INFINITY,
    },
  );
}

function visibleTableSnapshots(snapshot) {
  return snapshot.tables.filter((table) => !table.hidden);
}

function activeLayoutMode(snapshot) {
  const activeButtons = snapshot.layoutButtons.filter((button) => button.active);

  assert.equal(activeButtons.length, 1, "Expected one active layout button.");
  return activeButtons[0].layoutMode;
}

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
