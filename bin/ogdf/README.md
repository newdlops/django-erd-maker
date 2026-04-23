# OGDF native layout binaries

The extension resolves the OGDF layout engine from this platform-specific
bundle directory first:

```text
bin/ogdf/<platform>-<arch>/django-erd-ogdf-layout
bin/ogdf/<platform>-<arch>/django-erd-ogdf-layout.exe
```

Current bundled target:

- `darwin-arm64/django-erd-ogdf-layout`

Fallback behavior for platforms without a bundled binary:

- Use the bundled OGDF source archive in `vendor/ogdf/ogdf-foxglove-202510.tar.gz`
- Build a native wrapper into VS Code `globalStorage`
- Reuse the cached binary on subsequent launches

During development, set `DJANGO_ERD_OGDF_LAYOUT_BIN` to point at a local wrapper
binary. If neither the bundled binary nor the fallback build succeeds, the
extension falls back to the analyzer-provided layout instead of blocking the
webview.
