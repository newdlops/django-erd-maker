#!/usr/bin/env bash
# Keep compilers outside a long-lived Node parent to bound aggregate build RSS.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."

for release_tool in node cargo rustc cmake tar codesign vsce python3; do
  command -v "$release_tool" >/dev/null
done
node --input-type=module -e 'if (process.platform !== "darwin" || process.arch !== "arm64") throw new Error("Release requires a darwin-arm64 host")'

# These are generated compiler outputs, never user source or workspace data.
node --input-type=module -e 'import fs from "node:fs/promises"; for (const part of ["extension", "shared", "webview"]) await fs.rm(`out/${part}`, {recursive:true, force:true})'
python3 scripts/erd-poc/run_memory_bounded.py -- node --max-old-space-size=96 --max-semi-space-size=8 node_modules/typescript/bin/tsc -p .
python3 scripts/erd-poc/run_memory_bounded.py -- cargo build --locked --release -j 1 --manifest-path analyzer/Cargo.toml
python3 scripts/erd-poc/run_memory_bounded.py -- node --max-old-space-size=48 scripts/build-ogdf-binary.mjs --parallel 1 --portable true --cache-root .tmp/ogdf-build-lowmem --configure-only true
python3 scripts/release/run_native_build.py --limit-mib 1024
python3 scripts/erd-poc/run_memory_bounded.py -- node --max-old-space-size=64 scripts/prepare-release.mjs --finalize
