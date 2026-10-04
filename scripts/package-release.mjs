import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import { repoRoot, run, sha256, target, sourceArchives } from "./release/common.mjs";
import { verifyRelease } from "./verify-release-artifacts.mjs";
import { stageVsix } from "./release/stage.mjs";

const [mode, workArgument] = process.argv.slice(2);
assert.ok(["--stage", "--finish"].includes(mode) && workArgument,
  "Run bash scripts/package-release.sh to package with bounded memory");
const work = path.resolve(workArgument);
assert.ok(work.startsWith(path.join(repoRoot, ".tmp") + path.sep), "Packaging directory must be inside .tmp");
const verified = await verifyRelease({ smoke: false });
if (mode === "--stage") {
  await stageVsix(repoRoot, work);
  console.log(`Staged ${verified.name} ${verified.version} for vsce`);
} else {
  const candidate = path.join(work, "candidate.vsix");
  const installed = path.join(work, "unpacked");
  const archive = JSON.parse(await run("python3", [
    "scripts/release/inspect_vsix.py", candidate, installed,
  ], { capture: true }));
  const runtime = await verifyRelease({ root: path.join(installed, "extension"), checkSource: false });
  const dist = path.join(repoRoot, "dist");
  await fs.mkdir(dist, { recursive: true });
  const fileName = `${verified.name}-${verified.version}-${target}.vsix`;
  const output = path.join(dist, fileName);
  await fs.copyFile(candidate, output);
  const checksum = await sha256(output);
  const checksums = [`${checksum}  ${fileName}`];
  for (const file of sourceArchives) {
    await fs.copyFile(path.join(repoRoot, "sources", file), path.join(dist, file));
    checksums.push(`${await sha256(path.join(dist, file))}  ${file}`);
  }
  await fs.copyFile(path.join(repoRoot, "sources/manifest.json"), path.join(dist, "source-manifest.json"));
  checksums.push(`${await sha256(path.join(dist, "source-manifest.json"))}  source-manifest.json`);
  await fs.writeFile(path.join(dist, "SHA256SUMS"), checksums.join("\n") + "\n");
  await fs.writeFile(path.join(dist, "verification.json"), JSON.stringify({
    checkedAt: new Date().toISOString(), vsix: fileName, sha256: checksum,
    archive, runtime, installedIntoUserProfile: false, published: false,
  }, null, 2) + "\n");
  console.log(`Verified VSIX: ${output}\nSHA-256: ${checksum}`);
}
