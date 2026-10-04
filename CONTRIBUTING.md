# Contributing

Read [BUILDING](docs/BUILDING.md) for toolchains and reproducible build steps, and
[RELEASING](docs/RELEASING.md) for package checks. Keep source changes focused and
include a synthetic reproduction for analysis or relationship bugs.

The extension is TypeScript, the analyzer is Rust, and the native layout wrapper
is C++ with OGDF. TypeScript runtime code uses VS Code and Node built-ins; the
ONNX development dependency supports research scripts and is not shipped in VSIX.

Respect the boundaries between extension, shared protocol, and webview code.
Preserve canonical relationship coverage and the separation between temporary
Related diagram positions and the saved overview. Changes to visible behavior
need actual visual inspection in addition to relevant tests.

Run the relevant integration tests with one test worker and the repository's
memory guard for large layout work. Do not add production project snapshots,
database data, local paths, generated layout caches, or credentials to a release.
Use the synthetic fixtures under `test/fixtures/django` for reproductions.

By submitting a contribution, you agree that your original contribution is
licensed under this project's GPL-3.0-only terms, unless the contribution clearly
identifies an existing compatible third-party license. Preserve all third-party
copyright and license notices.
