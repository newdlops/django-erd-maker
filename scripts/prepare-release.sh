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
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 512 -- node --max-old-space-size=96 --max-semi-space-size=1 node_modules/typescript/bin/tsc -p .
env CARGO_BUILD_JOBS=1 CARGO_PROFILE_RELEASE_OPT_LEVEL=1 CARGO_PROFILE_RELEASE_DEBUG=0 CARGO_PROFILE_RELEASE_CODEGEN_UNITS=64 CARGO_PROFILE_RELEASE_LTO=false \
  python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 512 -- cargo build --locked --release -j 1 --manifest-path analyzer/Cargo.toml
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- node --max-old-space-size=48 --max-semi-space-size=1 scripts/build-ogdf-binary.mjs --parallel 1 --portable true --cache-root .tmp/ogdf-build-lowmem --configure-only true
python3 scripts/release/run_native_build.py --limit-mib 512

# Cargo source collection must also exit before the Node archive parent starts.
release_sources_work="$(mktemp -d "$PWD/.tmp/release-source-XXXXXX")"
trap 'node -e '\''require("node:fs").rmSync(process.argv[1], {recursive:true, force:true})'\'' "$release_sources_work"' EXIT
mkdir -p "$release_sources_work/rust-dependencies"
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- cargo vendor --locked --versioned-dirs --manifest-path analyzer/Cargo.toml "$release_sources_work/rust-dependencies/vendor" > "$release_sources_work/vendor-config.toml"
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- cargo metadata --locked --offline --format-version 1 --manifest-path analyzer/Cargo.toml > "$release_sources_work/cargo-metadata.json"
env MallocNanoZone=0 python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- node --max-old-space-size=40 --max-semi-space-size=1 scripts/prepare-release.mjs --finalize --sources-work "$release_sources_work"
env MallocNanoZone=0 python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- node --max-old-space-size=40 --max-semi-space-size=1 scripts/verify-release-artifacts.mjs
