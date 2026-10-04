import fs from "node:fs/promises";
import path from "node:path";
import {
  binaryPaths, collectFiles, digestFiles, minimumMacOS, readJson, repoRoot,
  requireReleaseHost, run, sourceArchives, target,
} from "./release/common.mjs";
import { prepareSources } from "./release/sources.mjs";
import { buildOgdfBinary } from "./build-ogdf-binary.mjs";

requireReleaseHost();
if (!process.argv.includes("--finalize")) {
  throw new Error("Run bash scripts/prepare-release.sh so compiler processes can exit between release stages.");
}
const manifest = await readJson(path.join(repoRoot, "package.json"));
const workParent = path.join(repoRoot, ".tmp");
await fs.mkdir(workParent, { recursive: true });
const work = await fs.mkdtemp(path.join(workParent, "release-source-"));
try {
  console.log(`Preparing ${manifest.name} ${manifest.version} (${target})`);
  const versions = {};
  for (const [name, command, args] of [
    ["node", process.execPath, ["--version"]], ["rustc", "rustc", ["--version"]],
    ["cargo", "cargo", ["--version"]], ["cmake", "cmake", ["--version"]],
    ["clang", "clang++", ["--version"]], ["vsce", "vsce", ["--version"]],
  ]) versions[name] = (await run(command, args, { capture: true })).split("\n")[0];
  const analyzer = path.join(repoRoot, binaryPaths[0]);
  await fs.mkdir(path.dirname(analyzer), { recursive: true });
  await fs.copyFile(path.join(repoRoot, "analyzer/target/release/django-erd-maker-analyzer"), analyzer);
  await fs.chmod(analyzer, 0o755);
  await run("codesign", ["--force", "--sign", "-", analyzer]);
  await buildOgdfBinary([
    "--parallel", "1",
    "--cache-root", path.join(repoRoot, ".tmp/ogdf-build-lowmem"), "--portable", "true", "--install-only", "true",
  ]);
  console.log("\nPreparing corresponding sources and dependency notices…");
  const sourceFiles = await prepareSources(work, versions);
  const runtimeFiles = [];
  for (const folder of ["out/extension", "out/shared", "out/webview"]) {
    for (const file of await collectFiles(path.join(repoRoot, folder))) {
      if (file.endsWith(".js")) runtimeFiles.push(path.posix.join(folder, file));
    }
  }
  const releaseManifest = {
    schemaVersion: 1, name: manifest.name, version: manifest.version, target,
    minimumMacOS, license: manifest.license, createdAt: new Date().toISOString(),
    tools: versions,
    build: { cargoLocked: true, parallelism: 1, cgal: false, openmp: false,
      nativeCompilerMemoryLimitMiB: 1024, otherStagesMemoryLimitMiB: 256,
      cmakeBuildType: "Release", wrapperMainOptimization: "-O0", signing: "ad-hoc" },
    archives: await digestFiles(path.join(repoRoot, "sources"), sourceArchives),
    binaries: await digestFiles(repoRoot, binaryPaths),
    runtimeFiles: await digestFiles(repoRoot, runtimeFiles),
    projectSourceFiles: sourceFiles,
  };
  await fs.writeFile(path.join(repoRoot, "sources/manifest.json"), JSON.stringify(releaseManifest, null, 2) + "\n");
  await run(process.execPath, ["--max-old-space-size=64", "scripts/verify-release-artifacts.mjs"]);
  console.log("Release prepared. Run npm run package:vsix to create and inspect the VSIX.");
} finally {
  await fs.rm(work, { recursive: true, force: true });
}
