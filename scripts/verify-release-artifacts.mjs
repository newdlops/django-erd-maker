import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  binaryPaths, collectFiles, minimumMacOS, readJson, repoRoot, requiredDocs,
  requireReleaseHost, run, sha256, sourceArchives, target,
} from "./release/common.mjs";
import { projectSourceFiles } from "./release/sources.mjs";
import { smokePackagedRuntime } from "./release/smoke.mjs";

export async function verifyRelease({ root = repoRoot, checkSource = true, smoke = true } = {}) {
  requireReleaseHost();
  const manifest = await readJson(path.join(root, "package.json"));
  const provenance = await readJson(path.join(root, "sources/manifest.json"));
  assert.match(manifest.publisher, /^[a-z0-9][a-z0-9-]*$/i);
  assert.equal(manifest.name, provenance.name);
  assert.equal(manifest.version, provenance.version);
  assert.equal(manifest.license, provenance.license);
  assert.equal(provenance.target, target);
  assert.equal(provenance.minimumMacOS, minimumMacOS);
  assert.equal(manifest.capabilities.untrustedWorkspaces.supported, false);
  assert.equal(manifest.capabilities.virtualWorkspaces.supported, false);
  assert.deepEqual(Object.keys(provenance.archives).sort(), [...sourceArchives].sort());
  assert.deepEqual(Object.keys(provenance.binaries).sort(), [...binaryPaths].sort());
  for (const command of ["openDiagram", "refreshDiagram", "showLog"]) {
    assert.ok(manifest.contributes.commands.some((item) => item.command === `djangoErd.${command}`));
  }
  for (const doc of requiredDocs) {
    const candidates = doc === "LICENSE" ? [doc, "LICENSE.txt"] : [doc, doc.toLowerCase()];
    let found = false;
    for (const candidate of candidates) {
      try { found ||= (await fs.stat(path.join(root, candidate))).size > 0; }
      catch (error) { if (error.code !== "ENOENT") throw error; }
    }
    assert.ok(found, `Missing release document: ${doc}`);
  }
  await fs.access(path.join(root, manifest.main));
  await fs.access(path.join(root, "scripts/vscode-uninstall.mjs"));
  const icon = await fs.readFile(path.join(root, manifest.icon));
  assert.equal(icon.subarray(0, 8).toString("hex"), "89504e470d0a1a0a", "Icon must be PNG");
  assert.ok(icon.readUInt32BE(16) >= 128, "Icon must be at least 128 pixels");
  assert.equal(icon.readUInt32BE(16), icon.readUInt32BE(20), "Icon must be square");
  for (const [file, hash] of Object.entries(provenance.archives)) {
    assert.equal(await sha256(path.join(root, "sources", file)), hash, `Source archive changed: ${file}`);
  }
  for (const [file, hash] of Object.entries(provenance.runtimeFiles)) {
    assert.equal(await sha256(path.join(root, file)), hash, `JavaScript changed: ${file}`);
  }
  for (const [file, hash] of Object.entries(provenance.assets ?? {})) {
    assert.equal(await sha256(path.join(root, file)), hash, `Model preview changed: ${file}`);
  }
  const runtimeFiles = [];
  for (const folder of ["out/extension", "out/shared", "out/webview"]) {
    runtimeFiles.push(...(await collectFiles(path.join(root, folder)))
      .filter((file) => file.endsWith(".js")).map((file) => path.posix.join(folder, file)));
  }
  assert.deepEqual(runtimeFiles.sort(), Object.keys(provenance.runtimeFiles).sort(), "Runtime file set changed");
  const binaries = [];
  for (const [relative, hash] of Object.entries(provenance.binaries)) {
    const file = path.join(root, relative);
    assert.equal(await sha256(file), hash, `Executable changed: ${relative}`);
    assert.ok((await fs.stat(file)).mode & 0o111, `Executable permission missing: ${relative}`);
    assert.match(await run("file", [file], { capture: true }), /Mach-O 64-bit executable arm64/);
    await run("codesign", ["--verify", "--strict", file], { capture: true });
    const linked = (await run("otool", ["-L", file], { capture: true })).split("\n").slice(1)
      .map((line) => line.trim().split(" ")[0]).filter(Boolean);
    assert.ok(linked.length > 0, `No load commands found: ${relative}`);
    for (const library of linked) assert.match(library, /^\/(usr\/lib|System\/Library)\//,
      `Non-system dependency in ${relative}: ${library}`);
    const loadCommands = await run("otool", ["-l", file], { capture: true });
    const osVersion = loadCommands.match(/\bminos\s+(\d+(?:\.\d+)*)/)?.[1];
    assert.ok(osVersion, `Minimum macOS version missing: ${relative}`);
    assert.ok(Number(osVersion.split(".")[0]) <= Number(minimumMacOS.split(".")[0]),
      `Executable needs newer macOS than documented: ${osVersion}`);
    binaries.push({ path: relative, sha256: hash, minimumMacOS: osVersion, linkedLibraries: linked });
  }
  if (checkSource) {
    const lock = await readJson(path.join(repoRoot, "package-lock.json"));
    assert.equal(manifest.version, lock.version);
    assert.equal(manifest.version, lock.packages[""].version);
    assert.equal(manifest.license, lock.packages[""].license);
    const files = await projectSourceFiles();
    assert.deepEqual(files, Object.keys(provenance.projectSourceFiles).sort(), "Source file set changed; prepare again");
    for (const [file, hash] of Object.entries(provenance.projectSourceFiles)) {
      assert.equal(await sha256(path.join(repoRoot, file)), hash, `Source changed; run prepare:release again: ${file}`);
    }
  }
  const smokeResult = smoke ? await smokePackagedRuntime(root) : undefined;
  return { name: manifest.name, version: manifest.version, target, iconSize: icon.readUInt32BE(16), binaries, smoke: smokeResult };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const result = await verifyRelease({ smoke: !process.argv.includes("--package-only") });
  console.log(`release artifacts verified: ${result.name} ${result.version} (${result.target})`);
}
