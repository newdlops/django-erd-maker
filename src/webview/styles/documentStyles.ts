export function getDocumentStyles(): string {
  return `
    :root {
      color-scheme: dark;
      --bg: #081018;
      --panel: rgba(7, 18, 28, 0.86);
      --panel-border: rgba(129, 169, 181, 0.22);
      --table: #0f1e2c;
      --table-border: rgba(123, 196, 170, 0.26);
      --table-header: #173247;
      --text: #e7f2f0;
      --muted: #9cb8ba;
      --accent: #6dd0b0;
      --accent-2: #ffbf69;
      --danger: #ff7c70;
      --grid: rgba(120, 167, 178, 0.08);
      font-family: "Iowan Old Style", "Palatino Linotype", "Book Antiqua", Georgia, serif;
    }

    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      background:
        radial-gradient(circle at 10% 12%, rgba(109, 208, 176, 0.18), transparent 26%),
        radial-gradient(circle at 84% 18%, rgba(255, 191, 105, 0.16), transparent 22%),
        linear-gradient(180deg, #0c141c 0%, #071018 100%);
      color: var(--text);
    }

    button { font: inherit; }
    [hidden] { display: none !important; }

    .erd-shell {
      display: grid;
      gap: 18px;
      grid-template-columns: minmax(320px, 380px) minmax(0, 1fr);
      padding: 18px;
    }

    .erd-sidebar,
    .erd-stage {
      border: 1px solid var(--panel-border);
      border-radius: 24px;
      background: var(--panel);
      backdrop-filter: blur(12px);
      box-shadow: 0 28px 80px rgba(0, 0, 0, 0.26);
    }

    .erd-sidebar {
      display: flex;
      flex-direction: column;
      gap: 16px;
      padding: 20px;
      max-height: calc(100vh - 36px);
      overflow: auto;
    }

    .erd-summary__eyebrow,
    .erd-panel__eyebrow {
      margin: 0;
      letter-spacing: 0.16em;
      text-transform: uppercase;
      font-size: 11px;
      color: var(--accent);
    }

    .erd-summary__title,
    .erd-panel h2 {
      margin: 6px 0 8px;
      font-size: 28px;
      line-height: 1.1;
    }

    .erd-summary__meta,
    .erd-panel__meta,
    .erd-sidebar__meta,
    .erd-panel__hint {
      margin: 0;
      color: var(--muted);
      line-height: 1.55;
      font-size: 13px;
    }

    .erd-panel {
      display: grid;
      gap: 14px;
      padding: 16px;
      border-radius: 18px;
      border: 1px solid rgba(113, 159, 171, 0.18);
      background: rgba(8, 19, 28, 0.62);
    }

    .erd-panel.is-selected {
      border-color: rgba(109, 208, 176, 0.34);
      box-shadow: inset 0 0 0 1px rgba(109, 208, 176, 0.24);
    }

    .erd-panel__section h3,
    .erd-sidebar__section h2 {
      margin: 0 0 10px;
      font-size: 14px;
      color: var(--accent-2);
      letter-spacing: 0.08em;
      text-transform: uppercase;
    }

    .erd-list {
      list-style: none;
      margin: 0;
      padding: 0;
      display: grid;
      gap: 8px;
    }

    .erd-list__item {
      display: grid;
      gap: 6px;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.45;
    }

    .erd-list__item--enum-option { color: #e4d3a7; }

    .erd-settings {
      display: grid;
      gap: 12px;
    }

    .erd-setting {
      display: grid;
      gap: 8px;
    }

    .erd-setting__header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      font-size: 13px;
    }

    .erd-setting__label {
      color: var(--text);
    }

    .erd-setting__value {
      color: var(--accent);
      font-size: 12px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
    }

    .erd-setting__range {
      width: 100%;
      margin: 0;
      accent-color: var(--accent);
    }

    .erd-badge {
      justify-self: start;
      padding: 2px 8px;
      border-radius: 999px;
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      background: rgba(109, 208, 176, 0.14);
      color: var(--accent);
    }

    .erd-badge--warning { color: var(--accent-2); background: rgba(255, 191, 105, 0.14); }
    .erd-badge--error { color: var(--danger); background: rgba(255, 124, 112, 0.16); }

    .erd-method-buttons {
      display: grid;
      gap: 8px;
    }

    .erd-method-card {
      display: grid;
      gap: 8px;
    }

    .erd-method-button,
    .erd-control-pill,
    .erd-inline-button,
    .erd-tool {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 12px;
      border-radius: 14px;
      border: 1px solid rgba(122, 163, 177, 0.22);
      background: rgba(16, 31, 44, 0.9);
      color: var(--text);
      padding: 10px 12px;
      cursor: pointer;
    }

    .erd-method-button.is-active,
    .erd-control-pill.is-active,
    .erd-tool.is-active,
    .erd-tool:hover {
      border-color: rgba(109, 208, 176, 0.44);
      background: rgba(22, 56, 67, 0.92);
    }

    .erd-control-pill {
      min-width: 0;
      padding: 8px 10px;
      font-size: 12px;
    }

    .erd-control-pill__status {
      color: var(--muted);
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }

    .erd-inline-button {
      justify-content: center;
      padding: 8px 10px;
      font-size: 12px;
    }

    .erd-method-links {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }

    .erd-relation-chip {
      display: inline-flex;
      align-items: center;
      padding: 4px 9px;
      border-radius: 999px;
      font-size: 11px;
      color: var(--text);
      background: rgba(123, 196, 170, 0.12);
      border: 1px solid rgba(123, 196, 170, 0.18);
    }

    .erd-relation-chip--high {
      color: var(--accent);
      border-color: rgba(109, 208, 176, 0.34);
    }

    .erd-relation-chip--medium {
      color: #c9e4ff;
      border-color: rgba(168, 216, 255, 0.28);
    }

    .erd-relation-chip--low {
      color: var(--accent-2);
      border-color: rgba(255, 191, 105, 0.3);
    }

    .erd-stage {
      display: grid;
      grid-template-rows: auto minmax(0, 1fr);
      min-height: calc(100vh - 36px);
      overflow: hidden;
    }

    .erd-stage__toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      padding: 14px 16px 0;
    }

    .erd-toolbar-group {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
    }

    .erd-canvas {
      overflow: hidden;
      cursor: grab;
      position: relative;
      border-radius: 24px;
      margin: 12px;
      background:
        linear-gradient(var(--grid) 1px, transparent 1px),
        linear-gradient(90deg, var(--grid) 1px, transparent 1px),
        linear-gradient(180deg, rgba(17, 35, 50, 0.35), rgba(8, 16, 24, 0.18));
      background-size: 32px 32px, 32px 32px, auto;
      touch-action: none;
    }

    .erd-canvas.is-panning { cursor: grabbing; }
    .erd-canvas.is-dragging-table { cursor: default; }
    .erd-scene { width: 100%; height: 100%; min-height: 720px; display: block; }
    .erd-scene__backdrop { fill: transparent; }
    .erd-viewport { transition: transform 120ms ease-out; transform-origin: 0 0; }

    .erd-marker { fill: none; stroke: #b6e7d9; stroke-width: 1.3; stroke-linecap: round; stroke-linejoin: round; }
    .erd-edge {
      fill: none;
      stroke: #b6e7d9;
      stroke-width: 2.3;
      stroke-linecap: round;
      stroke-linejoin: round;
      opacity: 0.96;
    }
    .erd-edge--derived_reverse { stroke: #9dcfe1; stroke-dasharray: 7 6; }
    .erd-edge--many-to-many { stroke: #f7d18a; }
    .erd-edge--reverse-many-to-many { stroke: #f7d18a; stroke-dasharray: 7 6; }

    .erd-method-overlay {
      stroke: rgba(255, 191, 105, 0.7);
      stroke-width: 3;
      stroke-dasharray: 10 8;
      opacity: 0;
      transition: opacity 140ms ease;
      pointer-events: none;
      filter: drop-shadow(0 0 10px rgba(255, 191, 105, 0.24));
    }
    .erd-method-overlay.is-active { opacity: 1; }

    .erd-crossing circle {
      fill: #0c141c;
      stroke: rgba(255, 191, 105, 0.9);
      stroke-width: 1.4;
    }
    .erd-crossing path {
      fill: none;
      stroke: rgba(255, 191, 105, 0.9);
      stroke-width: 1.4;
      stroke-linecap: round;
    }

    .erd-table { cursor: pointer; outline: none; touch-action: none; }
    .erd-table__frame {
      fill: var(--table);
      stroke: var(--table-border);
      stroke-width: 1.4;
      filter: drop-shadow(0 18px 36px rgba(0, 0, 0, 0.3));
    }
    .erd-table__header { fill: var(--table-header); opacity: 0.95; }
    .erd-table__divider { stroke: rgba(144, 189, 200, 0.18); stroke-width: 1; }
    .erd-table__divider--section { stroke: rgba(109, 208, 176, 0.14); }
    .erd-table__app {
      fill: var(--accent);
      font-size: 10px;
      letter-spacing: 0.16em;
      text-transform: uppercase;
    }
    .erd-table__title { fill: var(--text); font-size: 18px; font-weight: 700; }
    .erd-table__row { fill: var(--muted); font-size: 13px; }
    .erd-table__row--enum-option { fill: #e4d3a7; font-size: 12px; }
    .erd-table__row--property { fill: #a8d8ff; }
    .erd-table__row--method { fill: #ffcf8a; }

    .erd-table.is-selected .erd-table__frame {
      stroke: rgba(109, 208, 176, 0.62);
      stroke-width: 2.2;
    }
    .erd-table.is-method-target .erd-table__frame {
      stroke: rgba(255, 191, 105, 0.72);
      stroke-width: 2.2;
    }

    .erd-table.is-dragging .erd-table__frame {
      stroke: rgba(168, 216, 255, 0.72);
      stroke-width: 2.2;
    }

    .erd-panel__controls {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }

    .erd-hidden-table {
      grid-template-columns: minmax(0, 1fr) auto;
      align-items: center;
    }

    @media (max-width: 1180px) {
      .erd-shell {
        grid-template-columns: 1fr;
      }
      .erd-sidebar,
      .erd-stage {
        max-height: none;
        min-height: 640px;
      }
    }

    @media (max-width: 620px) {
      .erd-panel__controls {
        grid-template-columns: 1fr;
      }
    }
  `;
}
