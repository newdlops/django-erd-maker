#!/usr/bin/env bash
# Stage, stream the ZIP members, and verify in separate bounded processes.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
release_root="$PWD"
mkdir -p .tmp
release_stage="$(mktemp -d "$release_root/.tmp/vsix-packaging.XXXXXX")"
trap 'node -e '\''require("node:fs").rmSync(process.argv[1], {recursive:true, force:true})'\'' "$release_stage"' EXIT
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- node --max-old-space-size=40 --max-semi-space-size=1 scripts/package-release.mjs --stage "$release_stage"
(
  cd -- "$release_stage"
  python3 "$release_root/scripts/erd-poc/run_memory_bounded.py" --limit-mib 128 -- python3 "$release_root/scripts/release/package_streaming.py" --stage "$release_stage" --out "$release_stage/candidate.vsix"
)
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- node --max-old-space-size=40 --max-semi-space-size=1 scripts/package-release.mjs --finish "$release_stage"
