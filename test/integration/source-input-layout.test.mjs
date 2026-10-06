import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const native = path.join(root, 'native/ogdf-layout/src');
function compileOrUse(name, override, sources, directory) {
  if (override) return override;
  const binary = path.join(directory, name);
  const build = spawnSync('python3', [path.join(root, 'scripts/erd-poc/run_memory_bounded.py'),
    '--limit-mib', '512', '--', process.env.CXX ?? 'c++', '-std=c++17', '-O2', '-ffp-contract=off',
    '-I', native, ...sources, '-o', binary], {encoding:'utf8', timeout:60_000});
  assert.equal(build.status, 0, build.stderr);
  return binary;
}

test('source predictor depends on current source and rejects incomplete inputs', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'djerd-source-input-test-'));
  try {
    const binary = compileOrUse('source-input-layout', process.env.DJERD_SOURCE_INPUT_TEST_BIN,
      [path.join(root,'test/integration/source-input-layout.cpp'), path.join(native,'sourceInputLayout.cpp'),
        path.join(native,'straightVisualOptimization.cpp'), path.join(native,'routeBoundsIndex.cpp')], directory);
    const model = path.join(root,'media/source-layout/model.bin');
    const bytes = fs.readFileSync(model);
    const malformed = path.join(directory,'truncated.bin');
    fs.writeFileSync(malformed, bytes.subarray(0,bytes.length-1));
    const run = spawnSync(binary, [model,malformed], {encoding:'utf8',timeout:15_000});
    assert.equal(run.status,0,run.stderr);
    assert.match(run.stdout,/source-input invariance, dynamic dimensions, malformed, unsupported and expired contracts pass/);
  } finally { fs.rmSync(directory,{recursive:true,force:true}); }
});

test('directional card clearance rejects unsafe single, swap and simultaneous moves', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'djerd-source-gap-test-'));
  try {
    const binary = compileOrUse('source-card-clearance', process.env.DJERD_SOURCE_CARD_TEST_BIN,
      [path.join(root,'test/integration/source-card-clearance.cpp'),
        path.join(native,'straightVisualOptimization.cpp'),path.join(native,'routeBoundsIndex.cpp')],directory);
    const run = spawnSync(binary,[],{encoding:'utf8',timeout:15_000});
    assert.equal(run.status,0,run.stderr);
    const result = JSON.parse(run.stdout);
    assert.equal(result.independentIndexedCardClearanceChecks,3000);
    assert.ok(result.singleRejected>0 && result.swapsRejected>0 && result.groupsRejected>0);
    assert.ok(result.acceptedMovesAndIndexUpdatesKeepGap && result.roundoffBoundaryAndInvalidProposalsVerified);
  } finally { fs.rmSync(directory,{recursive:true,force:true}); }
});
