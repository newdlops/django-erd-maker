# Django ERD Maker VS Code Extension Specification

## 1. Overview

This project is a VS Code extension that analyzes Django projects and renders an interactive Entity Relationship Diagram (ERD) in a webview.

The extension must:

- discover Django apps and models from a target workspace,
- statically analyze model definitions with a Rust-based engine,
- build a full project-wide relationship graph,
- render the graph as a vector-based ERD,
- provide interaction and layout controls suitable for large schemas,
- preserve a codebase structure that is easy to extend over time.

## 2. Product Goal

The primary goal is to help developers understand the structure of a Django project by generating a visual ERD directly inside VS Code, without depending on runtime execution of the Django app.

## 3. Scope

### In Scope

- VS Code extension UI and commands
- Django model discovery and static analysis
- Relationship graph construction across the entire project
- Interactive ERD rendering in a webview
- Multiple layout strategies
- Extensible architecture for future UI and analysis features

### Out of Scope for the Initial Spec

- Live database introspection
- Migration graph visualization
- Full Django runtime bootstrapping as a hard dependency
- Non-Django ORM support

## 4. Core Functional Requirements

### 4.1 Django Project Analysis

- The extension must scan the current workspace for Django apps and model definitions.
- Analysis must be based primarily on static analysis rather than executing project code.
- The analysis engine must be implemented in Rust to prioritize speed and predictable performance.
- The extension side must treat the analyzer as a separable subsystem so the analysis engine can evolve independently.

### 4.2 Model Extraction

- The analyzer must identify Django models defined in project apps.
- It must extract enough metadata to render an ERD, including:
  - app name,
  - model name,
  - fields,
  - property-decorated computed attributes,
  - user-defined model methods,
  - field type,
  - choice or enum metadata for fields that declare selectable options,
  - relation type,
  - target model reference,
  - reverse relation metadata when inferable,
  - method-level related model references when inferable.
- The analyzer should preserve unresolved or partially resolved references as structured diagnostics rather than silently dropping them.
- User-defined methods means methods declared on the model itself rather than inherited framework methods.
- `@property`-decorated members must be represented separately from real model fields so the UI can distinguish persisted schema from computed logic-oriented attributes.
- Fields that use Django `choices` must expose enum-like metadata and statically inferable option lists when possible.

### 4.3 Relationship Graph Construction

- The system must build a project-wide graph of model relationships.
- The graph builder must follow relations using DFS or an equivalent traversal strategy.
- The graph must include:
  - `ForeignKey`,
  - `OneToOneField`,
  - `ManyToManyField`,
  - reverse relations,
  - disconnected models with no relations.
- The final graph must represent the complete known structure of the project, not only the connected component of one starting model.

### 4.4 ERD Rendering

- The ERD must be rendered in vector format.
- SVG is the default rendering target unless a stronger vector alternative is later justified.
- The ERD must be displayed in a VS Code webview.
- Each model must be rendered as a table-like node aligned with ERD conventions.
- Relationship edges must be rendered automatically between related tables.
- Fields backed by Django `choices` must be visually identifiable as enum-like fields.
- When choice metadata is available, the UI must be able to show the selectable options for that field.
- Each model view must be able to show property-decorated computed attributes in addition to persisted model fields.
- Selecting a table must reveal the model's user-defined methods.
- When a method is shown, the UI must be able to highlight tables associated with that method based on inferred related model references.

### 4.5 Interaction

- Users must be able to move tables manually in the ERD view.
- Users must be able to hide selected tables.
- Users must be able to select a table and inspect logic-oriented metadata for that model.
- Users must be able to toggle method visibility per table.
- Users must be able to toggle property-decorated attribute visibility per table.
- Users must be able to turn method-driven related-table highlighting on or off per table.
- The UI should support future view operations without major restructuring.
- The interaction layer should be designed so features such as filtering, pinning, collapsing, and grouping can be added later with minimal architectural churn.

### 4.6 Layout Options

- Users must be able to choose how tables are arranged.
- The initial layout option set must include:
  - hierarchical,
  - circular,
  - clustered.
- Layout application must be decoupled from rendering so new strategies can be added later.

### 4.7 Edge Routing and Visual Clarity

- Relationship lines must follow standard ERD semantics.
- The routing system must automatically connect tables while minimizing overlap as much as reasonably possible.
- The renderer must visually distinguish line crossings so users can recognize that two edges cross rather than merge.
- Automatic placement should optimize readability for medium and large schemas, not only small examples.

## 5. UI and UX Requirements

- The main visualization surface must be a webview inside VS Code.
- The ERD must remain readable for both connected and disconnected model groups.
- The UI must clearly distinguish persisted model fields, enum-like choice fields, property-decorated computed attributes, and user-defined methods.
- Method-driven table highlighting must act as a secondary comprehension aid and must not be confused with structural ERD edges.
- The UI structure must be modular so visual styling, node rendering, and edge rendering can be evolved independently.
- The extension should separate state management, layout computation, graph data, and presentation concerns.

## 6. Architecture Requirements

### 6.1 Layering

The project must be organized into clear layers, at minimum:

- VS Code extension host layer
- analysis orchestration layer
- Rust analyzer layer
- graph/domain layer
- layout layer
- rendering/webview UI layer

Each layer should depend on stable interfaces rather than concrete implementation details from unrelated layers.

### 6.2 Extensibility

- The codebase must prioritize abstraction and maintainable boundaries.
- Rendering behavior, layout behavior, and analysis behavior must be replaceable or extendable through well-defined interfaces.
- UI-specific logic must not be tightly coupled to Rust analyzer internals.
- Graph/domain types should be reusable across multiple renderers or future export formats.

### 6.3 File Size Constraint

- No single source code file should exceed 500 lines.
- Markdown documentation files may exceed that limit, but should stay under 3000 lines.
- If a source module trends toward the code-file limit, it must be split by responsibility rather than kept as a monolith.

## 7. Quality Requirements

- Static analysis must be fast enough to feel responsive on real Django projects.
- Failures in partial analysis must degrade gracefully and surface diagnostics where possible.
- The renderer must preserve vector quality during zoom and interaction.
- The project structure must support long-term growth without requiring large-scale rewrites.

## 8. Suggested Internal Module Direction

The implementation should likely separate responsibilities into modules similar to the following:

- extension command and activation
- workspace and project discovery
- Rust analyzer bridge
- model metadata schema
- relationship graph builder
- layout strategy registry
- edge routing engine
- webview state store
- ERD renderer
- user interaction controller

This list is directional, not mandatory, but the final structure should preserve equivalent separation of concerns.

## 9. Acceptance Criteria

The first usable version should satisfy the following:

- A user can open a Django project in VS Code and launch the ERD view.
- The extension discovers models across apps through static analysis.
- The resulting graph includes relation-connected models and isolated models.
- The ERD appears in a vector-based webview renderer.
- Users can move and hide tables.
- Users can select a table and view user-defined model methods.
- Users can view `@property`-decorated computed attributes per model.
- Users can identify fields with Django `choices` and inspect their selectable options when statically inferable.
- Users can toggle method visibility, property visibility, and method-driven table highlighting on or off per table.
- Users can switch between hierarchical, circular, and clustered layouts.
- Relations are drawn automatically with ERD-style semantics.
- Edge routing attempts to reduce overlap and visually indicates crossings.
- Method selection or inspection can highlight related tables inferred from that method.
- The codebase remains modular, layered, and free of oversized files.

## 10. Future-Friendly Design Direction

The initial implementation should avoid locking the project into a narrow UI or analyzer pipeline. The specification favors interfaces and separable modules so later work can add features such as export, search, filtering, layout persistence, grouped subgraphs, richer Django relation inference, or deeper method-level dependency analysis without major redesign.
