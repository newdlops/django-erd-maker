import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';

const require = createRequire(import.meta.url);
const {serializeJsonForScriptTag, serializeSceneForScriptTag} = require('../../out/webview/render/escapeHtml.js');

test('scene JSON safely round-trips mixed text, Unicode and literal escape sequences', () => {
  const text = '</template><script>"<&>\u2028\u2029😀한글\\u003C\\u2028\ud800';
  const value = {text, nested: [text, {text}], missing: undefined, null: null};
  const encoded = serializeJsonForScriptTag(value);
  assert.doesNotMatch(encoded, /[<>&\u2028\u2029]/);
  assert.doesNotMatch(encoded, /[\u007f-\uffff]/);
  assert.deepEqual(JSON.parse(encoded), JSON.parse(JSON.stringify(value)));
  assert.ok(encoded.includes('\\u003C/template\\u003E'));
});

test('large mixed scene text retains every character without chained whole-string copies', () => {
  const text = ('한글<&>\u2028\u2029\\u003C' + 'x'.repeat(997)).repeat(2048);
  const encoded = serializeJsonForScriptTag({text});
  assert.doesNotMatch(encoded, /[<>&\u2028\u2029]/);
  assert.equal(JSON.parse(encoded).text, text);
});

test('chunked scene JSON preserves object omission, array nulls, text and the transport replacer', () => {
  const value = {missing: undefined, active: true, count: 10, nothing: null,
    inspectorModels: [{modelId: '한글.Model', body: '<script>&\\u003C'}],
    list: [undefined, null, {crossingIds: ['c1', 'c2']}], other: {nested: ['한글', undefined]}};
  const replacer = (key, item) => key === 'crossingIds' ? item.map((_, i) => i) : item;
  const encoded = serializeSceneForScriptTag(value, replacer);
  assert.deepEqual(JSON.parse(encoded), JSON.parse(JSON.stringify(value, replacer)));
  assert.doesNotMatch(encoded, /[<>&\u007f-\uffff]/);
});
