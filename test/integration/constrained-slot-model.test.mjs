import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import test from 'node:test';
const require = createRequire(import.meta.url);
const { measureRenderedVisualConflicts, measureRenderedTableClearance } = require('../../out/webview/state/createDiagramRenderModel.js');
test('slot factors match the renderer for every injection, including free slots, hits and overlaps', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'erd-slot-model-'));
  try {
    const binary = path.join(directory, 'probe');
    const compile = spawnSync('clang++', ['-O3', '-std=c++17', 'test/integration/constrained-slot-model.cpp', '-o', binary],
      { encoding: 'utf8', timeout: 120000 });
    assert.equal(compile.status, 0, compile.stderr);
    const run = spawnSync(binary, [], { encoding: 'utf8', timeout: 10000 });
    assert.equal(run.status, 0, run.stderr);
    const results = run.stdout.trim().split('\n').map(line => JSON.parse(line));
    assert.equal(results.length, 60);
    let hitCases = 0, overlapCases = 0;
    const ends = [[0, 1], [0, 2], [0, 3], [1, 4], [2, 5], [3, 6], [7, 8]];
    for (const result of results) {
      const model = {
        tables: result.positions.map(([x, y], n) => ({ modelId: String(n), hidden: false,
          position: { x: x - 140, y: y - 60 }, size: { width: 280, height: 120 } })),
        edges: result.routes.map(([ax, ay, bx, by], e) => ({ edgeId: String(e), sourceModelId: String(ends[e][0]),
          targetModelId: String(ends[e][1]), points: [ax, ay].join(',') + ' ' + [bx, by].join(','), preserveRouteEndpoints: true })),
      };
      const metrics = measureRenderedVisualConflicts(model), clearance = measureRenderedTableClearance(model);
      assert.deepEqual(result.score, [metrics.visualCrossings, metrics.edgeCrossings, metrics.edgeNodeIntersections,
        metrics.nodeOverlaps, clearance.spacingViolations]);
      hitCases += metrics.edgeNodeIntersections > 0;overlapCases += metrics.nodeOverlaps > 0;
    }
    assert.ok(hitCases > 0 && overlapCases > 0, 'the fixture must cover both conflict classes');
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});
