# Django ERD Maker Implementation Plan

## 1. Purpose
This document turns `spec.md` into an implementation plan that is intentionally decomposed for AI-assisted development. The goal is not only to define what to build, but also to define how to build it in a way that reduces hallucination risk, hidden assumptions, and cognitive overload.

`e2e-test.md` is the companion document for specification-derived end-to-end scenarios and must be used when authoring or updating E2E tests.

## 2. Target v1
The first usable version should let a developer:
- open a Django project in VS Code,
- run `Django ERD: Open Diagram`,
- analyze models through a Rust-based static analyzer,
- build a full graph including isolated models,
- render an interactive SVG ERD in a webview,
- identify Django `choices` fields as enum-like fields and inspect their options,
- inspect user-defined model methods from a selected table,
- view `@property`-decorated computed attributes per table,
- toggle method visibility, property visibility, and method-driven highlighting per table,
- switch between hierarchical, circular, and clustered layouts,
- move and hide tables,
- see auto-routed ERD-style relationship lines with visible crossing cues.

## 3. Planning Principles
- Build in vertical slices, but define contracts before behavior.
- Keep extracted facts separate from resolved graph facts.
- Keep structural ERD relations separate from method-inferred associations.
- Keep UI state separate from analyzer output.
- Treat disconnected models as first-class from the start.
- Keep rendering independent from layout logic.
- Keep source code files below 500 lines by splitting by responsibility.
- Allow Markdown documentation files up to 3000 lines when the extra structure improves implementation safety.

## 4. AI Implementation Guardrails

### 4.1 Work Packet Rules
Every implementation task should be a small work packet.

A work packet should:
- have one primary outcome,
- touch one layer when possible,
- touch at most 1 to 3 files unless scaffolding is the explicit goal,
- introduce at most one new data shape at a time,
- have a clear fixture or input example,
- have a direct verification path before the next packet begins.

A work packet should not:
- implement analyzer extraction, graph normalization, and UI rendering in one step,
- change domain schema and UI behavior without updating fixtures and protocol together,
- introduce multiple uncertain inference rules in the same packet,
- rely on undocumented Django behavior or guessed AST structure.

### 4.2 Definition of Ready
Do not start a work packet unless all of the following are known:
- exact input shape,
- exact output shape,
- file ownership for the change,
- verification method,
- unsupported cases or fallback behavior.

If any of these are missing, first create the missing fixture, contract, or note.

### 4.3 Definition of Done for a Packet
A packet is done only when:
- the code compiles or type-checks for the touched layer,
- the fixture or test for that packet exists,
- diagnostics exist for unsupported or partial cases where relevant,
- no unrelated refactor was mixed into the packet,
- the next packet can consume the output without guessing.

### 4.4 Evidence Rules
- New analyzer behavior must be backed by at least one positive fixture and one unsupported or partial fixture when realistic.
- New graph behavior must be backed by a stable JSON input fixture and a snapshot or assertion-based output test.
- New UI behavior must be backed by stable mock graph data before it is wired to live analyzer output.
- New interaction behavior must be driven by explicit state shape, not ad hoc component state.

### 4.5 Stop Conditions
Stop and document instead of guessing when:
- a Django construct cannot be resolved statically with confidence,
- an AST pattern is not covered by fixtures,
- the same type is being interpreted differently across Rust and TypeScript,
- structural edges and method-driven highlights start sharing the same rendering path,
- a file is trending toward becoming a monolith.

### 4.6 Anti-Hallucination Constraints
- Prefer fixture-first implementation over code-first inference.
- Prefer JSON contracts over implicit in-memory coupling.
- Prefer a missing feature with a diagnostic over a guessed feature with silent wrong output.
- Prefer adding a parser limitation note over pretending dynamic Django code is statically knowable.
- Prefer one deterministic layout or routing baseline before heuristic tuning.
- Prefer writing E2E coverage from `e2e-test.md` instead of inventing ad hoc scenarios during implementation.
- Prefer real extension-host E2E runs with Django-installed fixture environments over mock-based E2E shortcuts.

## 5. Delivery Strategy

### 5.1 Contract-First
Before implementing extraction or rendering logic, define:
- the analyzer output schema,
- the graph schema,
- the UI message protocol,
- the state shape for toggles and selection.

### 5.2 Fixture-First
Before implementing a parser rule or renderer feature, create:
- a minimal Django fixture project or JSON graph fixture,
- the expected output shape,
- at least one unsupported or edge-case example where the result is partial.

For end-to-end coverage:
- derive E2E cases from `e2e-test.md`,
- update `e2e-test.md` when `spec.md` changes in a user-visible way,
- keep fixture workspaces aligned with the scenarios defined there,
- run E2E through the real extension command and webview flow in a Django-installed environment,
- do not treat mock analyzer payload tests as E2E coverage.

### 5.3 Layer-by-Layer
Use this order whenever possible:
1. shared contract,
2. fixture,
3. narrow implementation,
4. verification,
5. integration.

### 5.4 No Multi-Unknown Slices
Avoid work packets that combine two or more unknowns such as:
- new AST rule plus new graph rule,
- new graph rule plus new UI state rule,
- new UI state rule plus new rendering mode.

## 6. High-Level Architecture

### 6.1 Layers
1. VS Code extension host
2. Analysis orchestration
3. Rust analyzer
4. Shared domain and graph model
5. Layout and routing
6. Webview state and rendering

### 6.2 Responsibilities
- Extension host: commands, config, workspace access, webview lifecycle.
- Analysis orchestration: analyzer execution, progress, caching, diagnostics mapping.
- Rust analyzer: Django model discovery and metadata extraction.
- Shared domain: model IDs, graph types, relation semantics, diagnostics schema.
- Layout and routing: node placement, edge paths, crossing metadata.
- Webview: SVG rendering, interaction, local view state.

### 6.3 Runtime Flow
1. User opens the ERD command.
2. Extension discovers the target workspace and candidate Django files.
3. Extension invokes the Rust analyzer.
4. Analyzer returns model metadata and diagnostics.
5. Graph builder resolves relations and creates the full project graph.
6. Layout engine positions nodes.
7. Routing engine computes edge paths.
8. Webview renders SVG and applies interaction state.

## 7. Proposed Repository Structure
```text
/
  plan.md
  spec.md
  spec-kor.md
  e2e-test.md
  package.json
  tsconfig.json
  src/
    extension/
      activation/
      commands/
      config/
      panels/
      services/
    shared/
      domain/
      graph/
      protocol/
      diagnostics/
    webview/
      app/
      components/
      render/
      layout/
      state/
      interaction/
      styles/
  analyzer/
    Cargo.toml
    src/
      cli/
      discovery/
      parser/
      extract/
      resolve/
      diagnostics/
  test/
    fixtures/
    integration/
    unit/
```

## 8. Phase Plan

### Phase 0. Foundation and Tooling
Goal: create a buildable extension and analyzer skeleton.

Packets:
0.1 create VS Code extension package, TypeScript config, and activation entrypoint.
0.2 create Rust analyzer crate with CLI entrypoint and JSON hello-world output.
0.3 add formatter, linter, and test script scaffolding for both languages.
0.4 add placeholder `Open Diagram` command that opens a simple webview.
0.5 create fixture directories for Django projects, analyzer JSON, and UI graph mocks.
0.6 provision the dedicated E2E Python environment strategy with Django installed.
0.7 add the initial E2E scenario document derived from `spec.md`.

Exit gate:
- extension builds,
- analyzer builds,
- placeholder webview opens,
- test and fixture directories exist,
- the E2E environment plan for a Django-installed interpreter exists,
- `e2e-test.md` exists as the source of truth for scenario-driven E2E coverage.

### Phase 1. Shared Domain Contracts
Goal: define stable cross-layer types before implementing logic.

Packets:
1.1 define canonical model ID format and shared naming rules.
1.2 define analyzer output types for models, fields, relations, and diagnostics.
1.3 define types for `choices` metadata and statically inferable option lists.
1.4 define types for `@property` attributes, user-defined methods, and method-related model references.
1.5 define graph node, graph edge, and derived reverse relation shapes.
1.6 define separate auxiliary metadata shape for method-driven associations.
1.7 define layout result and routed edge path types.
1.8 define extension-to-webview message protocol.
1.9 add a minimal sample JSON payload and load it in TypeScript.

Exit gate:
- Rust and TypeScript can agree on serialized payload shape,
- the sample JSON can be loaded without analyzer logic,
- structural relations and method-driven associations are clearly separate in the schema.

### Phase 2. Fixture Projects and Discovery
Goal: make discovery work against controlled examples before real projects.

Packets:
2.1 create a minimal single-app Django fixture project.
2.2 create a multi-app fixture project with cross-app relations.
2.3 create a fixture with isolated models and disconnected subgraphs.
2.4 create a fixture with `choices`, `@property`, and user-defined model methods.
2.5 create a fixture that yields partial analysis or unresolved references.
2.6 create a fixture that can produce at least one edge crossing in a known layout.
2.7 ensure fixture workspaces are executable in the E2E environment with Django installed.
2.8 implement workspace root selection logic.
2.9 implement app discovery heuristics.
2.10 implement candidate file collection for model modules.
2.11 emit discovery diagnostics for ambiguous or partial workspace structure.

Exit gate:
- each fixture workspace produces stable app identity and candidate file lists,
- unsupported discovery cases degrade to diagnostics,
- repository fixtures cover the scenarios defined in `e2e-test.md`,
- fixture workspaces are usable in the real Django-installed E2E environment.

### Phase 3. Rust Parser Boundary
Goal: create a safe parser adapter layer before feature extraction starts.

Packets:
3.1 choose and wire the Python parser library in Rust.
3.2 create internal AST adapter utilities so extraction code does not depend on raw parser details everywhere.
3.3 add parser smoke tests against simple Python model files.
3.4 add parser diagnostics mapping for syntax errors and unsupported file states.

Exit gate:
- the analyzer can parse fixture files and surface stable syntax diagnostics.

### Phase 4. Rust Analyzer Core Extraction
Goal: extract model facts from common Django patterns.

Packets:
4.1 detect `models.Model` subclasses.
4.2 extract basic model identity and app-local metadata.
4.3 extract scalar fields and field names.
4.4 extract relation fields for `ForeignKey`, `OneToOneField`, and `ManyToManyField`.
4.5 extract statically inferable `choices` definitions and option labels or values.
4.6 extract `@property`-decorated attributes.
4.7 extract user-defined methods declared on the model.
4.8 extract direct target model references from relation fields.
4.9 infer method-level related model references when statically visible.
4.10 emit structured diagnostics for unsupported or ambiguous constructs.

Rules:
- do not combine 4.4, 4.5, 4.6, and 4.7 in one packet,
- each extraction rule needs a matching fixture,
- unsupported dynamic code must produce diagnostics instead of guessed output.

Exit gate:
- representative fixtures produce stable analyzer JSON,
- `choices`, `@property`, methods, and relation fields appear in separate metadata sections,
- unsupported patterns do not crash the analyzer.

### Phase 5. Graph Normalization and Resolution
Goal: convert extracted facts into a stable project graph.

Packets:
5.1 create a model registry keyed by canonical model ID.
5.2 resolve direct relation targets to canonical IDs.
5.3 derive reverse relations with provenance metadata.
5.4 preserve disconnected models in the final graph.
5.5 keep method-driven model associations as auxiliary metadata, not structural edges.
5.6 normalize graph nodes and structural edges into one renderable graph payload.
5.7 add JSON-based graph snapshot tests.

Exit gate:
- the graph includes connected and isolated nodes,
- reverse relations are derived where possible,
- method-driven associations remain separate from structural ERD edges.

### Phase 6. Layout Foundation
Goal: create a reusable layout layer before building rich rendering.

Packets:
6.1 define the layout strategy interface.
6.2 define node box measurement inputs based on fields, `choices`, properties, and methods.
6.3 implement hierarchical layout.
6.4 implement circular layout.
6.5 implement clustered layout.
6.6 add layout selection registry.
6.7 add fixture-based layout output assertions.

Rules:
- layout code may read graph data but must not perform rendering,
- layout output must stay deterministic for the same input unless randomness is explicitly introduced and seeded.

Exit gate:
- the same graph can be positioned through all three layouts,
- layout output can be recomputed after hide or move operations.

### Phase 7. Routing Foundation
Goal: compute readable structural relationship paths.

Packets:
7.1 define edge anchor or port selection rules for table sides.
7.2 define routed path data shape independent from SVG commands.
7.3 implement a baseline deterministic polyline or orthogonal router.
7.4 detect crossings between routed structural edges.
7.5 add crossing metadata for visual markers.
7.6 add routing tests using fixed node positions.

Rules:
- routing must operate only on positioned nodes and structural edges,
- method-driven highlight overlays must not share the same routing semantics.

Exit gate:
- structural edges connect the correct tables,
- crossings are detectable and markable,
- routing is usable for medium-sized graphs.

### Phase 8. Webview Rendering Shell
Goal: render stable mock data before live integration.

Packets:
8.1 create webview application shell and root store wiring.
8.2 render the SVG scene root with pan and zoom support.
8.3 render table frame, title, and core field rows.
8.4 render enum-like `choices` fields and their option lists.
8.5 render `@property` attribute rows.
8.6 render user-defined method rows.
8.7 render structural ERD edges from routed path data.
8.8 render method-driven table highlight overlays as a distinct visual layer.
8.9 add rendering tests using mock graph payloads.

Rules:
- structural edges and method-driven highlights must look different,
- table content sections must be independently toggleable by state, not by conditional ad hoc rendering,
- rendering should consume stable mock payloads before live analyzer wiring,
- mock payload rendering tests do not count as E2E coverage.

Exit gate:
- mock graph data renders a readable ERD in the webview,
- `choices`, properties, and methods are visually distinct,
- structural edges and method-driven highlights are not confused.

### Phase 9. Interaction State and Controls
Goal: add interaction through explicit state rather than component-local behavior.

Packets:
9.1 define selection state for the active table and active method context.
9.2 implement node drag and manual position overrides.
9.3 implement hide and show state for tables.
9.4 implement per-table method visibility toggle.
9.5 implement per-table property visibility toggle.
9.6 implement per-table method-highlight toggle.
9.7 implement layout mode selection and reset action.
9.8 add reducer or store tests for all interaction state transitions.

Rules:
- source graph state stays immutable for a given analysis result,
- manual position overrides must not mutate layout definitions directly,
- each toggle must map to one explicit state field.

Exit gate:
- users can select tables, move them, hide them, switch layouts, and control the per-table toggles,
- state transitions are testable without the renderer.

### Phase 10. Extension Integration
Goal: connect the extension host, analyzer, graph pipeline, and webview.

Packets:
10.1 implement analyzer process service and JSON decoding.
10.2 wire discovery, analyzer execution, and graph normalization into one command flow.
10.3 connect the webview to real graph payloads.
10.4 add refresh command and panel lifecycle handling.
10.5 add progress, error, and diagnostic surfacing.
10.6 run the extension inside the real VS Code extension test host.
10.7 configure E2E fixture workspaces to use Django-installed interpreters.
10.8 implement E2E automation from `e2e-test.md` against real fixture workspaces.
10.9 map automated E2E cases back to scenario IDs.

Rules:
- do not bypass shared contracts during integration,
- do not let the webview parse raw analyzer AST-oriented output directly,
- keep refresh and initial load on the same data path where possible,
- do not add E2E tests without linking them to scenarios in `e2e-test.md`,
- do not classify mock analyzer or mock graph tests as E2E.

Exit gate:
- the extension works end-to-end in VS Code against fixture projects,
- diagnostics surface without breaking the ERD view,
- E2E automation covers the implemented scenarios from `e2e-test.md`,
- E2E runs use the real extension host and Django-installed fixture environments.

### Phase 11. Quality Hardening
Goal: make the first release stable and maintainable.

Packets:
11.1 add timing instrumentation for discovery, parsing, graph building, layout, and rendering.
11.2 review files approaching the size limit and split by responsibility.
11.3 review unsupported Django patterns and ensure diagnostics are explicit.
11.4 add packaging and build verification for extension plus analyzer.
11.5 run a boundary audit to ensure layers are still decoupled.

Exit gate:
- core flows are test-covered,
- performance is measurable and acceptable,
- no major layer violation or oversized file remains.

## 9. Cross-Cutting Technical Decisions
- Use a versioned JSON contract between extension and analyzer.
- Define one canonical model ID format early, such as `app_label.ModelName`.
- Store reverse relations as derived structural edges with provenance.
- Store method-inferred related models as auxiliary metadata, not structural edges.
- Design view state for persistence early, but keep v1 persistence optional.
- Start with full-project analysis and add incrementality only after the baseline is stable.

## 10. Verification Matrix

### 10.1 Contract Verification
- Rust serializer output matches TypeScript decoder expectations.
- Sample analyzer JSON is valid before live analyzer integration.
- Structural edge payloads and method-association payloads remain separate.

### 10.2 Analyzer Verification
- Each extraction rule has a positive fixture.
- `choices`, `@property`, and method extraction each have their own fixture.
- At least one unsupported or partial case exists for complex extraction rules.

### 10.3 Graph Verification
- Connected graphs stay connected.
- Disconnected models are not dropped.
- Reverse relations appear only as derived edges.
- Method-level associations do not become ERD structural edges by accident.

### 10.4 UI Verification
- Fields, choice fields, properties, and methods are visually distinguishable.
- Per-table toggles affect only the intended table.
- Method-driven highlights do not visually impersonate structural edges.

### 10.5 Integration Verification
- `Open Diagram` works on fixture workspaces.
- `Refresh Diagram` reuses the same data path.
- Partial diagnostics do not block usable rendering.
- Implemented E2E tests map to scenario IDs from `e2e-test.md`.
- E2E runs execute against real Django-installed environments rather than mock payload input.

## 11. Recommended Fixture Set
- single app with scalar fields,
- multi-app project with cross-app foreign keys,
- isolated models with no relations,
- many-to-many and reverse relation cases,
- model with Django `choices`,
- model with `@property` attributes,
- model with user-defined methods referencing related models,
- unresolved target reference,
- unsupported dynamic Django construct,
- syntax-error fixture for analyzer diagnostics,
- known-crossing layout fixture for routing and crossing assertions.
- interpreter-configured fixture environments with Django installed.

## 12. Risks and Mitigations
- Python static analysis complexity: support common Django patterns first and emit diagnostics for dynamic cases.
- UI and analyzer coupling: normalize analyzer output into shared graph/domain contracts before rendering.
- Edge routing complexity: ship a deterministic router first and improve heuristics later.
- Layout rework risk: enforce one layout strategy interface from the start.
- Large file growth: split modules early instead of cleaning up after a monolith forms.
- AI overreach risk: enforce packet size, fixture-first work, and stop conditions instead of guessing.
- E2E drift risk: treat `e2e-test.md` as the scenario contract and update it whenever the spec changes.
- fake-confidence E2E risk: treat mock-based renderer checks as lower-level tests, not end-to-end validation.

## 13. Milestones
- Milestone A, Skeleton: finish Phase 0 and Phase 1, and open a placeholder webview from the command.
- Milestone B, Discovery and Analyzer: finish Phase 2 to Phase 4 and produce stable analyzer JSON from fixtures.
- Milestone C, Graph and Layout: finish Phase 5 to Phase 7 and produce routed graph output from fixture data.
- Milestone D, First Visual ERD: finish Phase 8 and render fixture graphs clearly in the webview.
- Milestone E, Usable Extension: finish Phase 9 and Phase 10 for end-to-end analysis and interaction.
- Milestone F, Release Readiness: finish Phase 11, package validation, and boundary review.

## 14. Definition of Done for v1
Version 1 is done when:
- the extension runs end-to-end in VS Code,
- Django models are discovered through static analysis,
- the graph includes disconnected models,
- SVG ERD rendering is interactive,
- choice fields and their options render correctly when statically inferable,
- layout switching works across hierarchical, circular, and clustered modes,
- node move and hide operations work,
- method inspection and per-table `@property` display work,
- per-table method/property/highlight toggles work,
- relation lines are auto-routed with visible crossing cues,
- method-driven related-table highlights work without being confused with structural ERD edges,
- diagnostics are surfaced for unsupported patterns,
- the codebase remains layered, modular, and within file size constraints.

## 15. Immediate Next Step
The recommended first packet is `0.1 create VS Code extension package, TypeScript config, and activation entrypoint`, followed immediately by `0.2 create Rust analyzer crate with CLI entrypoint and JSON hello-world output`. These two packets establish the toolchain without forcing any early domain assumptions.
