// Prepare the audited review build using unchanged, source-bound native binaries.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {archiveDirectory,binaryPaths,collectFiles,copyFile,digestFiles,readJson,repoRoot,
  sha256,sourceArchives} from './release/common.mjs';
import {projectSourceFiles} from './release/sources.mjs';
import {verifyRelease} from './verify-release-artifacts.mjs';

const previous=await readJson(path.join(repoRoot,'sources/manifest.json'));
const manifest=await readJson(path.join(repoRoot,'package.json'));
for(const file of binaryPaths)assert.equal(await sha256(path.join(repoRoot,file)),previous.binaries[file],file);
for(const [file,hash] of Object.entries(previous.projectSourceFiles)) {
  if(file.startsWith('analyzer/')||file.startsWith('native/ogdf-layout/'))
    assert.equal(await sha256(path.join(repoRoot,file)),hash,`Native source changed; rebuild required: ${file}`);
}
for(const file of sourceArchives.filter(file=>file!=='project-source.tar.gz'))
  assert.equal(await sha256(path.join(repoRoot,'sources',file)),previous.archives[file],file);
const work=await fs.mkdtemp(path.join(repoRoot,'.tmp/preview-release-source-'));
const files=await projectSourceFiles();
const project=path.join(work,'project');await fs.mkdir(project);
for(const file of files)await copyFile(repoRoot,project,file);
await archiveDirectory(project,path.join(repoRoot,'sources/project-source.tar.gz'));
const runtime=[];
for(const folder of ['out/extension','out/shared','out/webview'])
  for(const file of await collectFiles(path.join(repoRoot,folder)))
    if(file.endsWith('.js'))runtime.push(path.posix.join(folder,file));
const assets=[...['layout.json','manifest.json','overview.npz','individual.npz'].map(file=>'media/ml-preview/'+file),
  'media/source-layout/model.bin','media/source-layout/manifest.json'];
const result={...previous,version:manifest.version,createdAt:new Date().toISOString(),
  archives:await digestFiles(path.join(repoRoot,'sources'),sourceArchives),
  runtimeFiles:await digestFiles(repoRoot,runtime),assets:await digestFiles(repoRoot,assets),
  projectSourceFiles:await digestFiles(repoRoot,files),
  build:{nativeBuild:previous.build.nativeBuild??previous.build,nativeBinariesReused:true,nativeSourceInputsMatched:true,
    runtimeCompiler:'esbuild 0.28.2',runtimeBuildMode:'transformation-only',fullTypecheckPassed:false,
    releaseStagesMemoryLimitMiB:128,parallelism:1}};
await fs.writeFile(path.join(repoRoot,'sources/manifest.json'),JSON.stringify(result,null,2)+'\n');
await verifyRelease({smoke:false});
console.log(JSON.stringify({version:manifest.version,nativeBinariesReused:true,nativeSourceInputsMatched:true,
  runtimeFiles:runtime.length,modelAssets:assets.length,sourceFiles:files.length,guardLimitMiB:128}));
