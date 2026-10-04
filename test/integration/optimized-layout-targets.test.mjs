import assert from "node:assert/strict";
import childProcess from "node:child_process";
import { createRequire } from "node:module";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const { loadPhaseOneSample } = require("../../out/extension/services/loadPhaseOneSample.js");
const { runOgdfLayout } = require("../../out/extension/services/layout/runOgdfLayout.js");

for (const scenario of ["pass", "area-miss", "missing-route"]) {
  test(`optimized initial direct scene: ${scenario}`, async (t) => {
    const savedEnv = new Map(Object.entries(process.env).filter(([key]) =>
      /^(DJERD_|DJANGO_ERD_)/.test(key)));
    for (const key of savedEnv.keys()) delete process.env[key];
    Object.assign(process.env, {
      DJANGO_ERD_OGDF_LAYOUT_BIN: process.execPath,
      DJERD_OPTIMIZED_LAYOUT_CACHE: "0",
      DJERD_OPTIMIZED_BASELINE_CACHE: "0",
      DJERD_OPTIMIZED_TOTAL_BUDGET_MS: "60000",
    });
    t.after(() => {
      for (const key of Object.keys(process.env)) {
        if (/^(DJERD_|DJANGO_ERD_)/.test(key)) delete process.env[key];
      }
      for (const [key, value] of savedEnv) process.env[key] = value;
    });

    const payload = structuredClone(loadPhaseOneSample());
    const nodeIds = new Set(["accounts.Author", "blog.Post"]);
    payload.graph.nodes = payload.graph.nodes.filter((n) => nodeIds.has(n.modelId));
    payload.graph.structuralEdges = payload.graph.structuralEdges.filter((e) => e.id === "edge-post-author");
    payload.layout.nodes = payload.layout.nodes.filter((n) => nodeIds.has(n.modelId));
    payload.layout.nodes[0].position = { x: 0, y: 0 };
    payload.layout.nodes[1].position = scenario === "area-miss"
      ? { x: 100_000, y: 100_000 }
      : { x: 1200, y: 0 };
    payload.layout.mode = "fmmm";
    payload.layout.crossings = [];
    payload.layout.engineMetadata = { edgeBendTotal: 0 };
    payload.view = { ...payload.view, layoutMode: "fmmm", tableOptions: [] };

    const calls = [];
    const messages = [];
    t.mock.method(childProcess, "execFile", (file, args, options, callback) => {
      assert.equal(file, process.execPath, "only the isolated native-layout stub may execute");
      calls.push(args);
      const layout = structuredClone(payload.layout);
      const [author, post] = layout.nodes;
      layout.routedEdges = scenario === "missing-route" ? [] : [{
        edgeId: "edge-post-author",
        crossingIds: [],
        points: [
          { x: post.position.x, y: post.position.y + post.size.height / 2 },
          { x: author.position.x + author.size.width, y: author.position.y + author.size.height / 2 },
        ],
      }];
      queueMicrotask(() => callback(null, JSON.stringify(layout), ""));
      return { kill: () => true };
    });

    const result = await runOgdfLayout(root, payload, "fmmm", {
      info: (message) => messages.push(message),
      warn: (message) => messages.push(message),
    }, 1, "straight", true, false, true);

    assert.equal(result.applied, true);
    const stoppedAtInitial = messages.some((message) => message.includes("all targets satisfied after initial layout"));
    assert.equal(stoppedAtInitial, scenario === "pass");
    if (scenario === "pass") {
      assert.equal(calls.length, 1, "a complete target-pass direct scene needs no scorer or reroute");
      assert.equal(result.layout.engineMetadata.visualCrossings, 0);
    } else {
      assert.ok(calls.length > 1, `missing area or routes must keep optimization running:\n${messages.join("\n")}`);
    }
  });
}
