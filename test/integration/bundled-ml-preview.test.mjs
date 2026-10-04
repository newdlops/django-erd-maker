import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import test from 'node:test';
const require=createRequire(import.meta.url);
const {loadPhaseOneSample}=require('../../out/extension/services/loadPhaseOneSample.js');
const {loadBundledMlPreview,previewGraphSignature}=require('../../out/extension/services/layout/bundledMlPreview.js');

test('bundled preview loads only for the original graph and card dimensions',async()=>{
  const root=await fs.mkdtemp(path.join(os.tmpdir(),'django-erd-ml-preview-'));
  try {
    const folder=path.join(root,'media/ml-preview');await fs.mkdir(folder,{recursive:true});
    const payload=loadPhaseOneSample();
    const text=JSON.stringify(payload.layout);
    const manifest={schemaVersion:1,graphSignature:previewGraphSignature(payload),
      layoutSha256:createHash('sha256').update(text).digest('hex'),overviewVisual:285,individualVisual:1963};
    await fs.writeFile(path.join(folder,'manifest.json'),JSON.stringify(manifest));
    await fs.writeFile(path.join(folder,'layout.json'),text);
    const loaded=await loadBundledMlPreview(root,payload);
    assert.equal(loaded.text,text);
    for(const mutate of [
      p=>p.graph.nodes.pop(),
      p=>p.graph.structuralEdges.pop(),
      p=>p.graph.structuralEdges[0].targetModelId+='changed',
      p=>p.layout.nodes[0].size.width+=1,
      p=>p.layout.nodes[0].size.height+=1,
    ]) {
      const changed=structuredClone(payload);mutate(changed);
      assert.equal(await loadBundledMlPreview(root,changed),undefined);
    }
    const reordered=structuredClone(payload);
    reordered.graph.nodes.reverse();reordered.graph.structuralEdges.reverse();reordered.layout.nodes.reverse();
    assert.equal((await loadBundledMlPreview(root,reordered)).text,text);
    await fs.writeFile(path.join(folder,'layout.json'),text+' ');
    assert.equal(await loadBundledMlPreview(root,payload),undefined);
    await fs.unlink(path.join(folder,'manifest.json'));
    assert.equal(await loadBundledMlPreview(root,payload),undefined);
  } finally {await fs.rm(root,{recursive:true,force:true});}
});
