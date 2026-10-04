import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';

const require = createRequire(import.meta.url);
const {loadPhaseOneSample} = require('../../out/extension/services/loadPhaseOneSample.js');
const {decodeDiagramBootstrapPayload, decodeOwnedDiagramBootstrapPayload,
  decodeLayoutSnapshot, decodeOwnedLayoutSnapshot}
  = require('../../out/shared/protocol/decodeDiagramBootstrap.js');

test('consuming a newly parsed bootstrap releases raw models progressively while preserving validation', () => {
  const source = JSON.parse(JSON.stringify(loadPhaseOneSample()));
  const original = structuredClone(source);
  const pure = decodeDiagramBootstrapPayload(source);
  assert.deepEqual(source, original, 'regular decoding may not mutate callers');
  assert.notStrictEqual(pure.analyzer.models, source.analyzer.models);

  const owned = structuredClone(original);
  const models = owned.analyzer.models;
  const firstModel = models[0];
  const consumed = decodeOwnedDiagramBootstrapPayload(owned);
  assert.deepEqual(consumed, pure);
  assert.strictEqual(consumed.analyzer.models, models,
    'owned decoding must replace records progressively in the original array');
  assert.notStrictEqual(models[0], firstModel, 'every record still passes through model validation');
});

test('owned bootstrap decoding still rejects invalid fields with their model context', () => {
  const source = JSON.parse(JSON.stringify(loadPhaseOneSample()));
  source.analyzer.models[0].fields[0].name = 42;
  assert.throws(() => decodeOwnedDiagramBootstrapPayload(source),
    /diagramBootstrapPayload\.analyzer\.models\[0\]\.fields\[0\]\.name/);
});

test('owned native layout decoding consumes large arrays without changing validated geometry', () => {
  const source = JSON.parse(JSON.stringify(loadPhaseOneSample().layout));
  const original = structuredClone(source);
  const pure = decodeLayoutSnapshot(source);
  assert.deepEqual(source, original);
  const consumed = decodeOwnedLayoutSnapshot(source);
  assert.deepEqual(consumed, pure);
  for (const key of ['crossings', 'nodes', 'routedEdges']) {
    assert.strictEqual(consumed[key], source[key], key);
    assert.notStrictEqual(pure[key], source[key], key);
  }
  const invalid = structuredClone(original);
  invalid.nodes[0].position.x = 'invalid';
  assert.throws(() => decodeOwnedLayoutSnapshot(invalid), /nodes\[0\]\.position\.x/);
});
