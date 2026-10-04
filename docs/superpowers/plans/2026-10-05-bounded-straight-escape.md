# Bounded Straight Escape Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve fresh straight geometry with bounded stochastic exploration while returning the best complete, independently audited scene.

**Architecture:** Keep the current spatial delta scorer and greedy placement. Reserve up to 10 seconds of its shared budget for annealing, retain the best request-local coordinates, and restore them before the final full audit. Treat partial collinear overlap as invalid geometry.

**Tech Stack:** C++17, OGDF, TypeScript, Node integration tests.

**Spec:** `docs/PERFORMANCE.ko.md`, plus the active user requirement: overview <=200, individual visual crossings <500, uncached source discovery through HTML <120 seconds.

## Global Constraints

- One heavy job/thread at a time, nice +10, runtime <=128 MiB; builds alone <=512 MiB.
- No analyzer, layout, result, or ML preview cache reads in fresh analysis.
- Preserve every actual model, card dimension, relationship identity and declaration provenance.
- Every nonself relationship remains an independent two-point line. No hidden edges, new grouping, dummy product nodes, or bends.
- Preserve the current 110-second request and 90-second layout ceilings. A native position search uses at most 40 seconds and half its worker timeout.
- Keep existing untracked `run_source_face_warp_reward.py` and `source_face_warp_policy.py` untouched.

## Review Focus

- A shared deadline expiring during uphill exploration must still return the best complete scene.
- Partial line overlap must not become a lower-count accepted candidate.
- Isolated models obstructing unrelated lines must remain in observations and output.
- Empty/zero-pressure graphs must stop without sampling from invalid weights.
- A shorter worker timeout or explicit zero search budget must bound/disable exploration.

---

### Task 1: Exact geometry and bounded escape

**Files:** `native/ogdf-layout/src/straightVisualOptimization.h`, `straightVisualOptimization.cpp`, `straightVisualPlacement.cpp`, `test/integration/straight-visual-state.cpp`.

**Interfaces:** Keep `optimizeStraightVisualPlacement(...)`; add `escapeBudgetMs` (default 0) and `maxEscapeIterations` (default 1000000) to its options, and escape/uphill counters to its result. A positive escape budget is a reservation within `budgetMs`, never an extension.

- [x] Add literal partial-overlap and separated-collinear-line regressions; run the native property binary and observe the overlap regression fail.
- [x] Match the canonical audit's collinear overlap tolerance in every full/delta pair evaluation; retain independent lanes. Near-collinear input-order regression also passed after symmetric projection checks.
- [x] Add bounded exploration regressions with greedy rounds disabled: expiry/zero iteration count, complete output, no regression against a full audit, uphill moves followed by best-scene restoration, and an isolated obstacle.
- [x] Implement pressure-weighted proposals, local/neighbor points, swaps and request-local pendant group translations; temperature 8 to .08 in two cooling cycles, maximum debt 150. Never accept invalid routes or actual card overlap.
- [x] Restore the best complete nodes atomically and run the existing full audit; verify randomized delta properties still pass.

### Task 2: Shared deadline integration and fresh evidence

**Files:** `native/ogdf-layout/src/main.cpp`, `src/extension/services/layout/runOgdfLayout.ts`, `test/integration/fresh-analysis-latency.test.mjs`, release metadata and performance documentation if promoted.

**Interfaces:** `DJERD_STRAIGHT_VISUAL_POSITION_BUDGET_MS` remains the total position budget. Clamp to 40000; native reserves `min(10000, total * .25)` for escape. The extension selects `min(40000, workerTimeout * .5, configuredValue)`; zero disables it.

- [x] Add the default 40000-budget and shorter-deadline/zero-budget regressions; observe the current 30000 default fail the new behavior assertion.
- [x] Wire the shared native budget and report escape counters without changing successful-target semantics or scene coverage.
- [x] Build one job at <=512 MiB; run the focused JS/native suites at <=128 MiB. Final focused suite: 61 passed, peak 125.2 MiB.
- [x] Run fresh Captain source discovery, Rust analysis, layout and real HTML under <=128 MiB. Independently audit both actual views and canonical fidelity; accept no saved coordinates as runtime input. Final v21: 88.1385 seconds, 111.3 MiB, overview 2945 / individual 2954, all 1247 models and 1732 independent routes.
- [x] Promote only a material actual improvement within the user latency/memory contract. Verify actual desktop/tablet/mobile rendering and selection/toggle behavior, then version, package and install using existing authorization. Otherwise retain 0.0.1072 and document the measured failure. Release 0.0.1073 passed package verification and installed-binary identity checks; the project window was reloaded.

Final integration: commit and push the verified release on `multi-view` using the existing authorization. Record the resulting commit ID and push result in the execution ledger.

Execution continues natively under the user's existing continuation and resource authorizations. The full crossing goal remains active until its actual criteria pass.
