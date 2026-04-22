# Boundary Audit

## Scope
- `src/extension`: VS Code host integration, process execution, discovery orchestration.
- `src/shared`: stable contracts and pure data helpers shared across host, analyzer, and webview.
- `src/webview`: pure rendering and browser-side interaction code.
- `analyzer/src`: Rust static analysis, graph building, layout, and routing.

## Rules
- `webview` must not import `extension` modules directly.
- `extension` may depend on `shared`, but `shared` must not depend on `extension`.
- analyzer output consumed by the extension must be decoded through `src/shared/protocol`.
- non-Markdown files must stay at or below 500 lines.

## Phase 11 Findings
- Discovery types were moved into `src/shared/protocol/discoveryContract.ts` so the webview no longer imports `extension` internals.
- Timing metadata is carried through the shared bootstrap contract instead of using ad hoc host-only structures.
- Quality checks are automated in `test/integration/phase11-quality-hardening.test.mjs`.

## Current Largest Reviewed Files
- `src/shared/protocol/decodeDiagramBootstrap.ts`
- `analyzer/src/extract/field_extractor.rs`
- `analyzer/src/layout_engine/mod.rs`

All reviewed source files remain under the line limit after Phase 11 changes.
