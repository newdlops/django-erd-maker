import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import test from 'node:test';

const require = createRequire(import.meta.url);
const {loadPhaseOneSample} = require('../../out/extension/services/loadPhaseOneSample.js');
const {createDiagramRenderModel, measureRenderedVisualConflicts, measureRenderedTableClearance}
  = require('../../out/webview/state/createDiagramRenderModel.js');

for (const catalog of [false, true]) {
  test(`geometry-only auditing preserves the ${catalog ? 'catalog' : 'detailed'} rendered scene`, () => {
    const payload = structuredClone(loadPhaseOneSample());
    if (catalog) {
      const model = payload.analyzer.models[0], graphNode = payload.graph.nodes[0];
      const layoutNode = payload.layout.nodes[0];
      payload.analyzer.models = [];
      payload.graph.nodes = [];
      payload.layout.nodes = [];
      payload.layout.routedEdges = [];
      payload.graph.structuralEdges = [];
      payload.graph.methodAssociations = [];
      payload.view.tableOptions = [];
      for (let i = 0; i < 600; i++) {
        const modelId = `test.Model${i}`;
        payload.analyzer.models.push({...model, identity: {...model.identity,
          id: modelId, modelName: `Model${i}`}});
        payload.graph.nodes.push({...graphNode, modelId});
        payload.layout.nodes.push({...layoutNode, modelId,
          position: {x: (i % 30) * 400, y: Math.floor(i / 30) * 240}});
        if (i > 0) {
          payload.graph.structuralEdges.push({id: `edge${i}`, sourceModelId: modelId,
            targetModelId: `test.Model${i - 1}`, kind: 'foreign_key', provenance: 'declared'});
        }
      }
    }
    const full = createDiagramRenderModel(payload);
    const geometry = createDiagramRenderModel(payload, undefined, {includeInspector: false});
    assert.equal(geometry.inspector.models.length, 0);
    assert.equal(full.inspector.models.length, payload.layout.nodes.length,
      'the ordinary UI must retain complete model inspection');
    const {inspector: fullInspector, ...fullScene} = full;
    const {inspector: geometryInspector, ...geometryScene} = geometry;
    assert.deepEqual(geometryScene, fullScene);
    assert.deepEqual(measureRenderedVisualConflicts(geometry), measureRenderedVisualConflicts(full));
    assert.deepEqual(measureRenderedTableClearance(geometry), measureRenderedTableClearance(full));
  });
}
