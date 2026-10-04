import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
test('moving a card updates crossings and its obstruction of unrelated lines exactly', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'djerd-straight-visual-test-'));
  const binary = process.env.DJERD_STRAIGHT_VISUAL_TEST_BIN ?? path.join(directory, 'straight-visual-state-test');
  try {
    if (!process.env.DJERD_STRAIGHT_VISUAL_TEST_BIN) {
      const build = spawnSync('python3', [
        path.join(root, 'scripts/erd-poc/run_memory_bounded.py'), '--limit-mib', '512', '--',
        process.env.CXX ?? 'c++', '-std=c++17', '-O2', '-ffp-contract=off',
        '-I', path.join(root, 'native/ogdf-layout/src'),
        path.join(root, 'test/integration/straight-visual-state.cpp'),
        path.join(root, 'native/ogdf-layout/src/straightVisualOptimization.cpp'),
        path.join(root, 'native/ogdf-layout/src/straightVisualPlacement.cpp'),
        path.join(root, 'native/ogdf-layout/src/routeBoundsIndex.cpp'), '-o', binary,
      ], {encoding: 'utf8', timeout: 60_000, env: {...process.env, MallocNanoZone: '0'}});
      assert.equal(build.status, 0, build.stderr);
    }
    const run = spawnSync(binary, [], {encoding: 'utf8', timeout: 15_000});
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /2000 move evaluations match complete scene audits/);
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
});
