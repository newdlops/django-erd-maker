# Build from source

The first packaged preview targets **Apple Silicon macOS 26 or later**. It is a
desktop extension with native executables, not a universal or web extension.
The current C++ executable requires macOS 26.0; a lower deployment target needs
a separate rebuild and execution checks on that older system.

## Tools

Use Node.js 22, npm, Rust/Cargo 1.95.0 with the `rust-docs` component, Apple Xcode
Command Line Tools, CMake 3.20 or later, Python 3.9 or later, and tar. Packaging
uses `@vscode/vsce` 3.9.2. The tested tool versions are recorded in the generated
`sources/manifest.json`. Rust 1.95.0 and CMake 4.3.2 were available on the initial
preparation host. Build tools are not required by people installing the VSIX.

```sh
npm ci --ignore-scripts
npm install --global @vscode/vsce@3.9.2
```

The npm install skips development dependency lifecycle scripts. ONNX Runtime is
used by separate research work and is neither needed by nor included in the
extension runtime. The TypeScript compiler emits CommonJS modules with no npm
runtime dependencies.

## Developer build

```sh
node --max-old-space-size=96 --max-semi-space-size=1 node_modules/typescript/bin/tsc -p .
env CARGO_BUILD_JOBS=1 CARGO_PROFILE_RELEASE_OPT_LEVEL=1 CARGO_PROFILE_RELEASE_DEBUG=0 CARGO_PROFILE_RELEASE_CODEGEN_UNITS=64 CARGO_PROFILE_RELEASE_LTO=false cargo build --locked --release -j 1 --manifest-path analyzer/Cargo.toml
node scripts/build-ogdf-binary.mjs --parallel 1 --portable true
```

The analyzer resolver accepts `DJANGO_ERD_ANALYZER_BIN`, then the matching
`bin/analyzer/<platform>-<arch>/` executable, then development release/debug
paths. A packaged binary therefore takes precedence over `analyzer/target`.
When working on the Rust analyzer after preparing a release, set the override
to the new development binary or prepare the release again.

The OGDF wrapper uses the supplied Foxglove 202510 archive. `--portable true`
disables optional OpenMP to avoid a Homebrew library dependency. CGAL is off by
default. The wrapper's larger compilation units build at `-O0` to limit compiler
memory; the extracted crossing kernels build at `-O2 -ffp-contract=off` and the
OGDF library uses the Release build configuration. macOS executables receive
an ad-hoc signature at their final location. This is not Apple notarization.

## Prepare and package

Run costly commands sequentially. The preparation script guards its own stages:
512 MiB for TypeScript, Rust and single-worker C++ compilation, and 128 MiB for
source preparation and verification. The analyzer uses optimization level 1,
64 codegen units and no LTO to stay within the compiler cap. Do not wrap
preparation in a second guard; the shared
lock intentionally prevents nested or simultaneous runs.
Cargo vendoring and metadata collection run before the archive parent starts,
so their memory is not added to a waiting Node process.
Final verification also starts after the archive process has exited.

```sh
bash scripts/prepare-release.sh
python3 scripts/erd-poc/run_memory_bounded.py --limit-mib 128 -- node --max-old-space-size=48 --max-semi-space-size=1 --test --test-concurrency=1 test/integration/release-packaging.test.mjs
bash scripts/package-release.sh
```

Equivalent npm entry points are `npm run prepare:release`, `npm run verify:release`,
and `npm run package:vsix`. Preparation compiles current TypeScript and Rust,
builds OGDF with one worker, copies executable files, vendors locked Rust sources,
copies dependency notices, and makes source archives. It can download missing
locked crates from the configured Cargo registry. It reuses the local
`.tmp/ogdf-build-lowmem` CMake build cache.

Verification checks current file hashes against the preparation snapshot. Editing
source, documentation, or assets after preparation requires preparing again.
Packaging then inspects the VSIX file list and executes both native tools from
an isolated extraction, with development overrides removed. The smoke fixture
contains three synthetic models and two relationships. It does not open a user
project or install the extension into an existing VS Code profile.

## Rebuild the source included in a VSIX

Unzip the VSIX into `unpacked-vsix/`, create an empty working directory, and from
that directory run the following, adjusting the relative input path if needed:

```sh
tar -xzf ../unpacked-vsix/extension/sources/project-source.tar.gz
tar -xzf ../unpacked-vsix/extension/sources/rust-dependencies.tar.gz
mkdir -p vendor/ogdf .cargo
cp ../unpacked-vsix/extension/sources/ogdf-foxglove-202510.tar.gz vendor/ogdf/
cp rust-dependencies/config.toml .cargo/config.toml
npm ci --ignore-scripts
node --max-old-space-size=96 --max-semi-space-size=8 node_modules/typescript/bin/tsc -p .
cargo build --frozen --release -j 1 --manifest-path analyzer/Cargo.toml
node scripts/build-ogdf-binary.mjs --parallel 1 --portable true
```

The vendored Cargo configuration allows the Rust build to use the supplied crate
sources without network access. npm dependencies and general-purpose build tools
are installed separately. Two probe source files are included because CMake
references them during configuration; only the main layout executable is built.

The archives provide the source for the packaged build, including uncommitted
working-tree changes. They do not claim byte-for-byte reproducibility: compiler
versions, macOS SDK, build paths, timestamps, and signatures can change output.
The source manifest records tool versions and SHA-256 digests for review.
