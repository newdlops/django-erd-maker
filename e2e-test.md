# Django ERD Maker E2E Test Scenarios

## 1. Purpose
This document defines end-to-end test scenarios for the Django ERD Maker VS Code extension. It is derived from `spec.md` and should be treated as the scenario source of truth when implementing E2E tests.

The goal is to ensure that E2E tests:
- validate user-visible behavior from the specification,
- cover both happy paths and partial-analysis behavior,
- run the real extension in an execution environment that is as close as practical to actual user usage,
- use stable fixture workspaces instead of ad hoc project samples,
- avoid inventing behaviors that are not defined in `spec.md`.

## 2. Relationship to `spec.md`
- `spec.md` defines product requirements and acceptance criteria.
- `e2e-test.md` converts those requirements into executable user journeys.
- If `spec.md` changes in a way that affects user-visible behavior, `e2e-test.md` must be updated before or alongside the E2E test code.

## 3. E2E Testing Principles
- Every E2E test must map to one or more explicit items in `spec.md`.
- E2E tests must run the real extension inside the VS Code extension test host or an equivalent real extension execution environment.
- E2E tests must use repository fixtures, not arbitrary local Django projects.
- E2E tests must run against a Python environment where Django is actually installed for the target fixture workspace.
- E2E tests must execute the real analyzer process and the real webview render path.
- E2E tests must validate behavior through VS Code commands, webview state, and rendered output rather than internal implementation details.
- Visual assertions should prefer stable DOM or SVG markers over pixel-perfect screenshots when possible.
- When a behavior is partially inferable by static analysis, tests should check both the positive case and the diagnostic or degraded case.
- Mock analyzer JSON, mock graph payloads, and renderer-only harnesses are not acceptable as the primary input path for E2E tests.

## 4. Real Execution Environment Requirements
The E2E suite should run in an environment that reflects actual extension usage as closely as practical.

Required characteristics:
- the VS Code extension is launched in the extension test host, not simulated by direct function calls,
- the built or test-built Rust analyzer binary is executed as part of the flow,
- the workspace under test is a real Django fixture project,
- the selected Python interpreter for the workspace points to an environment where Django is installed,
- the ERD is produced through the same command, discovery, analysis, graph, layout, and webview pipeline that a user triggers,
- the tests observe real webview output and interaction state.

Recommended environment structure:
- one or more dedicated E2E Python virtual environments,
- Django installed into those E2E environments,
- fixture workspaces stored in the repository,
- deterministic setup for selecting the interpreter used by the fixture workspace.

Clarification:
- mock payloads are still acceptable for lower-level renderer or component tests,
- but those tests are not E2E and must not replace the scenarios defined in this document.

## 5. Fixture Requirements
The E2E suite should use stable fixture workspaces that collectively cover:
- a single-app Django project,
- a multi-app Django project with cross-app relations,
- a project containing disconnected models,
- a project with `choices` fields,
- a project covering more than one common `choices` declaration style when practical,
- a project with `@property` attributes,
- a project with user-defined model methods that reference related models,
- a project with reverse relations that are inferable from declared relations,
- a project with string-based model references across apps when practical,
- a project with unresolved or partially inferable targets,
- a project that produces graph crossings in at least one layout,
- a project with enough nodes to exercise layout switching and table movement.

The fixture set should favor diversity of realistic model situations over a single oversized demo project. Small focused projects are preferred if they keep test intent clearer.

## 6. Scenario Format
Each E2E test case should capture:
- scenario ID,
- source requirement references from `spec.md`,
- fixture workspace,
- interpreter or environment expectation when relevant,
- setup steps,
- user actions,
- expected visible results,
- notes on automation strategy where visual behavior may need stable markers.

## 7. Core Scenarios

### E2E-01 Open ERD From Command
Spec reference:
- `spec.md` 4.1, 4.4, 9

Fixture:
- minimal single-app Django project

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace in VS Code.
2. Run `Django ERD: Open Diagram`.

Expected results:
- A webview panel opens successfully.
- The ERD surface is rendered in the webview.
- At least one model table is visible.
- The extension does not require running the Django project.

Automation notes:
- Prefer asserting the panel title, the webview root element, and at least one rendered table node.

### E2E-02 Render Connected And Isolated Models
Spec reference:
- `spec.md` 4.3, 5, 9

Fixture:
- project with connected models and isolated models

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.

Expected results:
- Models participating in relations are rendered.
- Models with no relations are also rendered.
- Isolated models are not silently dropped from the ERD.

Automation notes:
- Assert by model IDs or table labels rather than screen position.

### E2E-03 Show Structural Relations
Spec reference:
- `spec.md` 4.3, 4.4, 4.7, 9

Fixture:
- project with `ForeignKey`, `OneToOneField`, and `ManyToManyField`

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.

Expected results:
- Structural relation lines are rendered between related tables.
- Relation rendering includes all supported relation categories present in the fixture.
- Tables that should be structurally unrelated do not gain false structural edges.

Automation notes:
- Prefer DOM or SVG edge metadata that includes source model ID, target model ID, and relation type.

### E2E-04 Display Choice Fields And Options
Spec reference:
- `spec.md` 4.2, 4.4, 5, 9

Fixture:
- project with Django `choices` fields

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Inspect the model table containing a `choices` field.

Expected results:
- The `choices` field is visually distinguishable from an ordinary scalar field.
- The field is identifiable as enum-like.
- The selectable options are visible when choice metadata is statically inferable.

Automation notes:
- Assert on stable markers such as field row kind, enum badge, and rendered option items.

### E2E-05 Select Table And Show Property Attributes
Spec reference:
- `spec.md` 4.2, 4.4, 4.5, 5, 9

Fixture:
- project with `@property` attributes on a model

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select the target model table.

Expected results:
- Property-decorated attributes are visible for the selected model.
- Property attributes are visually distinct from persisted model fields.
- Property attributes are scoped to the selected table and do not appear on unrelated tables.

### E2E-06 Select Table And Show User-Defined Methods
Spec reference:
- `spec.md` 4.2, 4.4, 4.5, 5, 9

Fixture:
- project with user-defined model methods

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select the target model table.

Expected results:
- User-defined methods declared on the model are visible.
- Framework-inherited methods are not presented as user-defined methods.
- Method rows are visually distinct from fields and `@property` attributes.

### E2E-07 Method Inspection Highlights Related Tables
Spec reference:
- `spec.md` 4.2, 4.4, 4.5, 5, 9

Fixture:
- project where a user-defined method references related models in a statically visible way

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select the target table.
4. Reveal or inspect a user-defined method.

Expected results:
- Tables associated with that method are highlighted.
- Highlighted tables match inferred method-related model references.
- Highlighting is visually distinct from structural ERD edges.
- Unrelated tables are not highlighted.

Automation notes:
- Prefer explicit highlight classes or data attributes on table nodes.

### E2E-08 Toggle Method Visibility Per Table
Spec reference:
- `spec.md` 4.5, 9

Fixture:
- project with multiple model tables and user-defined methods

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select table A and turn off method visibility.
4. Select table B and keep method visibility enabled.

Expected results:
- Methods are hidden for table A only.
- Methods remain visible for table B.
- The toggle is table-scoped, not global.

### E2E-09 Toggle Property Visibility Per Table
Spec reference:
- `spec.md` 4.5, 9

Fixture:
- project with multiple model tables and `@property` attributes

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select table A and turn off property visibility.
4. Select table B and keep property visibility enabled.

Expected results:
- Property attributes are hidden for table A only.
- Property attributes remain visible for table B.
- The toggle is table-scoped, not global.

### E2E-10 Toggle Method-Driven Highlighting Per Table
Spec reference:
- `spec.md` 4.5, 9

Fixture:
- project with user-defined methods that infer related tables

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select a table with method-inferred associations.
4. Turn off method-driven highlighting for that table.
5. Inspect the method again.

Expected results:
- Method metadata may remain visible.
- Related-table highlighting is suppressed for that table.
- Structural ERD edges remain visible.

### E2E-11 Switch Layout Modes
Spec reference:
- `spec.md` 4.6, 9

Fixture:
- project with enough nodes to make layout changes observable

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Switch from hierarchical to circular layout.
4. Switch from circular to clustered layout.

Expected results:
- The view updates after each layout change.
- The same set of tables remains present across layouts.
- Structural edges remain attached to the correct tables after each layout change.

Automation notes:
- Assert on selected layout state and changed node positions rather than exact coordinates.

### E2E-12 Drag Table And Preserve Manual Position
Spec reference:
- `spec.md` 4.5, 9

Fixture:
- project with multiple visible tables

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Drag one table to a new position.

Expected results:
- The dragged table moves in the ERD.
- Connected structural edges update to the new position.
- Other tables do not move unexpectedly unless layout logic explicitly requires it.

### E2E-13 Hide Table
Spec reference:
- `spec.md` 4.5, 9

Fixture:
- project with multiple visible tables

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Hide a selected table.

Expected results:
- The selected table is hidden from the current ERD view.
- Structural edges connected to the hidden table are removed or hidden consistently.
- Other tables remain visible.

### E2E-14 Crossing Indicator Is Visible
Spec reference:
- `spec.md` 4.7, 9

Fixture:
- project and layout combination that intentionally produces at least one structural edge crossing

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Select the layout known to produce crossings.

Expected results:
- Crossing structural edges remain visually distinguishable as crossings rather than merged lines.
- The crossing indicator is applied only where crossings occur.

Automation notes:
- Prefer explicit crossing metadata rendered into DOM or SVG markers rather than screenshot-only checks.

### E2E-15 Refresh Diagram After Analysis
Spec reference:
- `spec.md` 4.1, 4.3, 4.4, 9

Fixture:
- any stable Django fixture workspace

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Run `Django ERD: Refresh Diagram`.

Expected results:
- The diagram refreshes without breaking the webview.
- The rendered tables and relations still reflect the fixture project.
- The refresh path uses the same core analysis flow as the initial load.

### E2E-16 Partial Analysis Still Produces Usable ERD
Spec reference:
- `spec.md` 4.2, 7, 9

Fixture:
- project with unsupported or partially inferable model constructs

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.

Expected results:
- The ERD still renders for the supported parts of the project.
- Diagnostics are surfaced for unsupported or partial analysis areas.
- The extension does not silently fail or show an empty graph unless the fixture truly contains no renderable models.

### E2E-17 Multi-App Project End-To-End
Spec reference:
- `spec.md` 4.1, 4.2, 4.3, 9

Fixture:
- multi-app Django project with cross-app relations, `choices`, properties, and methods

Environment:
- fixture workspace uses a Python interpreter with Django installed

Steps:
1. Open the fixture workspace.
2. Run `Django ERD: Open Diagram`.
3. Inspect tables from multiple apps.
4. Switch layouts.
5. Select a table with methods and properties.

Expected results:
- Models from multiple apps are discovered.
- Cross-app relations are rendered.
- Choice fields, properties, and methods are displayed where defined.
- The graph remains coherent across interaction and layout changes.

## 8. Scenario Coverage Map
- Project discovery and analysis startup: E2E-01, E2E-15, E2E-17
- Connected and disconnected graph coverage: E2E-02, E2E-17
- Structural relation rendering: E2E-03
- Choice field rendering: E2E-04, E2E-17
- Property rendering: E2E-05, E2E-09, E2E-17
- Method rendering and highlighting: E2E-06, E2E-07, E2E-08, E2E-10, E2E-17
- Layout switching: E2E-11, E2E-17
- Drag and hide interactions: E2E-12, E2E-13
- Edge crossing clarity: E2E-14
- Partial-analysis resilience: E2E-16

## 9. Implementation Guidance
- Name test files and cases by scenario ID so failures map back to this document.
- Keep fixtures small and purposeful; do not use one massive fixture for all scenarios.
- Prefer one E2E test per scenario or tightly related scenario pair.
- If a scenario becomes untestable due to missing stable selectors, add test-friendly markers to the UI rather than weakening the scenario.
- E2E automation should bootstrap or select the real Django-installed interpreter required by each fixture workspace.
- E2E automation should fail loudly if Django is missing from the configured interpreter instead of silently falling back to mocks.
- E2E automation should use the same user-facing commands that a real user would trigger.

## 10. Maintenance Rule
When `spec.md` changes, review this document in the same change or immediately after. E2E automation should be updated from this file rather than being expanded informally inside test code.
