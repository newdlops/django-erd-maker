# Django ERD Maker

Explore the structure of a Django project inside Visual Studio Code. Find a model,
follow its fields and relationships, and open a compact diagram of its direct
connections.

[한국어](README.ko.md) · [Support](SUPPORT.md) · [Privacy](PRIVACY.md)

## What you can do

- Discover Django models across a workspace using static Python source analysis.
- Inspect fields, properties, methods, foreign keys, one-to-one and many-to-many
  relationships, reverse relationships, and inheritance.
- Search models, highlight their direct connections, and pan or zoom the diagram.
- Open **Related diagram** to read a model's immediate neighbors in a compact,
  paginated view. Follow another model and return with **Back**.
- Open **Circular view** to place every model on an app-ordered ring. Trace a
  model's connections, inspect individual fields, and zoom or pan independently.
- Inspect grouped leaf models through one card and one connection per external
  peer when the layout contains leaf groups.
- Refresh the diagram after changing source files and inspect discovery or
  analysis diagnostics in the extension's output channel.

The analyzer parses source files; it does not import your Django project, run
`manage.py`, query a database, or require a running development server.

## Requirements

The current preview package supports **macOS 26 or later on Apple Silicon (`darwin-arm64`)**
and **VS Code 1.100 or later**. Native tools are included. Python, Django, Rust,
and CMake do not need to be installed to use this VSIX.

Open a local Django project in a trusted workspace. For Remote SSH, containers,
or WSL, the extension runs on the workspace host: that host must have a supported
package. Windows, Linux, Intel Mac, and browser-only VS Code are not supported by
this release package.

## Install and open

1. Download the matching `.vsix` from this project's
   [releases](https://github.com/newdlops/django-erd-maker/releases).
2. In VS Code, open **Extensions → … → Install from VSIX…** and select the file.
3. Open the Django project folder. In a multi-root workspace, the first workspace
   folder is used to discover the project.
4. Run **Django ERD: Open Diagram** from the Command Palette.
5. Search for a model and press **Enter**, then choose **Related diagram** to
   gather its direct connections.

If a release has not been published yet, see the repository's
[release guide](docs/RELEASING.md) to build a local VSIX.

## Navigation

| Action | How |
| --- | --- |
| Find a model | Search above the diagram; Enter visits the next match, Shift+Enter the previous one |
| Focus search | Cmd+F on macOS, Ctrl+F on other development hosts |
| Inspect connections | Select a model, then use the model panel's search and direction filters |
| Read a compact neighborhood | Choose Related diagram or select a connection row |
| Explore the entire catalog on a ring | Choose Circular view; search or use the Model picker |
| Navigate the circular graph | Left/Right selects models; Shift+arrows pans; +/− zooms; Fit restores the whole ring |
| Follow a model | Choose Explore model, or Explore a member on a grouped card |
| Return through exploration | Back |
| Return to the original overview | Full diagram; Escape outside text inputs also exits the related view |
| Clear the overview selection | Click empty space on the diagram |
| Inspect layout options | Expand Layout & view in the overview |

Related diagram contains direct relationships only. Its cards use temporary
positions, and pagination counts physical peer cards, which can each represent
several models or fields. These positions do not replace the full diagram's layout.

## Commands

| Command | Purpose |
| --- | --- |
| Django ERD: Open Diagram | Discover and visualize the current Django workspace |
| Django ERD: Refresh Diagram | Refresh an open diagram from the workspace |
| Django ERD: Show Logs | Open the Django ERD Maker output channel |

## Preview limitations

Static analysis cannot fully resolve models created dynamically at runtime or
every custom field and metaprogramming pattern. Diagnostics report unresolved
references and parsing problems. Very large projects can take longer to lay out;
the optimized layout mode is experimental and can run for an extended period.

Overview connection filters currently narrow the model panel; the compact related
diagram also applies them to its cards. Selecting a leaf group and then changing
a filter returns to the model-wide relationship scope. See the
[release notes](CHANGELOG.md) for the preview's known interaction limitations.

## Data and licensing

Analysis runs locally. This extension does not send project source or diagram
data to a hosted analysis service and contains no analytics or telemetry client.
Logs and layout caches can contain project paths and model names; review them
before sharing. See [Privacy](PRIVACY.md).

Django ERD Maker is licensed under **GPL-3.0-only**, except third-party components
which retain their own licenses. The VSIX includes corresponding project and
native engine source archives under `sources/`, with build instructions and
third-party notices. See [LICENSE](LICENSE), [NOTICE](NOTICE.md), and
[THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md).

This is an independent project, not affiliated with the Django Software Foundation
or Microsoft.
