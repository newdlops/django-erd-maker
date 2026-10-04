import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import test, { before, after } from 'node:test';
const require = createRequire(import.meta.url);
const { measureRenderedVisualConflicts, measureRenderedTableClearance } = require('../../out/webview/state/createDiagramRenderModel.js');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'erd-slots-'));
const binary = path.join(directory, 'optimizer');
const nodes = [
  ['hub', 1000, 2000], ['a', 3000, 1000], ['b', 3000, 2000], ['c', 3000, 3000],
  ['ta', 6000, 3000], ['tb', 6000, 1000], ['tc', 6000, 2000],
  ['outside-1', 1000, 5000], ['outside-2', 6000, 5000], ['isolated', 6500, 6000],
];
const edges = [['ha', 'hub', 'a'], ['hb', 'hub', 'b'], ['hc', 'hub', 'c'],
  ['at', 'a', 'ta'], ['bt', 'b', 'tb'], ['ct', 'c', 'tc'], ['outside', 'outside-1', 'outside-2']];
before(() => {
  const compile = spawnSync('clang++', ['-O3', '-std=c++17', '-Wall', '-Wextra',
    'scripts/erd-poc/constrained_visual_optimizer.cpp', '-o', binary], { encoding: 'utf8', timeout: 120000 });
  assert.equal(compile.status, 0, compile.stderr);
  fs.writeFileSync(path.join(directory, 'nodes.tsv'), nodes.map(([id]) => `${id}\t280\t120\n`).join(''));
  fs.writeFileSync(path.join(directory, 'edges.tsv'), edges.map(e => e.join('\t') + '\n').join(''));
  fs.writeFileSync(path.join(directory, 'positions.tsv'), nodes.map(n => n.join('\t') + '\n').join(''));
});
after(() => fs.rmSync(directory, { recursive: true, force: true }));
function run(name, options = []) {
  const output = path.join(directory, name + '.tsv');
  const process = spawnSync(binary, [...options, '--nodes', path.join(directory, 'nodes.tsv'),
    '--edges', path.join(directory, 'edges.tsv'), '--positions', path.join(directory, 'positions.tsv'),
    '--out', output, '--fit', '0', '--width', '7000', '--height', '7000', '--steps', '0', '--seconds', '10'],
  { encoding: 'utf8', timeout: 15000 });
  assert.equal(process.status, 0, process.stderr);
  return { output, log: process.stderr };
}
function audit(result) {
  const positions = new Map(fs.readFileSync(result.output, 'utf8').trim().split('\n').map(row => {
    const [id, x, y] = row.split('\t'); return [id, { x: Number(x), y: Number(y) }];
  }));
  const routes = new Map(fs.readFileSync(result.output + '.routes.tsv', 'utf8').trim().split('\n').map(row => row.split('\t')));
  assert.deepEqual(new Set(positions.keys()), new Set(nodes.map(n => n[0])));
  assert.deepEqual(new Set(routes.keys()), new Set(edges.map(e => e[0])));
  const model = {
    tables: nodes.map(([id]) => ({ modelId: id, hidden: false,
      position: { x: positions.get(id).x - 140, y: positions.get(id).y - 60 }, size: { width: 280, height: 120 } })),
    edges: edges.map(([id, source, target]) => ({ edgeId: id, sourceModelId: source, targetModelId: target,
      points: routes.get(id), preserveRouteEndpoints: true })),
  };
  const metrics = measureRenderedVisualConflicts(model), clearance = measureRenderedTableClearance(model);
  const done = result.log.match(/done step=\d+ visual=(\d+) cross=(\d+) hit=(\d+) overlap=(\d+) spacing=(\d+)/);
  assert.deepEqual(done.slice(1).map(Number), [metrics.visualCrossings, metrics.edgeCrossings, metrics.edgeNodeIntersections,
    metrics.nodeOverlaps, clearance.spacingViolations]);
  assert.equal(metrics.nodeOverlaps + clearance.spacingViolations, 0);
  assert.ok(clearance.bboxArea <= 49000000);
  const movingSlots = new Set(nodes.slice(1, 4).map(([, x, y]) => `${x},${y}`));
  for (const [id, x, y] of nodes) {
    const p = positions.get(id);
    if (['a', 'b', 'c'].includes(id)) assert.ok(movingSlots.delete(`${p.x},${p.y}`), 'slots form a bijection');
    else assert.deepEqual(p, { x, y }, 'cards outside the assignment remain fixed');
  }
  assert.equal(movingSlots.size, 0);
  return metrics;
}
test('joint slot assignment resolves conflicts entirely between moving stars', () => {
  const original = run('original'), baseline = audit(original);
  assert.ok(baseline.edgeCrossings > 0);
  const options = ['--slot-rounds', '1', '--slot-size', '16', '--slot-steps', '2000', '--slot-port-steps', '0', '--seed', '7'];
  const optimized = run('optimized', options), actual = audit(optimized);
  assert.equal(actual.visualCrossings, 0, optimized.log);
  assert.match(optimized.log, /slot round=0 .* variables=3 moved=[23] candidate=0 accepted=1/);
  const repeat = run('repeat', options); audit(repeat);
  for (const suffix of ['', '.routes.tsv']) assert.equal(fs.readFileSync(optimized.output + suffix, 'utf8'), fs.readFileSync(repeat.output + suffix, 'utf8'));
  const replay = run('replay', ['--positions', optimized.output, '--initial-ports', optimized.output + '.routes.tsv']);
  assert.deepEqual(audit(replay), actual);
  const reject = run('reject-near-best', ['--positions', optimized.output, '--initial-ports', optimized.output + '.routes.tsv',
    ...options, '--slot-polish-slack', '16']);
  assert.deepEqual(audit(reject), actual);
  assert.match(reject.log, /slot round=0 .* accepted=0 .* tested=2/);
  for (const suffix of ['', '.routes.tsv']) assert.equal(fs.readFileSync(reject.output + suffix, 'utf8'),
    fs.readFileSync(optimized.output + suffix, 'utf8'), 'a worse trial restores positions and boundary ports');
});
test('slot assignment deadline returns a complete feasible scene', () => {
  const result = run('deadline', ['--slot-rounds', '100000', '--slot-size', '128', '--slot-steps', '100000000', '--seconds', '0.001']);
  assert.ok(audit(result).visualCrossings <= audit(run('baseline-deadline')).visualCrossings);
});
