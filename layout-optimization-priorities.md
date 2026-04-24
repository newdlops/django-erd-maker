# Layout Optimization Priorities

## 1. Current Evidence

Latest `log.txt` run:

- Analyzer input size: `models=1237`, `nodes=1233`, `structuralEdges=3068`, `routedEdges=3046`.
- Timeout budget: `60000ms`.
- Native input preservation is working. Failed native runs now keep `nodes.tsv`, `edges.tsv`, `failure.json`, `stdout.txt`, and `stderr.txt`.

Preserved SIGSEGV fixtures:

| Mode | Failure | Preserved Directory |
| --- | --- | --- |
| `ortho` | `SIGSEGV` | `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-ortho-HqagsL` |
| `planarization_grid` | `SIGSEGV` | `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-planarization_grid-3tPajz` |
| `planarization` | `SIGSEGV` | `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-planarization-PHsW9x` |

Preserved timeout fixtures:

| Mode | Failure | Preserved Directory |
| --- | --- | --- |
| `hierarchical_greedy_switch` | `60000ms` timeout | `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-hierarchical_greedy_switch-jWQJ0E` |
| `hierarchical_sifting` | `60000ms` timeout | `/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-hierarchical_sifting-D8gIUQ` |

Important concurrency signal:

- `hierarchical_greedy_switch` completed once in `7032ms`, then an older in-flight run for the same mode timed out and emitted a fallback payload.
- This means stale layout results can overwrite newer successful results.

## 2. Priority Order

### P0. Stop Stale Layout Results

Problem:

- A slow older request can finish after a newer request and still update the diagram.
- This creates false fallback states and can disable layouts even when a later request succeeded.

Goal:

- Each layout request gets a monotonically increasing request id.
- The extension/webview only applies a result when it matches the latest request id for that panel.
- Old failures should be logged but should not update the visible diagram or disabled layout list.

Why first:

- Without this, log interpretation is unreliable.
- It also prevents user-visible regression when expensive layouts are clicked in quick succession.

Expected implementation:

- Add a layout request token in the extension-side refresh flow.
- Include the token in layout start/result logging.
- Drop stale results before `Diagram payload ready` is sent to the webview.

Verification:

- Trigger two layout clicks quickly: slow mode, then fast mode.
- Confirm the slow mode timeout is logged as stale and does not replace the fast mode.

### P1. Reproduce and Fix SIGSEGV in Planarization Family

Problem:

- `planarization`, `planarization_grid`, and `ortho` crash with `SIGSEGV`.
- `stdout.txt` and `stderr.txt` are empty, so the crash is inside native OGDF/wrapper execution.

Goal:

- Reproduce each preserved fixture from the command logged in `log.txt`.
- Capture a backtrace.
- Identify whether the crash is caused by:
  - invalid graph shape after TSV import,
  - `PlanarizationLayout` with custom bounded crossing minimizer,
  - our post-layout geometry sanitization/packing/routing,
  - OGDF behavior on this graph.

Working commands:

```sh
'/Users/lky/project/django-erd-maker/bin/ogdf/darwin-arm64/django-erd-ogdf-layout' layout --mode 'planarization' --nodes-file '/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-planarization-PHsW9x/nodes.tsv' --edges-file '/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-planarization-PHsW9x/edges.tsv'
'/Users/lky/project/django-erd-maker/bin/ogdf/darwin-arm64/django-erd-ogdf-layout' layout --mode 'planarization_grid' --nodes-file '/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-planarization_grid-3tPajz/nodes.tsv' --edges-file '/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-planarization_grid-3tPajz/edges.tsv'
'/Users/lky/project/django-erd-maker/bin/ogdf/darwin-arm64/django-erd-ogdf-layout' layout --mode 'ortho' --nodes-file '/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-ortho-HqagsL/nodes.tsv' --edges-file '/var/folders/pc/jdz8pf2x2hl_wf6wpxl1zjzm0000gn/T/django-erd-ogdf-ortho-HqagsL/edges.tsv'
```

Backtrace approach:

- Rebuild native wrapper with debug symbols.
- Run the preserved command under `lldb`.
- If it crashes before output JSON, inspect the top OGDF/wrapper frames.

Likely fixes:

- If `createBoundedSubgraphPlanarizer()` is involved, try `VariableEmbeddingInserter` with reduced postprocessing instead of `FixedEmbeddingInserter`.
- If connected component packing or edge routing is involved, temporarily disable post-processing for the crash fixture and re-enable step by step.
- If OGDF planarization cannot safely handle this graph, make the mode use a non-crashing bounded projection for ERD-scale graphs.

### P2. Cap or Replace Expensive Layered Cross-Min Modes

Problem:

- `hierarchical_sifting` timed out after `60000ms`.
- `hierarchical_greedy_switch` can complete in `7032ms`, but another in-flight run timed out after `60000ms`.
- These modes produce very large bounding boxes, often above `190k x 125k`.

Goal:

- Avoid exact expensive cross-minimizers on 1k+ ERD graphs unless they have proven bounded behavior.
- Prefer deterministic bounded substitutes that preserve layered structure.

Recommended policy:

| Mode | Current Risk | Proposed Strategy |
| --- | --- | --- |
| `hierarchical_sifting` | timeout | use bounded barycenter/median base plus sifting-style geometry at `nodes>=1000` |
| `hierarchical_greedy_switch` | inconsistent; timeout possible | use bounded median base plus switch-style geometry at `nodes>=1000`, or cap with request cancellation |
| `hierarchical_split` | completes but huge bbox | keep available, but mark as exploratory/large-canvas layout |
| `hierarchical_grid_sifting` | surrogate, huge bbox | revise geometry to reduce bbox before considering it useful |

Verification:

- Preserve exact mode behavior for small fixtures.
- On the 1233-node fixture, all layered modes should either finish under 10s or intentionally select a bounded surrogate.

### P3. Choose Better Default and Recommended Layouts

Problem:

- Several modes run successfully but produce extremely large canvases, which hurts navigation and rendering.
- `hierarchical` and many hierarchical variants are not good defaults for this graph size.

Best current candidates from `log.txt`:

| Mode | Time | Strategy | Bounding Box | Assessment |
| --- | ---: | --- | ---: | --- |
| `fmmm` | `191ms` | exact | `11724 x 9035` | best compact exact overview |
| `linear` | `120ms` | exact | `8132 x 10248` | compact, but less structural |
| `pivot_mds` | `130ms` | bounded | `15696.7 x 15403.9` | good graph overview |
| `stress_minimization` | `450ms` | bounded | `15788 x 14936` | good graph overview |
| `davidson_harel` | `620ms` | bounded | `16904 x 17450.2` | usable |
| `fast_multipole_multilevel` | `267-273ms` | bounded | about `25k x 18k` | acceptable |
| `cluster_ortho` | `107ms` | cluster surrogate | `21200 x 18512` | useful for app-level grouping |

Poor current candidates:

| Mode | Reason |
| --- | --- |
| `hierarchical`, `hierarchical_barycenter` | huge canvas around `195k-202k` wide |
| `hierarchical_split`, `hierarchical_greedy_switch` | huge canvas, can exceed `229k-266k` wide |
| `visibility`, `upward_*`, `planar_draw` | surrogate plus huge canvas |
| `circular` | huge canvas |
| `fast_multipole` | much larger than `fast_multipole_multilevel` |

Recommendation:

- Default for large graphs should be `fmmm` or `stress_minimization`, not `hierarchical`.
- Keep `hierarchical` as an explicit layered view.
- Surface large-canvas warning metadata for layouts whose bbox exceeds a threshold, for example `50000` in either dimension.

### P4. Reduce Bounding Box and Rendering Load

Problem:

- Some layouts produce a small number of visible labels/tables on first frame because the camera is centered on a huge canvas.
- Renderer builds around `15k` edge segments for 1233 nodes and 3046 routed edges.

Goals:

- Fit the graph to a useful initial viewport.
- Avoid huge empty canvas from isolated components or long layered lanes.
- Reduce edge segment count where possible.

Optimization ideas:

- Component packing:
  - Pack disconnected components by area using a tighter target aspect ratio.
  - Treat isolated nodes as a compact grid beside major components instead of preserving algorithm coordinates.
- Layer compression:
  - For layered layouts, compress unused lane gaps after OGDF output.
  - Cap per-layer vertical spacing based on node height distribution.
- Edge routing:
  - Use obstacle-aware routes only for visible or near-visible edges.
  - Store native/static routes but simplify collinear and near-collinear points aggressively.
  - Consider route-level LOD: straight/2-segment path at low zoom, full route at high zoom.
- Camera:
  - Initial fit should prefer occupied component bounds rather than routed edge outliers.
  - Ignore route lanes outside node bounds for initial zoom, while keeping them for panning bounds.

### P5. Improve Layout Evaluation Metrics

Current logs are useful but not enough to rank layout quality.

Add metrics:

- node bbox width/height separate from routed-edge bbox width/height,
- connected component count and largest component size,
- isolated node count,
- edge route point count total and average,
- route segments crossing node boxes,
- stale result flag,
- preserved fixture directory on failures,
- strategy class: `exact`, `bounded`, `surrogate`, `bounded_projection`, `fallback`.

Use these metrics to classify each layout:

- `recommended`: compact, stable, under 5s, no fallback.
- `usable`: stable, under 60s, acceptable bbox.
- `exploratory`: correct but large or visually noisy.
- `disabled`: crash, timeout, or stale result only.

## 3. Proposed Work Packets

### Packet A. Stale Result Guard

Files:

- `src/extension/services/diagram/loadLiveDiagram.ts`
- `src/extension/services/layout/runOgdfLayout.ts`
- shared protocol files only if a request id must be sent to the webview.

Done when:

- A timed-out older layout cannot overwrite a newer successful layout.
- Logs identify stale results.

### Packet B. SIGSEGV Reproduction Harness

Files:

- `scripts/` helper script or documented command file.
- Native wrapper only if adding debug flags or crash checkpoint logging.

Done when:

- Preserved TSV fixtures can be replayed with one command.
- Crash mode, signal, and stage are identified.

### Packet C. Planarization Crash Fix

Files:

- `native/ogdf-layout/src/main.cpp`.

Done when:

- `planarization`, `planarization_grid`, and `ortho` no longer SIGSEGV on preserved fixtures.
- If exact OGDF path is unsafe, the mode explicitly reports `bounded_projection` instead of crashing.

### Packet D. Layered Timeout Policy

Files:

- `native/ogdf-layout/src/main.cpp`.
- Possibly layout metadata contract if new strategy names are added.

Done when:

- `hierarchical_sifting` and `hierarchical_greedy_switch` no longer timeout on the 1233-node fixture.
- Exact behavior is retained for small graphs.

### Packet E. Layout Ranking and Default Policy

Files:

- layout registry/contract files.
- UI layout selector metadata if warnings are exposed.

Done when:

- Large graph default points to a compact stable mode.
- Large-canvas modes remain selectable but are clearly non-default.

## 4. Immediate Next Step

Start with Packet A before more algorithm tuning.

Reason:

- Current logs show a stale timeout after a successful `hierarchical_greedy_switch` run.
- Without stale result protection, optimization results can be misread and users can see fallback even after success.

Then run Packet B on the three preserved SIGSEGV fixtures.

After crash stage is known, decide whether to fix the exact planarization path or intentionally route large ERD graphs through a bounded projection.

## 5. Progress Notes

Implemented:

- Stale refresh results are guarded in the panel and cache update path.
- Layout logs now include request ids for refresh/cache and OGDF start/completion/failure events.
- Native OGDF input is simplified to one topology edge per undirected model pair while preserving every original routed edge id in output.
- Planarization crossing minimization now uses `VariableEmbeddingInserter(removeReinsert=None)` instead of `FixedEmbeddingInserter`.
- Layered layout spacing was reduced from `layerDistance=120,nodeDistance=76` to `layerDistance=96,nodeDistance=44`.
- OGDF completion logs now split `nodeBBox` from `routeBBox` and include route point count.

Verified on preserved fixtures:

| Mode | Previous Result | Current Result | Current BBox |
| --- | --- | --- | --- |
| `planarization` | `SIGSEGV` | success in about `2.9s` | `66037 x 19543` |
| `planarization_grid` | `SIGSEGV` | success in about `2.8s` | `65880 x 19696` |
| `ortho` | `SIGSEGV` | success in about `1.7s` | `73438 x 19344` |
| `hierarchical_sifting` | `60000ms` timeout | success in about `0.6s` | `64984 x 13529` |
| `hierarchical_greedy_switch` | stale timeout after success | success in about `0.7s`; stale overwrite guarded | `85540 x 13866` |

Interpretation:

- The main failure cause was not that `1233` nodes are too many.
- The crash/timeout came from feeding dense parallel and reciprocal ERD edges directly into OGDF topology algorithms.
- Keeping a simplified topology graph for layout while preserving all original edge ids for rendering gives exact OGDF algorithms a tractable graph without losing relationship output.
