import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const binary = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN ?? path.join(root, 'bin/ogdf',
  `${process.platform}-${process.arch}`, process.platform === 'win32'
    ? 'django-erd-ogdf-layout.exe' : 'django-erd-ogdf-layout');

test('an exhausted knot budget returns every freshly routed relationship at accepted positions', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'djerd-knot-budget-'));
  const positions = [
    ['a', -500, -500], ['b', 500, 500], ['c', -500, 500],
    ['d', 500, -500], ['e', -500, 0], ['f', 500, 0],
  ];
  const nodes = path.join(directory, 'nodes.tsv');
  const edges = path.join(directory, 'edges.tsv');
  const positionsPath = path.join(directory, 'positions.tsv');
  fs.writeFileSync(nodes, positions.map(([id, x, y]) => `${id}\t80\t60\t${x}\t${y}\ttest`).join('\n') + '\n');
  fs.writeFileSync(positionsPath, positions.map(([id, x, y]) => `${id}\t${x}\t${y}`).join('\n') + '\n');
  const relationships = positions.flatMap(([source], i) => positions.slice(i + 1)
    .map(([target]) => `${source}${target}\t${source}\t${target}\tforeign_key\tdeclared`));
  fs.writeFileSync(edges, relationships.join('\n') + '\n');
  const arguments_ = ['layout', '--mode', 'fmmm', '--nodes-file', nodes,
    '--edges-file', edges, '--edge-routing', 'straight', '--positions-tsv', positionsPath];
  const environment = {...process.env, DJERD_LAYOUT_THREADS: '1',
    DJERD_CANONICAL_CROSSING_CACHE: '0', DJERD_DISABLE_WALL_CLOCK_BUDGETS: '0',
    DJERD_VISUAL_KNOT_BUDGET_MS: '0', DJERD_XINGS_DETOUR: '0',
    DJERD_XINGS_DETOUR_FINAL: '0', DJERD_CANONICAL_ROUTE_REPAIR: '0'};
  const run = enabled => {
    // stderr is retained alongside stdout so the stopping condition is checked.
    const result = spawnSync(binary, arguments_, {cwd: root, encoding: 'utf8',
      env: {...environment, DJERD_VISUAL_KNOT: enabled ? '1' : '0'}, timeout: 5000});
    assert.equal(result.status, 0, result.stderr);
    return {layout: JSON.parse(result.stdout), stderr: result.stderr};
  };
  try {
    const baseline = run(false);
    const bounded = run(true);
    assert.match(bounded.stderr, /\[visual-knot\].*budgetHit=1/);
    assert.doesNotMatch(bounded.stderr, /\[visual-knot\] 0→/,
      'the fixture must exercise intersecting routes');
    assert.match(bounded.stderr, /0 swap accepted/);
    assert.equal(bounded.layout.nodes.length, positions.length);
    assert.deepEqual(bounded.layout.nodes, baseline.layout.nodes);
    assert.deepEqual(bounded.layout.routedEdges, baseline.layout.routedEdges);
    assert.equal(bounded.layout.routedEdges.length, relationships.length);
    assert.ok(bounded.layout.routedEdges.every(route => route.points.length >= 2));
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
});
