export function getDocumentStyles(): string {
  return `
    html {
      height: 100%;
    }

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
      font-family: var(--vscode-font-family, system-ui, sans-serif);
    }

    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100dvh;
      height: 100%;
      background:
        radial-gradient(circle at 10% 12%, rgba(109, 208, 176, 0.18), transparent 26%),
        radial-gradient(circle at 84% 18%, rgba(255, 191, 105, 0.16), transparent 22%),
        linear-gradient(180deg, #0c141c 0%, #071018 100%);
      color: var(--text);
    }

    button, input, select { font: inherit; touch-action: manipulation; }
    button:disabled { opacity: 0.46; cursor: default; }
    button:focus-visible, summary:focus-visible, input:focus-visible, select:focus-visible {
      outline: 2px solid var(--accent);
      outline-offset: 3px;
    }
    [hidden] { display: none !important; }

    .erd-summary__title,
    .erd-summary__meta,
    .erd-sidebar__meta,
    .erd-panel,
    .erd-panel h2,
    .erd-panel__meta,
    .erd-panel__hint,
    .erd-list__item,
    .erd-method-card,
    .erd-method-button,
    .erd-control-pill,
    .erd-inline-button,
    .erd-tool,
    .erd-relation-chip,
    .erd-setting__header,
    .erd-setting__label,
    .erd-setting__value,
    .erd-badge,
    .erd-search__count,
    .erd-gpu-warning__title,
    .erd-gpu-warning__body {
      min-width: 0;
      max-width: 100%;
      overflow-wrap: anywhere;
      word-break: break-word;
      white-space: normal;
    }

    .erd-method-button > span:first-child,
    .erd-control-pill > span:first-child,
    .erd-tool > span:first-child {
      flex: 1 1 auto;
      min-width: 0;
      text-align: left;
    }

    .erd-shell {
      display: grid;
      gap: 12px;
      grid-template-columns: 320px minmax(0, 1fr);
      padding: 12px;
      height: 100dvh;
      overflow: hidden;
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
      display: grid;
      grid-template-rows: auto minmax(0, 1fr);
      gap: 12px;
      padding: 12px;
      min-height: 0;
      min-width: 0;
      overflow: hidden;
    }

    .erd-sidebar__tabs {
      position: relative;
      z-index: 4;
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 6px;
      padding: 4px;
      border: 1px solid rgba(122, 163, 177, 0.18);
      border-radius: 16px;
      background: rgba(7, 18, 28, 0.96);
      box-shadow: 0 10px 24px rgba(0, 0, 0, 0.18);
    }

    .erd-sidebar__tab {
      min-width: 0;
      padding: 9px 10px;
      border: 1px solid transparent;
      border-radius: 12px;
      background: transparent;
      color: var(--muted);
      cursor: pointer;
      overflow-wrap: anywhere;
    }

    .erd-sidebar__tab.is-active {
      border-color: rgba(109, 208, 176, 0.34);
      background: rgba(22, 56, 67, 0.92);
      color: var(--text);
    }

    .erd-sidebar__tab:focus-visible {
      outline: 2px solid var(--accent);
      outline-offset: 2px;
    }

    .erd-sidebar__sheet {
      display: grid;
      align-content: start;
      gap: 16px;
      min-width: 0;
      min-height: 0;
      overflow: auto;
      overscroll-behavior: contain;
      padding: 3px;
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
      font-size: 23px;
      line-height: 1.2;
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
      padding: 12px;
      border-radius: 18px;
      border: 1px solid rgba(113, 159, 171, 0.18);
      background: rgba(8, 19, 28, 0.62);
    }

    .erd-panel.is-selected {
      border-color: rgba(109, 208, 176, 0.34);
      box-shadow: inset 0 0 0 1px rgba(109, 208, 176, 0.24);
    }

    .erd-panel__header {
      padding: 4px 0 8px; border-bottom: 1px solid var(--panel-border);
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
      flex-wrap: wrap;
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
    .erd-badge--relation-foreign-key,
    .erd-badge--relation-reverse-foreign-key { color: #72e0b1; background: rgba(86, 205, 157, 0.16); }
    .erd-badge--relation-one-to-one,
    .erd-badge--relation-reverse-one-to-one { color: #82bdff; background: rgba(91, 157, 235, 0.16); }
    .erd-badge--relation-many-to-many,
    .erd-badge--relation-reverse-many-to-many { color: #ffd07a; background: rgba(238, 173, 66, 0.16); }
    .erd-badge--relation-inheritance { color: #c6a4ff; background: rgba(167, 119, 235, 0.16); }

    .erd-relationship-list { gap: 10px; }
    .erd-relationship {
      gap: 7px;
      padding: 10px;
      border: 1px solid rgba(122, 163, 177, 0.16);
      border-radius: 12px;
      background: rgba(16, 31, 44, 0.58);
    }
    .erd-relationship.is-revealed {
      border-color: rgba(255, 191, 105, 0.68);
      background: rgba(255, 191, 105, 0.10);
      box-shadow: inset 0 0 0 1px rgba(255, 191, 105, 0.16);
    }
    .erd-relationship__header {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 8px;
      min-width: 0;
    }
    .erd-relationship__target {
      width: 100%;
      min-width: 0;
      padding: 0;
      border: 0;
      background: transparent;
      color: var(--text);
      cursor: pointer;
      text-align: left;
      line-height: 1.5;
      overflow-wrap: anywhere;
      word-break: break-word;
      white-space: normal;
    }
    .erd-relationship__target:hover { color: var(--accent-2); }
    .erd-relationship__target:focus-visible {
      outline: 2px solid var(--accent);
      outline-offset: 3px;
      border-radius: 4px;
    }

    .erd-method-buttons {
      display: grid;
      gap: 8px;
    }

    .erd-connections { display: grid; gap: 10px; min-width: 0; }
    .erd-connections__heading { display: flex; align-items: center; justify-content: space-between; gap: 8px; }
    .erd-connections__heading h3 { margin: 0; font-size: 15px; }
    .erd-connections__heading h3 span { color: var(--muted); font-weight: 400; }
    .erd-connections__heading .erd-tool { padding: 6px 9px; min-height: 32px; font-size: 12px; }
    .erd-connections__search input {
      width: 100%; min-width: 0; padding: 10px;
      border: 1px solid var(--panel-border); border-radius: 8px;
      background: var(--bg); color: var(--text); font-size: 13px;
    }
    .erd-connections__search input::placeholder { color: var(--muted); }
    .erd-connections__filters { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 5px; }
    .erd-connections__filters button {
      display: flex; align-items: center; justify-content: space-between; gap: 4px;
      min-height: 34px; padding: 6px 8px; min-width: 0;
      border: 1px solid var(--panel-border); border-radius: 7px;
      background: transparent; color: var(--muted); cursor: pointer;
      font-size: 11px; text-align: left; overflow-wrap: anywhere;
    }
    .erd-connections__filters button[aria-pressed="true"] {
      border-color: var(--accent); background: rgba(109, 208, 176, 0.12); color: var(--text);
    }
    .erd-connections__filters button:hover { background: rgba(109, 208, 176, 0.12); }
    .erd-connections__filters span { font-variant-numeric: tabular-nums; white-space: nowrap; flex-shrink: 0; }
    .erd-connections__count { margin: 0; font-size: 12px; color: var(--muted); }
    .erd-connections__list { display: grid; gap: 0; list-style: none; padding: 0; margin: 0; }
    .erd-connection-row {
      display: grid; gap: 5px; width: 100%; min-width: 0; padding: 11px 8px;
      border: 1px solid transparent; border-bottom-color: var(--panel-border); border-radius: 4px;
      background: transparent; color: var(--text); text-align: left; cursor: pointer;
      line-height: 1.4; overflow-wrap: anywhere;
    }
    .erd-connection-row:hover { background: rgba(109, 208, 176, 0.08); }
    .erd-connection-row.is-revealed {
      border-color: var(--accent-2); background: rgba(255, 191, 105, 0.09);
    }
    .erd-connection-row__top { display: grid; gap: 2px; min-width: 0; }
    .erd-connection-row__top strong { font-size: 14px; font-weight: 600; }
    .erd-connection-row__direction { font-size: 11px; color: var(--muted); }
    .erd-connection-row__field { font: 12px var(--vscode-editor-font-family, ui-monospace, monospace); color: var(--accent-2); }
    .erd-connection-row__meta { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 6px; font-size: 11px; color: var(--muted); }
    .erd-connection-row .erd-badge { font-size: 10px; letter-spacing: 0; text-transform: none; padding: 2px 6px; }
    .erd-connections__more { margin-top: 12px; width: 100%; font-size: 12px; }
    .erd-connections__empty { padding: 16px 0; }
    .erd-model-details { border-top: 1px solid var(--panel-border); padding-top: 10px; }
    .erd-model-details summary { color: var(--muted); font-size: 12px; padding: 8px 0; cursor: pointer; }
    .erd-model-details > div { margin-top: 14px; }

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
      flex: 0 0 auto;
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

    .erd-tool--layout {
      justify-content: center;
      padding: 8px 10px;
      font-size: 12px;
      line-height: 1.2;
    }

    .erd-method-links {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
    }

    .erd-relation-chip {
      display: inline-flex;
      align-items: center;
      line-height: 1.35;
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
      grid-template-rows: auto auto minmax(0, 1fr);
      min-height: 0;
      min-width: 0;
      overflow: hidden;
    }

    .erd-stage__toolbar {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 8px;
      padding: 12px;
      border-bottom: 1px solid var(--panel-border);
    }

    .erd-toolbar-group {
      display: flex;
      flex-wrap: wrap;
      align-items: center;
      gap: 6px;
    }

    .erd-stage__toolbar .erd-tool { min-height: 34px; border-radius: 8px; padding: 7px 10px; font-size: 12px; }
    .erd-related-open { border-color: rgba(109, 208, 176, 0.6); justify-content: center; }
    .is-related-diagram .erd-full-diagram-tools,
    .is-related-diagram .erd-layout-options,
    .is-related-diagram .erd-connection-context,
    .is-related-diagram .erd-stage__toolbar [data-related-diagram-open] { display: none; }
    .erd-connections__group { display: grid; gap: 6px; padding: 8px; border: 1px solid var(--accent-2); border-radius: 8px; font-size: 12px; }
    .erd-connections__group .erd-tool { min-height: 32px; padding: 5px 8px; font-size: 12px; }
    .erd-related-diagram { grid-row: 3; display: grid; grid-template-rows: auto auto auto minmax(0, 1fr); min-width: 0; min-height: 0; }
    .erd-related-header { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 10px; padding: 14px 16px 8px; min-width: 0; }
    .erd-related-header > div { min-width: 0; }
    .erd-related-header h2 { margin: 0 0 4px; font-size: 18px; line-height: 1.35; overflow-wrap: anywhere; }
    .erd-related-actions, .erd-related-pagination { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
    .erd-related-diagram .erd-tool { padding: 6px 9px; min-height: 32px; font-size: 12px; border-radius: 7px; }
    .erd-related-status { display: flex; align-items: center; flex-wrap: wrap; gap: 8px; padding: 0 16px 8px; }
    .erd-related-status p { flex: 1 1 240px; margin: 0; color: var(--muted); font-size: 12px; line-height: 1.5; overflow-wrap: anywhere; }
    .erd-related-pagination { padding: 8px 16px; border-bottom: 1px solid var(--panel-border); font-size: 12px; color: var(--muted); font-variant-numeric: tabular-nums; }
    .erd-related-pagination > span:first-child { flex: 1 1 auto; }
    .erd-related-scroll { min-width: 0; min-height: 0; overflow: auto; overscroll-behavior: contain; background: var(--bg); }
    .erd-related-scroll:focus-visible { outline: 2px solid var(--accent); outline-offset: -3px; }
    .erd-related-graph { position: relative; display: grid; grid-template-columns: minmax(0, 1fr) 64px minmax(180px, 0.9fr) 64px minmax(0, 1fr); padding: 24px; min-height: 100%; align-items: center; isolation: isolate; }
    .erd-related-lines { position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none; z-index: -1; }
    .erd-related-wing { display: grid; align-content: center; gap: 16px; min-width: 0; grid-row: 1; }
    .erd-related-wing--left { grid-column: 1; }
    .erd-related-wing--right { grid-column: 5; }
    .erd-related-center { grid-column: 3; grid-row: 1; min-width: 0; padding: 14px; border: 1px solid var(--accent-2); border-radius: 12px; background: var(--table-header); overflow-wrap: anywhere; }
    .erd-related-center > span { color: var(--accent-2); font-size: 11px; }
    .erd-related-center h3 { margin: 8px 0; font-size: 18px; line-height: 1.35; }
    .erd-related-center p { margin: 0 0 12px; color: var(--muted); font-size: 12px; }
    .erd-related-card { min-width: 0; border: 1px solid var(--panel-border); border-radius: 10px; background: var(--table); overflow-wrap: anywhere; }
    .erd-related-card.is-selected { border-color: var(--accent-2); background: #20302c; }
    .erd-related-card__select { display: grid; gap: 6px; padding: 12px; width: 100%; min-width: 0; text-align: left; background: transparent; border: 0; border-radius: 10px; color: var(--text); cursor: pointer; }
    .erd-related-card__select:hover { background: rgba(109, 208, 176, 0.1); }
    .erd-related-card__select strong { font-size: 14px; font-weight: 600; line-height: 1.4; overflow-wrap: anywhere; }
    .erd-related-card__direction, .erd-related-card__meta { font-size: 11px; color: var(--muted); line-height: 1.4; }
    .erd-related-card__members { font-size: 12px; line-height: 1.5; overflow-wrap: anywhere; }
    .erd-related-card__follow { display: block; width: 100%; min-height: 34px; padding: 7px 12px; border: 0; border-top: 1px solid var(--panel-border); background: transparent; color: var(--accent); text-align: left; font-size: 12px; cursor: pointer; overflow-wrap: anywhere; }
    .erd-related-card__follow:hover { background: rgba(109, 208, 176, 0.1); }
    .erd-related-card__member { display: block; padding: 0 8px 8px; }
    .erd-related-card__member select { display: block; width: 100%; min-width: 0; min-height: 34px; padding: 5px; border: 1px solid var(--panel-border); border-radius: 6px; background: var(--bg); color: var(--text); font-size: 12px; }
    .erd-related-empty { padding: 16px; font-size: 13px; color: var(--muted); }
    .erd-related-graph.is-narrow { grid-template-columns: minmax(0, 1fr) 48px minmax(0, 1fr); padding: 16px; }
    .erd-related-graph.is-narrow .erd-related-wing--left { display: none; }
    .erd-related-graph.is-narrow .erd-related-wing--right { grid-column: 3; }
    .erd-related-graph.is-narrow .erd-related-center { grid-column: 1; padding: 10px; }
    .erd-related-graph.is-narrow .erd-related-center h3 { font-size: 16px; }
    .erd-tool--zoom { justify-content: center; min-width: 34px; }
    .erd-layout-options { flex: 1 0 100%; min-width: 0; }
    .erd-layout-options summary { width: fit-content; cursor: pointer; color: var(--muted); padding: 6px 2px; font-size: 12px; }
    .erd-layout-options__body { display: grid; gap: 10px; max-height: 28dvh; overflow: auto; padding: 8px 3px 3px; }
    .erd-connection-context {
      display: grid; gap: 6px; padding: 10px 14px;
      border-bottom: 1px solid rgba(255, 191, 105, 0.4); background: rgba(255, 191, 105, 0.06);
      max-height: 28dvh; overflow: auto; min-width: 0;
    }
    .erd-connection-context__flow, .erd-connection-context__actions { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; min-width: 0; }
    .erd-connection-context__eyebrow { font-size: 11px; color: var(--accent-2); margin-right: 6px; }
    .erd-connection-context__model {
      max-width: 100%; min-height: 32px; padding: 4px 7px; font-size: 13px; font-weight: 600;
      color: var(--text); background: var(--table); border: 1px solid var(--panel-border);
      border-radius: 6px; cursor: pointer; overflow-wrap: anywhere;
    }
    .erd-connection-context__model:hover { border-color: var(--accent); }
    .erd-connection-context__field { font-size: 12px; color: var(--accent-2); overflow-wrap: anywhere; }
    .erd-connection-context__actions .erd-panel__hint { flex: 1 1 240px; font-size: 12px; }
    .erd-connection-context__actions .erd-tool { min-height: 32px; padding: 6px 9px; border-radius: 7px; font-size: 12px; }
    .erd-connection-context .erd-badge { text-transform: none; letter-spacing: 0; }

    .erd-search {
      align-items: center;
      flex: 1 1 230px;
      min-width: 0;
    }

    .erd-search__input {
      border-radius: 8px;
      border: 1px solid rgba(122, 163, 177, 0.22);
      background: rgba(16, 31, 44, 0.9);
      color: var(--text);
      padding: 10px 12px;
      min-width: 0;
      width: 100%;
      flex: 1 1 190px;
      font-size: 13px;
      font: inherit;
    }

    .erd-search__input::placeholder {
      color: var(--muted);
    }

    .erd-search__input:focus {
      border-color: rgba(109, 208, 176, 0.44);
      background: rgba(22, 56, 67, 0.92);
    }

    .erd-search__count {
      color: var(--muted);
      font-size: 12px;
      min-width: 40px;
      text-align: left;
    }

    .erd-leaf-card-select {
      min-width: 0;
      max-width: min(290px, 100%);
      padding: 8px 12px;
      border: 1px solid var(--panel-border);
      border-radius: 14px;
      background: var(--panel);
      color: var(--text);
      font: inherit;
      text-overflow: ellipsis;
    }

    [data-leaf-cards-toggle]:focus-visible,
    .erd-leaf-card-select:focus-visible {
      outline: 2px solid var(--accent);
      outline-offset: 3px;
    }

    .erd-visually-hidden {
      position: absolute;
      width: 1px;
      height: 1px;
      padding: 0;
      overflow: hidden;
      clip-path: inset(50%);
      white-space: nowrap;
    }

    .erd-canvas {
      grid-row: 3;
      overflow: hidden;
      cursor: grab;
      position: relative;
      border-radius: 24px;
      margin: 12px;
      min-height: 0;
      background:
        linear-gradient(var(--grid) 1px, transparent 1px),
        linear-gradient(90deg, var(--grid) 1px, transparent 1px),
        linear-gradient(180deg, rgba(17, 35, 50, 0.35), rgba(8, 16, 24, 0.18));
      background-size: 32px 32px, 32px 32px, auto;
      touch-action: none;
    }

    .erd-canvas.is-panning { cursor: grabbing; }
    .erd-canvas.is-dragging-table { cursor: default; }
    .erd-connection-anchors { position: absolute; inset: 0; pointer-events: none; z-index: 3; }
    .erd-connection-anchor {
      position: absolute; max-width: min(240px, calc(100% - 16px)); min-height: 34px;
      padding: 6px 10px; border: 1px solid var(--accent); border-radius: 6px;
      background: var(--bg); color: var(--text); box-shadow: 0 3px 10px rgba(0, 0, 0, 0.4);
      font-size: 13px; text-align: left; overflow-wrap: anywhere; pointer-events: auto; cursor: pointer;
    }
    .erd-connection-anchor span { color: var(--muted); font-size: 11px; margin-right: 5px; }
    .erd-connection-anchor.is-origin { border-color: var(--accent-2); }
    .erd-connection-anchor:hover { background: var(--table-header); }
    .erd-scene { width: 100%; height: 100%; display: block; }
    .erd-gpu-warning {
      position: absolute;
      inset: 18px;
      display: grid;
      align-content: center;
      justify-items: start;
      gap: 10px;
      padding: 28px;
      border-radius: 22px;
      border: 1px solid rgba(255, 124, 112, 0.24);
      background: rgba(7, 18, 28, 0.92);
      box-shadow: 0 24px 64px rgba(0, 0, 0, 0.4);
      z-index: 3;
    }
    .erd-gpu-warning__eyebrow {
      margin: 0;
      color: var(--accent-2);
      font-size: 12px;
      letter-spacing: 0.16em;
      text-transform: uppercase;
    }
    .erd-gpu-warning__title {
      margin: 0;
      font-size: 28px;
      line-height: 1.15;
    }
    .erd-gpu-warning__body {
      margin: 0;
      max-width: 56ch;
      color: var(--muted);
      font-size: 14px;
      line-height: 1.6;
    }
    .erd-minimap {
      position: absolute;
      top: 16px;
      right: 16px;
      width: 220px;
      height: 148px;
      padding: 8px;
      border-radius: 18px;
      border: 1px solid rgba(129, 169, 181, 0.28);
      background: rgba(7, 18, 28, 0.88);
      box-shadow: 0 18px 42px rgba(0, 0, 0, 0.34);
      backdrop-filter: blur(10px);
      touch-action: none;
      z-index: 2;
    }
    .erd-minimap__canvas {
      display: block;
      width: 100%;
      height: 100%;
      border-radius: 12px;
    }
    .erd-minimap__viewport {
      position: absolute;
      inset: 8px auto auto 8px;
      border-radius: 10px;
      border: 2px solid rgba(255, 191, 105, 0.94);
      box-shadow: 0 0 0 1px rgba(7, 18, 28, 0.82);
      background: rgba(255, 191, 105, 0.12);
      pointer-events: none;
      min-width: 10px;
      min-height: 10px;
    }
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
    .erd-edge--inheritance { stroke: #c8b6ff; stroke-width: 2.6; }

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

    @media (max-width: 980px) {
      .erd-shell { grid-template-columns: 280px minmax(0, 1fr); gap: 8px; padding: 8px; }
      .erd-sidebar { padding: 8px; }
      .erd-panel { padding: 10px; }
      .erd-minimap { width: 150px; height: 104px; right: 10px; top: 10px; }
    }

    @media (max-width: 680px) {
      .erd-shell { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 34dvh) minmax(0, 1fr); }
      .erd-sidebar, .erd-stage { border-radius: 14px; }
      .erd-sidebar__tabs { top: 0; }
      .erd-sidebar__tab { padding: 5px 8px; }
      .erd-panel__header { position: static; }
      .erd-canvas { margin: 6px; border-radius: 12px; }
      .erd-stage__toolbar { padding: 8px; gap: 5px; }
      .erd-layout-options summary { padding: 3px 2px; }
      .erd-minimap { width: 110px; height: 78px; top: 6px; right: 6px; padding: 5px; }
      .erd-connection-context { max-height: 20dvh; padding: 6px 8px; }
      .erd-connection-context__eyebrow { display: none; }
      .erd-connection-context__actions .erd-panel__hint { flex-basis: 100%; }
    }

    @media (prefers-reduced-motion: reduce) {
      *, *::before, *::after { transition: none !important; animation: none !important; }
    }
  `;
}
