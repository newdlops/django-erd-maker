# Privacy and local data

Django ERD Maker performs analysis on the machine running the VS Code workspace
extension host. It does not upload project source or diagram data to a hosted
analysis service, use a telemetry client, require an account, or contact a database.
In a remote workspace, "local" means the remote extension host.

## Files read

The extension searches the opened workspace for Django project markers, app
definitions, and Python modules. It sends file paths to its bundled Rust analyzer,
which reads and parses Python source. The project is not imported or executed as
Python code. The first folder is used when a workspace contains multiple folders.

## Files and state written

- Temporary analyzer request files and layout input/output files are written under
  the extension host's operating-system temporary directory. The analyzer cleans
  its request directory after a run. Native layout caches and diagnostic files may
  remain, depending on the layout path and development options.
- Native fallback builds, when used in a development/source installation, are
  cached under the extension's VS Code `globalStorage` directory. The supported
  packaged target already includes the native executables.
- The webview retains its view state for navigation and refresh. Layout and
  diagnostic caches can contain model identifiers and positions.
- The **Django ERD Maker** output channel includes project paths, model and module
  names, diagnostic text, timings, and executable paths.
- Opt-in development variables can preserve layout inputs or export analysis
  artifacts. `DJANGO_ERD_EXPORT_ANALYSIS_ARTIFACTS=1` writes JSON exports at the
  extension root; it is off by default. These exports may describe the project
  graph and model metadata.

The extension does not intentionally modify the project's Python models or
database. Build tools and extension installation can use the network; they are
separate from source analysis. VS Code and extension registries have their own
privacy policies.

## Sharing diagnostics

Review logs and exports before attaching them to an issue. Remove sensitive
paths, proprietary identifiers, source excerpts, and any unrelated secrets.
Prefer a minimal synthetic Django project that reproduces the issue.

To discard local diagnostic data, close the diagram, remove only the relevant
`django-erd-*` temporary/cache artifacts you no longer need, and clear the output
channel. Uninstalling an extension does not guarantee that operating-system
temporary files or all VS Code storage are removed.

The native layout tool writes its research face-raster image only when explicitly
enabled with `DJERD_FACE_PPM=1`. This diagnostic goes to `/tmp/face-raster.ppm`
and can reveal diagram geometry. It is disabled by default.
