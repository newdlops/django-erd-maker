Bundled Graphviz runtime lives here.

Expected layout:

- `resources/graphviz/<platform>-<arch>/bin/dot`
- `resources/graphviz/<platform>-<arch>/lib/...`

Examples:

- `resources/graphviz/darwin-arm64/bin/dot`
- `resources/graphviz/linux-x64/bin/dot`
- `resources/graphviz/win32-x64/bin/dot.exe`

The extension resolves the current platform directory first and sets `GVBINDIR`
to the bundled `lib/graphviz` directory, plus platform library search paths,
before invoking the analyzer.

To stage a runtime into the extension package during release preparation:

- `GRAPHVIZ_RUNTIME_SOURCE=/path/to/graphviz-runtime npm run bundle:graphviz`
- `GRAPHVIZ_DOT_SOURCE=/path/to/dot npm run bundle:graphviz`

On macOS, `npm run bundle:graphviz` can run without extra environment variables
and will vendor the installed Homebrew `graphviz` runtime into the current
platform directory.
