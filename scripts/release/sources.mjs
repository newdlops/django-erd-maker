import fs from "node:fs/promises";
import path from "node:path";
import {
  archiveDirectory, collectFiles, copyFile, digestFiles, readJson, repoRoot,
  requiredDocs, run, upstreamArchive,
} from "./common.mjs";

const sourceDirectories = ["src", "analyzer/src", "native/ogdf-layout/src", "docs", "media", "licenses", "scripts/release", "test/fixtures/django/feature_rich_project"];
const sourceFiles = [
  "package.json", "package-lock.json", "tsconfig.json", ".gitignore", ".vscodeignore",
  ".github/workflows/package-preview.yml",
  "analyzer/Cargo.toml", "analyzer/Cargo.lock", "native/ogdf-layout/CMakeLists.txt",
  "CONTRIBUTING.md", ...requiredDocs,
  "scripts/build-ogdf-binary.mjs", "scripts/vscode-uninstall.mjs",
  "scripts/prepare-release.sh", "scripts/prepare-release.mjs", "scripts/package-release.sh", "scripts/package-release.mjs", "scripts/verify-release-artifacts.mjs",
  "scripts/prepare-preview-release.mjs",
  "scripts/erd-poc/run_memory_bounded.py", "scripts/erd-poc/ogdf_planar_backbone.cpp",
  "scripts/erd-poc/bundle_latest_checkpoint_preview.cjs",
  "scripts/erd-poc/ogdf_general_layout_probe.cpp", "test/integration/release-packaging.test.mjs",
  "scripts/erd-poc/straight_visual_optimizer.cpp",
  "test/integration/straight-visual-state.test.mjs", "test/integration/straight-visual-state.cpp",
  "test/integration/source-input-layout.test.mjs", "test/integration/source-input-layout.cpp",
  "test/integration/source-card-clearance.cpp",
  "test/integration/bundled-ml-preview.test.mjs",
  "test/integration/fresh-analysis-latency.test.mjs",
  "test/integration/owned-bootstrap-decoding.test.mjs",
  "test/integration/geometry-rendering.test.mjs",
  "test/integration/scene-transport.test.mjs",
  "test/integration/script-json-escaping.test.mjs",
  "test/integration/visual-knot-budget-native.test.mjs",
  "test/integration/cluster-knot-budget-native.test.mjs",
  "test/integration/rectangle-collision-index.test.mjs",
  "test/integration/rectangle-collision-index.cpp",
  "test/integration/route-bounds-index.test.mjs",
  "test/integration/route-bounds-index.cpp",
  "test/integration/face-raster-grid.test.mjs",
  "test/integration/face-raster-grid.cpp",
  "test/integration/straight-route-candidates.test.mjs",
  "test/integration/straight-route-candidates.cpp",
  "test/e2e/release.cjs", "test/e2e/suite/index.cjs",
  "vendor/ogdf/LICENSE.txt", "vendor/ogdf/LICENSE_GPL_v2.txt", "vendor/ogdf/LICENSE_GPL_v3.txt",
];

export async function projectSourceFiles() {
  const files = [...sourceFiles];
  for (const directory of sourceDirectories) {
    for (const file of await collectFiles(path.join(repoRoot, directory))) {
      files.push(path.posix.join(directory, file));
    }
  }
  return [...new Set(files)].sort();
}

export async function prepareSources(work, toolVersions) {
  const sources = path.join(repoRoot, "sources");
  await fs.mkdir(sources, { recursive: true });
  const rustRoot = path.join(work, "rust-dependencies");
  const vendor = path.join(rustRoot, "vendor");
  await fs.mkdir(rustRoot, { recursive: true });
  const vendorConfig = await fs.readFile(path.join(work, "vendor-config.toml"), "utf8");
  await fs.writeFile(path.join(rustRoot, "config.toml"), vendorConfig.replaceAll(vendor, "rust-dependencies/vendor") + "\n");
  const metadata = await readJson(path.join(work, "cargo-metadata.json"));
  const dependencies = metadata.packages.filter((pkg) => pkg.source).sort((a, b) =>
    `${a.name}@${a.version}`.localeCompare(`${b.name}@${b.version}`));
  const licensesRoot = path.join(repoRoot, "licenses", "rust");
  await fs.mkdir(licensesRoot, { recursive: true });
  const inventory = [];
  for (const pkg of dependencies) {
    if (!pkg.license && !pkg.license_file) throw new Error(`Missing license metadata: ${pkg.name}`);
    const folder = `${pkg.name}-${pkg.version}`;
    const source = path.join(vendor, folder);
    const notices = (await fs.readdir(source)).filter((name) => /^(licen[cs]e|copying|copyright|notice)([._-]|$)/i.test(name));
    if (pkg.license_file && !notices.includes(pkg.license_file)) notices.push(pkg.license_file);
    for (const notice of notices) {
      const input = path.join(source, notice);
      if (!(await fs.stat(input)).isFile()) continue;
      await copyFile(source, path.join(licensesRoot, folder), notice);
    }
    inventory.push({ name: pkg.name, version: pkg.version, license: pkg.license ?? `See ${pkg.license_file}`,
      repository: pkg.repository, source: `rust-dependencies/vendor/${folder}`, notices });
  }
  await fs.writeFile(path.join(licensesRoot, "inventory.json"), JSON.stringify(inventory, null, 2) + "\n");

  const sysroot = await run("rustc", ["--print", "sysroot"], { capture: true });
  const rustDocs = path.join(sysroot, "share/doc/rust");
  const runtimeNotices = path.join(repoRoot, "licenses/rust-toolchain");
  await fs.mkdir(runtimeNotices, { recursive: true });
  await fs.cp(path.join(rustDocs, "licenses"), path.join(runtimeNotices, "licenses"), { recursive: true });
  for (const name of ["COPYRIGHT.html", "COPYRIGHT-library.html"]) await copyFile(rustDocs, runtimeNotices, name);

  await fs.writeFile(path.join(repoRoot, "THIRD_PARTY_NOTICES.md"), renderNotices(inventory, toolVersions.rustc));
  await archiveDirectory(work, path.join(sources, "rust-dependencies.tar.gz"), ["rust-dependencies"]);
  await fs.copyFile(path.join(repoRoot, "vendor/ogdf", upstreamArchive), path.join(sources, upstreamArchive));
  const files = await projectSourceFiles();
  const projectRoot = path.join(work, "project");
  await fs.mkdir(projectRoot, { recursive: true });
  for (const file of files) await copyFile(repoRoot, projectRoot, file);
  await archiveDirectory(projectRoot, path.join(sources, "project-source.tar.gz"));
  return digestFiles(repoRoot, files);
}

function renderNotices(inventory, rustc) {
  return `# Third-party notices

Generated by \`npm run prepare:release\` from Cargo.lock and the installed Rust
toolchain. Third-party terms and copyright notices remain in force. The project's
GPL-3.0-only declaration does not replace these licenses.

## OGDF Foxglove 202510

The native layout executable statically links OGDF, licensed under GPL version 2
or 3 with the exceptions reproduced in \`bin/ogdf/licenses/LICENSE.txt\`. This
distribution uses GPL version 3 for the wrapper and OGDF combination. Both
upstream GPL texts are retained. See the [OGDF license](https://www.ogdf.uni-osnabrueck.de/license/).

The unmodified upstream source distribution, including its embedded notices, is
provided in \`sources/${upstreamArchive}\`. It contains:

| Component | Upstream license | Notice/source location within OGDF |
| --- | --- | --- |
| ABACUS | LGPL | src/ogdf/lib/abacus, include/ogdf/lib/abacus |
| COIN-OR | EPL-1.0, covered by OGDF's stated linking exception | src/coin, include/coin |
| Minisat | MIT | include/ogdf/lib/minisat/LICENSE |
| pugixml | MIT | include/ogdf/lib/pugixml/pugixml.h |
| Backward | MIT | include/ogdf/lib/backward/backward.hpp |
| Bandit (upstream tests) | MIT | test/include/bandit/LICENSE.md |
| Snowhouse (upstream tests) | BSL-1.0 | test/include/bandit/assertion_frameworks/snowhouse/LICENSE_1_0.txt |
| TinyDir (upstream tests) | BSD-2-Clause | test/include/tinydir.h |

This table describes the supplied upstream archive; it does not imply every
test helper is linked into the executable. Original file-level notices govern.
The release disables optional CGAL and OpenMP. Apple system libraries are linked
from the host and are not copied into the VSIX.

## Rust analyzer dependencies

All ${inventory.length} resolved external packages in Cargo.lock, including build
and other-platform dependencies, are listed below. Their full source and notices
are in \`sources/rust-dependencies.tar.gz\`. Copies of available package notice
files are in \`licenses/rust/<name>-<version>/\`; machine-readable metadata is in
\`licenses/rust/inventory.json\`. File-level copyright notices also remain in source.

| Package | Version | Declared license expression |
| --- | --- | --- |
${inventory.map((pkg) => `| ${pkg.name} | ${pkg.version} | ${pkg.license.replaceAll("|", "\\|")} |`).join("\n")}

## Rust runtime and build tools

Analyzer toolchain: \`${rustc}\`. Rust's runtime/library and third-party
attributions are preserved in \`licenses/rust-toolchain/\`, including
\`COPYRIGHT-library.html\` and the toolchain's license texts. Rust source for this
version is available from [rust-lang/rust](https://github.com/rust-lang/rust).

TypeScript, ONNX Runtime, CMake, Cargo, Node.js, and vsce are development tools or
development dependencies. Their executables and npm packages are not included in
this VSIX. The extension's emitted JavaScript uses Node and VS Code APIs supplied
by VS Code. The project lockfiles document build dependencies.

## Corresponding source

\`sources/project-source.tar.gz\` contains this extension's TypeScript, analyzer
Rust, native wrapper C++, build scripts, documentation, and asset source. See
\`docs/BUILDING.md\` inside that archive. Archive and executable SHA-256 digests
are recorded in \`sources/manifest.json\` in the VSIX. General-purpose compilers
and operating-system libraries are not bundled as project source.
`;
}
