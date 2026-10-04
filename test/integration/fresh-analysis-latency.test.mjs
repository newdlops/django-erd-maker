import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const {loadPhaseOneSample} = require('../../out/extension/services/loadPhaseOneSample.js');
const {runOgdfLayout} = require('../../out/extension/services/layout/runOgdfLayout.js');

function fixture() {
  const payload = structuredClone(loadPhaseOneSample());
  const ids = new Set(['accounts.Author', 'blog.Post']);
  payload.graph.nodes = payload.graph.nodes.filter(n => ids.has(n.modelId));
  payload.graph.structuralEdges = payload.graph.structuralEdges.filter(e => e.id === 'edge-post-author');
  payload.layout.nodes = payload.layout.nodes.filter(n => ids.has(n.modelId));
  payload.layout.nodes[0].position = {x: 0, y: 0};
  payload.layout.nodes[1].position = {x: 1200, y: 0};
  payload.layout.mode = 'fmmm';
  payload.layout.crossings = [];
  payload.view = {...payload.view, layoutMode: 'fmmm', tableOptions: []};
  return payload;
}

function isolate(t) {
  const saved = new Map(Object.entries(process.env).filter(([key]) => /^(DJERD_|DJANGO_ERD_)/.test(key)));
  for (const key of saved.keys()) delete process.env[key];
  process.env.DJANGO_ERD_OGDF_LAYOUT_BIN = process.execPath;
  t.after(() => {
    for (const key of Object.keys(process.env)) if (/^(DJERD_|DJANGO_ERD_)/.test(key)) delete process.env[key];
    for (const [key, value] of saved) process.env[key] = value;
  });
}

function resultLayout(payload, run) {
  const layout = structuredClone(payload.layout);
  const [author, post] = layout.nodes;
  // Each real native execution produces visibly different positions.
  author.position.y += run * 20;
  post.position.y += run * 20;
  layout.engineMetadata = {edgeBendTotal: 0};
  layout.routedEdges = [{edgeId: 'edge-post-author', crossingIds: [], points: [
    {x: post.position.x, y: post.position.y + post.size.height / 2},
    {x: author.position.x + author.size.width, y: author.position.y + author.size.height / 2}
  ]}];
  return layout;
}

for (const optimized of [false, true]) {
  test(`fresh ${optimized ? 'optimized' : 'ordinary'} requests recompute layout without reading cached results`, async t => {
    isolate(t);
    const payload = fixture();
    const calls = [], cacheReads = [];
    const originalRead = fs.readFile;
    t.mock.method(fs, 'readFile', async (file, ...args) => {
      if (/ml-preview|django-erd-(?:optimized-)?(?:layout|baseline)-cache/.test(String(file))) cacheReads.push(String(file));
      return originalRead(file, ...args);
    });
    t.mock.method(childProcess, 'execFile', (file, args, options, callback) => {
      assert.equal(file, process.execPath);
      assert.ok(Number.isInteger(options.timeout), 'native process deadlines must be integer milliseconds');
      calls.push({args, timeout: options.timeout, env: options.env});
      queueMicrotask(() => callback(null, JSON.stringify(resultLayout(payload, calls.length)), ''));
      return {kill: () => true};
    });
    const run = () => runOgdfLayout(root, structuredClone(payload), 'fmmm', undefined,
      undefined, 'straight', false, false, optimized,
      {freshAnalysis: true, deadlineMs: Date.now() + 30_000.5});
    const first = await run(), second = await run();
    assert.equal(first.applied, true, first.reason);
    assert.equal(second.applied, true, second.reason);
    assert.deepEqual(cacheReads, [], 'fresh requests may not read previews or layout/baseline caches');
    assert.equal(calls.length, 2, 'each request must execute a new layout');
    if (optimized) {
      for (const call of calls) {
        assert.equal(call.args[call.args.indexOf('--mode') + 1], 'hierarchical_barycenter',
          'fresh optimization must start with its required baseline directly');
        assert.equal(call.args[call.args.indexOf('--cluster-graph') + 1], '1',
          'fresh optimization must avoid computing a user-mode layout that it discards');
      }
    }
    assert.notDeepEqual(first.layout.nodes[0].position, second.layout.nodes[0].position);
    assert.ok(calls.every(call => call.timeout > 0 && call.timeout <= 90_000),
      'foreground computation must leave time to render before 120 seconds');
    assert.ok(calls.every(call => call.env.DJERD_FACE_RASTER_CELLS === '1'),
      'the transient face-search grid must fit alongside the live analysis host');
  });
}

test('a fresh request expires while waiting for the resource queue without overlapping another native process', async t => {
  isolate(t);
  const originalRead = fs.readFile;
  t.mock.method(fs, 'readFile', async (file, ...args) => {
    if (/django-erd-(?:optimized-)?(?:layout|baseline)-cache/.test(String(file))) {
      throw Object.assign(new Error('isolated cold run'), {code: 'ENOENT'});
    }
    return originalRead(file, ...args);
  });
  const payload = fixture();
  let calls = 0, active = 0, peak = 0;
  t.mock.method(childProcess, 'execFile', (file, args, options, callback) => {
    calls++; active++; peak = Math.max(peak, active);
    setTimeout(() => {
      active--;
      callback(null, JSON.stringify(resultLayout(payload, calls)), '');
    }, calls === 1 ? 200 : 1);
    return {kill: () => true};
  });
  const run = deadlineMs => runOgdfLayout(root, structuredClone(payload), 'fmmm', undefined,
    undefined, 'straight', false, false, false, {freshAnalysis: true, deadlineMs});
  const first = run(Date.now() + 1000);
  const producerStart = Date.now();
  while (calls === 0 && Date.now() - producerStart < 1000) await new Promise(resolve => setTimeout(resolve, 1));
  assert.ok(calls > 0, 'the producer must reach its native process');
  const start = Date.now();
  const expired = await run(start + 40);
  assert.equal(expired.applied, false);
  assert.match(expired.reason, /queue|budget|deadline|timeout|timed out/i);
  assert.ok(Date.now() - start < 150, 'queue wait must consume the request deadline');
  const third = run(Date.now() + 1000);
  await Promise.all([first, third]);
  assert.equal(calls, 2, 'expired queued work must never execute');
  assert.equal(peak, 1, 'expired queue entries may not release later work ahead of the producer');
});

test('fresh optimization budgets native searches inside the request even when offline search is unlimited', async t => {
  isolate(t);
  process.env.DJERD_OPTIMIZED_TOTAL_BUDGET_MS = '0';
  const payload = fixture(), wide = fixture();
  wide.layout.nodes[1].position = {x: 100_000, y: 100_000};
  const calls = [];
  t.mock.method(childProcess, 'execFile', (file, args, options, callback) => {
    calls.push({args, options});
    queueMicrotask(() => callback(null,
      JSON.stringify(resultLayout(calls.length === 1 ? wide : payload, calls.length)), ''));
    return {kill: () => true};
  });
  const result = await runOgdfLayout(root, payload, 'fmmm', undefined, undefined,
    'straight', false, false, true, {freshAnalysis: true, deadlineMs: Date.now() + 60_000});
  assert.equal(result.applied, true, result.reason);
  const relocation = calls.filter(call => call.options.env.DJERD_KNOT_RELOCATE === '1');
  assert.equal(relocation.length, 1, 'the non-target baseline must exercise live relocation');
  const {env, timeout} = relocation[0].options;
  assert.equal(env.DJERD_DISABLE_WALL_CLOCK_BUDGETS, '0');
  const searchMs = Number(env.DJERD_KNOT_RELOCATE_BUDGET_MS)
    + Number(env.DJERD_RENDERED_CARRIER_GEOMETRY_OPT_BUDGET_MS)
    + Number(env.DJERD_RENDERED_CARRIER_NODE_TARGET_BUDGET_MS);
  assert.ok(searchMs < timeout * 0.8,
    'native searches must leave room for final scoring and JSON output');
});

for (const [label, damage] of [
  ['missing node', layout => layout.nodes.pop()],
  ['duplicate node', layout => { layout.nodes[1] = structuredClone(layout.nodes[0]); }],
  ['missing route', layout => { layout.routedEdges = []; }],
  ['duplicate route', layout => layout.routedEdges.push(structuredClone(layout.routedEdges[0]))],
  ['truncated route', layout => layout.routedEdges[0].points.pop()],
  ['bent route', layout => layout.routedEdges[0].points.splice(1, 0, {x: 600, y: 600})],
]) {
  test(`fresh optimization retains the complete baseline when rerouting returns a ${label}`, async t => {
    isolate(t);
    const payload = fixture(), wide = fixture();
    wide.layout.nodes[1].position = {x: 100_000, y: 100_000};
    const baseline = resultLayout(wide, 1);
    const candidate = resultLayout(payload, 2);
    damage(candidate);
    let calls = 0;
    t.mock.method(childProcess, 'execFile', (file, args, options, callback) => {
      calls++;
      queueMicrotask(() => callback(null, JSON.stringify(calls === 1 ? baseline : candidate), ''));
      return {kill: () => true};
    });
    const result = await runOgdfLayout(root, payload, 'fmmm', undefined, undefined,
      'straight', false, false, true, {freshAnalysis: true, deadlineMs: Date.now() + 60_000});
    assert.equal(calls, 2, 'exercise the live reroute acceptance gate');
    assert.equal(result.applied, true, result.reason);
    assert.deepEqual(result.layout.nodes.map(node => [node.modelId, node.position]),
      baseline.nodes.map(node => [node.modelId, node.position]),
      'lower scores cannot justify losing requested geometry');
    assert.deepEqual(result.layout.routedEdges.map(route => [route.edgeId, route.points]),
      baseline.routedEdges.map(route => [route.edgeId, route.points]));
  });
}
