# Changelog

## 0.0.1070 — Latest ML checkpoint review (2026-10-04)

- Update the bundled preview to the latest source-cell neural checkpoints,
  trained for 25 overview and 20 individual-view updates.
- Preserve the verified overview 285 / individual 1,963 layout, all 1,244
  original models and 1,727 relationships. No additional reduction is claimed.
- Retain the source-cell and joint body/port experiment code, measurements and
  replay evidence. The 150 / 750 target remains unmet.

## 0.0.1069 — ML layout review (2026-10-04)

- Include the latest trained checkpoint outputs: overview 285 and individual
  view 1,963 visual conflicts for the reviewed 1,244-model, 1,727-relation graph.
- Load the included snapshot for the default layout when the complete graph
  and original card dimensions match. Explicit file overrides remain available.
- Preserve independent overview and individual positions and boundary ports;
  turn off June bundles and Leaf cards to inspect the individual geometry.
- Retain model checkpoints, research records and source provenance. The active
  150 / 750 target remains unmet; this release is for reviewing current progress.

## 0.0.1068 — Preview release preparation (2026-09-16)

This entry describes the current release candidate. It does not imply that the
version has already been published to a registry.

- Static Django workspace discovery and native Python source analysis.
- Interactive ERD with model search, relationship inspection, and native layout.
- Compact Related diagram, relationship filters, leaf group exploration, and
  navigation back to the original overview.
- Direct connections for selected models and consistent dimming of large leaf
  cards. Multiple fields and grouped leaf members share physical connections.
- Apple Silicon macOS 26+ packaging with bundled native tools, source archives,
  third-party license notices, and installation/support documentation.

### Known limitations

- This VSIX targets `darwin-arm64`; other native hosts and browser-only VS Code
  are not supported by this release.
- Runtime-generated models and some custom Django patterns may not be resolved.
- Overview connection filters update the inspector but do not yet narrow the
  highlighted overview lines. Related diagram applies them to its cards.
- Changing a connection filter clears the selected peer/leaf group scope.
- Dense hubs can be difficult to trace in the overview; Related diagram provides
  a compact alternative.
- A selected peer may move off the current related-diagram page when the editor
  changes between wide and narrow layouts. Select the relationship again to
  reveal its card. This path was identified by source review.
- Model search cycles through matches with Enter rather than listing suggestions.
