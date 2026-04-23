# OGDF source bundle

This directory stores the official OGDF source archive used for runtime fallback
builds on platforms that do not have a prebuilt bundled binary.

Expected archive:

- `ogdf-foxglove-202510.tar.gz`

The build wrapper is implemented under `native/ogdf-layout` and can be compiled
manually with:

```sh
node scripts/build-ogdf-binary.mjs --extension-root . --install-dir bin/ogdf/darwin-arm64
```
