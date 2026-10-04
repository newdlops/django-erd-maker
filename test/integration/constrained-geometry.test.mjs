import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const require = createRequire(import.meta.url);
const { measureRenderedEdgeCrossings } = require('../../out/webview/state/createDiagramRenderModel.js');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

test('compiled crossing predicates agree with the renderer at shared and collinear decimal endpoints', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'erd-crossing-predicate-'));
  try {
    const source = path.join(directory, 'geometry.cpp'), binary = path.join(directory, 'geometry');
    fs.writeFileSync(source, `#include "constrained_scene.h"
int main(){Point a,b,c,d;while(std::cin>>a.x>>a.y>>b.x>>b.y>>c.x>>c.y>>d.x>>d.y)
  std::cout<<crosses(segment(a,b),segment(c,d))<<'\\n';}
`);
    const build = spawnSync('clang++', ['-O3', '-std=c++17', '-I', path.join(root, 'scripts/erd-poc'), source, '-o', binary],
      { encoding: 'utf8', timeout: 30000 });
    assert.equal(build.status, 0, build.stderr);
    const cases = [
      [[15572.25, 10816.85], [14476.5, 13797.72], [15848.01, 10066.63], [14476.5, 13797.72]],
      [[14476.5, 13797.72], [15572.25, 10816.85], [14476.5, 13797.72], [15848.01, 10066.63]],
      [[10000.01, 10000.01], [10002.01, 10002.01], [10000.01, 10002.01], [10002.01, 10000.01]],
      [[1.01, 1.01], [4.01, 4.01], [2.01, 2.01], [3.01, 3.01]],
    ];
    const expected = cases.map(points => measureRenderedEdgeCrossings({
      tables: points.map((p, i) => ({ modelId: String(i), position: { x: p[0], y: p[1] }, size: { width: 1, height: 1 } })),
      edges: [0, 2].map(i => ({ edgeId: String(i), sourceModelId: String(i), targetModelId: String(i + 1),
        points: points.slice(i, i + 2).map(p => p.join(',')).join(' '), preserveRouteEndpoints: true })),
    }).edgeCrossings);
    assert.deepEqual(expected, [0, 0, 1, 0]);
    const run = spawnSync(binary, [], { input: cases.map(points => points.flat().join(' ')).join('\n'), encoding: 'utf8', timeout: 1000 });
    assert.equal(run.status, 0, run.stderr);
    assert.deepEqual(run.stdout.trim().split('\n').map(Number), expected);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});
