# Changelog

## 0.0.1077 — Circular relationship view (2026-10-07)

- Add Circular view for the entire model catalog, ordered by app, with one mark
  per model and separate curves for parallel, reverse and self relationships.
- Search or select a model to highlight its connections; select a curve or field
  to inspect the declared relationship in the existing model panel.
- Add independent zoom, drag, Fit and keyboard navigation. Returning to Full
  diagram preserves the ERD viewport and model positions.
- Build the view from the current analysis without another layout computation.
  Native model weights and straight-route crossing metrics are unchanged.

## 0.0.1076 — Fresh source model and card clearance (2026-10-06)

- Predict new positions from current source names, actual card dimensions and
  original relations using a shared portable native model. Each analysis builds
  its own inputs; saved layout coordinates and preview results are not read.
- Preserve horizontal 56px / vertical 42px card clearance during both position
  search phases, retaining all original cards and independent straight routes.
- On the same 1,247-model / 1,732-route source project, analysis through HTML
  took 48.86 seconds with a combined host/worker peak of 119.0 MiB. Both rendered
  views scored 2,049 visualCross, with zero card overlaps or spacing violations
  and a BBOX of 1.448B. The complete native position phase took 33.66 seconds.
- Reserve 20% of the original position allowance for refiner cleanup, independent
  audits and serialization. A prior installation trial exceeded the 40-second
  closeout deadline and fell back; its result is excluded from quality claims.
- Keep the result `quality-degraded`: overview ≤200 and individual <500 remain
  unmet. Prior held-out testing showed weak generalization; the final refit loss
  is training loss. See [performance notes](docs/PERFORMANCE.ko.md).
- Keep connection filter counts on one line at narrow inspector widths.

## 0.0.1075 — Local integration candidate (2026-10-06)

- Initial local source-model candidate; a subsequent installed analysis exceeded
  the complete position deadline. Its timed-out fallback is excluded from quality
  claims. The corrected deliverable is 0.0.1076.

## 0.0.1074 — Bounded knot search work (2026-10-05)

- Stop cluster knot swaps after 45,000 candidate evaluations, while retaining
  the existing 5-second wall-clock safety limit.
- On the same 1,247-model / 1,732-route source project, uncached optimized
  analysis through HTML took 83.65 seconds and peaked at 123.9 MiB. Actual
  rendered overview visualCross was 2,912 and individual visualCross was 2,922,
  compared with 2,945 / 2,954 for 0.0.1073. The BBOX decreased from 7.669B to
  6.714B; rendered node overlaps remained zero.
- The result remains `quality-degraded`; overview ≤200 and individual <500
  remain unmet. This is one default-budget source run; see measurement limits
  in [performance notes](docs/PERFORMANCE.ko.md).

## 0.0.1073 — Bounded straight-line escape (2026-10-05)

- Reserve up to 10 seconds within the shared 40-second position budget for
  stochastic moves and swaps, then restore the best complete request-local
  scene before the independent audit. Shorter deadlines and zero budgets apply
  to both phases; no result coordinates are reused between analyses.
- Reject partially overlapping collinear lines with symmetric full/delta
  scoring, including near-collinear tolerance cases.
- On the same 1,247-model / 1,732-route project, fresh overview visualCross
  decreased from 3,274 to 2,945 and individual visualCross from 3,271 to 2,954.
  Source discovery through HTML generation took 88.1 seconds with a combined
  host/worker peak of 111.3 MiB and no model or layout result cache reads.
- All models and individual straight routes remain present, with no card
  overlaps. Analysis is slower than 0.0.1072 and the bounding area increased
  from 6.142B to 7.669B. Overview ≤200 and individual <500 remain unmet;
  the result retains its `quality-degraded` status. See
  [measurement conditions](docs/PERFORMANCE.ko.md).

## 0.0.1072 — Fresh straight-line crossing reduction (2026-10-05)

- Score card moves and swaps against every independent straight relationship
  and every actual rendered card, using transient spatial indexes.
- Run a bounded position search in the initial fresh worker, using canvas
  dimensions from the start, and avoid repeating legacy relocation afterward.
- Keep parallel relationships on distinct boundary slots and reject collapsed
  paths or incomplete initial results, including missing isolated models.
- On the same 1,247-model / 1,732-route project, fresh overview visualCross
  decreased from 7,993 to 3,274 and individual visualCross from 8,309 to 3,271.
  Source discovery through HTML generation took 77.3 seconds, with a combined
  host/worker peak of 110.9 MiB. No model or layout result cache was read.
- This is an interim improvement. Overview ≤200 and individual <500 remain
  unmet; the result retains its `quality-degraded` status. See
  [measurement conditions](docs/PERFORMANCE.ko.md).

## 0.0.1071 — Bounded fresh analysis (2026-10-05)

- Reanalyse source and compute a new layout for each foreground request,
  bypassing bundled previews and model, baseline and layout result caches.
- Bound foreground work with shared deadlines and a single native worker;
  reduce analyzer AST retention, collision search and route-candidate storage.
- Share repeated scene edges and crossing IDs during transport, and serialize
  large scenes in small ASCII chunks while restoring exact browser data.
- Reject reroute candidates that omit or duplicate requested nodes/relations,
  truncate paths, add bends, or regress the crossing acceptance metrics.
- On the current 1,247-model / 1,732-route project, fresh document preparation
  took 25.1 seconds in ordinary mode and 77.2 seconds in optimized mode, with
  measured combined host/worker peaks of 113.9 / 118.4 MiB. Compiler stages
  retain a separate 512 MiB cap. See [measurement conditions](docs/PERFORMANCE.ko.md).
- Optimized visualCross is 7,993; the 500 target and table-clearance targets
  remain unmet. The cached 285 / 1,963 preview is not a fresh-analysis result.

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
