export function renderCircularDiagram(): string {
  return `
    <section id="erd-circular-diagram" class="erd-circular-diagram" data-circular-diagram aria-label="All models circular diagram" hidden>
      <header class="erd-circular-header">
        <div><h2>Circular view</h2><p class="erd-panel__hint" data-circular-count></p></div>
        <button type="button" class="erd-tool" data-circular-close>Full diagram</button>
      </header>
      <div class="erd-circular-controls">
        <label class="erd-circular-picker">Model <select data-circular-model name="circular-model" autocomplete="off" translate="no"><option value="">All models</option></select></label>
        <div class="erd-toolbar-group" role="group" aria-label="Circular view controls">
          <button type="button" class="erd-tool" data-circular-zoom="in" aria-label="Zoom in circular view">+</button>
          <button type="button" class="erd-tool" data-circular-zoom="out" aria-label="Zoom out circular view">−</button>
          <button type="button" class="erd-tool" data-circular-fit>Fit</button>
          <button type="button" class="erd-tool" data-circular-clear>Clear selection</button>
        </div>
      </div>
      <div class="erd-circular-plot" data-circular-plot tabindex="0" role="group" aria-label="Circular model graph" aria-describedby="erd-circular-help">
        <canvas data-circular-canvas aria-hidden="true"></canvas>
        <div class="erd-circular-center" data-circular-center></div>
        <p class="erd-circular-empty" data-circular-empty hidden>No models to display. Open a Django workspace and refresh the diagram.</p>
        <p class="erd-circular-empty" data-circular-error role="alert" hidden>The circular canvas is unavailable. Return to Full diagram to inspect your models.</p>
      </div>
      <footer class="erd-circular-footer">
        <p data-circular-status role="status" aria-live="polite"></p>
        <details><summary>Navigation help</summary><p id="erd-circular-help">Grouped by app · Bars show connection counts. Drag to pan; scroll or +/− to zoom. Left/Right selects models; Shift + arrows pans. Select a curve to inspect its field.</p></details>
      </footer>
    </section>`;
}
