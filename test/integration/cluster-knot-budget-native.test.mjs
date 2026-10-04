import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const binary = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN ?? path.join(root, 'bin/ogdf',
  `${process.platform}-${process.arch}`, 'django-erd-ogdf-layout');

test('an exhausted cluster swap budget preserves accepted positions and all new routes', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'djerd-cluster-knot-budget-'));
  // Four dense communities joined at their highest-degree nodes produce the
  // polar spine needed by the cluster swap pass.
  const positions = Array.from({length: 24}, (_, i) =>
    [`n${i}`, (i % 6) * 240, Math.floor(i / 6) * 360]);
  const nodes = path.join(directory, 'nodes.tsv');
  const edges = path.join(directory, 'edges.tsv');
  const positionsPath = path.join(directory, 'positions.tsv');
  fs.writeFileSync(nodes, positions.map(([id, x, y]) =>
    `${id}\t80\t60\t${x}\t${y}\ttest`).join('\n') + '\n');
  fs.writeFileSync(positionsPath, positions.map(([id, x, y]) =>
    `${id}\t${x}\t${y}`).join('\n') + '\n');
  const pairs = [];
  for (let group = 0; group < 4; group++) {
    for (let a = 0; a < 6; a++) for (let b = a + 1; b < 6; b++)
      pairs.push([group * 6 + a, group * 6 + b]);
  }
  for (let a = 0; a < 4; a++) for (let b = a + 1; b < 4; b++)
    pairs.push([a * 6, b * 6]);
  const relationships = pairs.map(([source, target], i) =>
    `edge${i}\tn${source}\tn${target}\tforeign_key\tdeclared`);
  fs.writeFileSync(edges, relationships.join('\n') + '\n');
  const arguments_ = ['layout', '--mode', 'fmmm', '--nodes-file', nodes,
    '--edges-file', edges, '--edge-routing', 'straight', '--cluster-graph', '1',
    '--positions-tsv', positionsPath];
  const environment = {...process.env, DJERD_LAYOUT_THREADS: '1',
    DJERD_CANONICAL_CROSSING_CACHE: '0', DJERD_DISABLE_WALL_CLOCK_BUDGETS: '0',
    DJERD_KNOT_SWAP_BUDGET_MS: '0', DJERD_KNOT_RELOCATE: '0',
    DJERD_SKIP_CG_OPT: '1', DJERD_NO_PD_KNOT: '1', DJERD_VISUAL_KNOT: '0',
    DJERD_FACE_RASTER: '0', DJERD_FACE_UNTANGLE: '0', DJERD_XINGS_DETOUR: '0',
    DJERD_XINGS_DETOUR_FINAL: '0', DJERD_CANONICAL_ROUTE_REPAIR: '0'};
  const run = enabled => {
    const result = spawnSync(binary, arguments_, {cwd: root, encoding: 'utf8',
      env: {...environment, DJERD_NO_KNOT_MIN: enabled ? '0' : '1'}, timeout: 5000});
    assert.equal(result.status, 0, result.stderr);
    return {layout: JSON.parse(result.stdout), stderr: result.stderr};
  };
  try {
    const baseline = run(false), bounded = run(true);
    assert.match(bounded.stderr, /\[knot-min\].*budgetHit=1/);
    assert.deepEqual(bounded.layout.nodes, baseline.layout.nodes);
    assert.deepEqual(bounded.layout.routedEdges, baseline.layout.routedEdges);
    assert.equal(bounded.layout.routedEdges.length, relationships.length);
    assert.ok(bounded.layout.routedEdges.every(route => route.points.length >= 2));
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
});
