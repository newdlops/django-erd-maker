import fs from "node:fs/promises";
import path from "node:path";
import { collectFiles, copyFile, readJson, requiredDocs, sourceArchives } from "./common.mjs";

// Copy explicit release inputs before invoking vsce. Its file discovery would
// otherwise traverse large local research/build trees before applying ignores.
export async function stageVsix(root, destination) {
  const metadata = await readJson(path.join(root, "sources/manifest.json"));
  const files = [
    ...requiredDocs, "media/icon.png", "scripts/vscode-uninstall.mjs", ".vscodeignore",
    "sources/manifest.json", ...sourceArchives.map((file) => `sources/${file}`),
    ...Object.keys(metadata.runtimeFiles), ...Object.keys(metadata.binaries),
    ...Object.keys(metadata.assets ?? {}),
  ];
  for (const folder of ["bin/ogdf/licenses", "licenses"]) {
    for (const file of await collectFiles(path.join(root, folder))) files.push(`${folder}/${file}`);
  }
  for (const file of files) await copyFile(root, destination, file);
  const manifest = await readJson(path.join(root, "package.json"));
  // Compilation and verification have completed in the source checkout. Only
  // the lifecycle script that VS Code itself runs belongs in the installed VSIX.
  manifest.scripts = { "vscode:uninstall": manifest.scripts["vscode:uninstall"] };
  delete manifest.devDependencies;
  await fs.writeFile(path.join(destination, "package.json"), JSON.stringify(manifest, null, 2) + "\n");
}
