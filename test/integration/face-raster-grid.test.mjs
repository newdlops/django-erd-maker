import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

test('one raster grid preserves obstacle and face labels from two grids', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'djerd-face-grid-'));
  const binary = path.join(directory, 'face-grid-test');
  try {
    const build = spawnSync(process.env.CXX ?? 'c++', [
      '-std=c++17', '-O2', '-I', path.join(root, 'native/ogdf-layout/src'),
      path.join(root, 'test/integration/face-raster-grid.cpp'), '-o', binary,
    ], {encoding: 'utf8', timeout: 60_000});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(binary, [], {encoding: 'utf8', timeout: 15_000});
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /100 flood fills/);
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
});
