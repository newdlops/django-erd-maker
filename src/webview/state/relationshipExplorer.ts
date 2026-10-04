export interface ExplorerRelationship {
  edgeId: string;
  fieldName: string;
  kind: string;
  direction: string;
  otherModelId: string;
  sourceModelId: string;
  targetModelId: string;
}

export interface ExplorerModel {
  modelId: string;
  modelName: string;
  relationships: ExplorerRelationship[];
}

export interface ExplorerOptions {
  query?: string;
  filter?: string;
  limit?: number;
  revealedEdgeId?: string;
  canGoBack?: boolean;
  modelIds?: string[];
  groupLabel?: string;
}

/** Shared initial HTML and browser rendering; safe to serialize into the webview. */
export function createRelationshipExplorerTools() {
  function escape(value: unknown): string {
    return String(value ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
  }
  function name(id: string): string { return id.slice(id.indexOf('.') + 1); }
  function pair(a: string, b: string): string { return JSON.stringify([a, b].sort()); }
  function kindLabel(kind: string): string {
    switch (kind.replace('reverse_', '')) {
      case 'foreign_key': return 'Many → one';
      case 'one_to_one': return 'One ↔ one';
      case 'many_to_many': return 'Many ↔ many';
      case 'inheritance': return 'Inheritance';
      default: return 'Relationship';
    }
  }
  function matchesFilter(relationship: ExplorerRelationship, filter: string): boolean {
    if (filter === 'inheritance') return relationship.kind === 'inheritance';
    if (filter === 'outgoing' || filter === 'incoming') return relationship.direction === filter && relationship.kind !== 'inheritance';
    return true;
  }
  function results(model: ExplorerModel, options: ExplorerOptions = {}) {
    const query = (options.query || '').trim().toLocaleLowerCase();
    return (model.relationships || []).filter(r => matchesFilter(r, options.filter || 'all')
      && (!options.modelIds || options.modelIds.includes(r.otherModelId))
      && (!query || [r.otherModelId, r.fieldName, kindLabel(r.kind)].join(' ').toLocaleLowerCase().includes(query)))
      .sort((a, b) => a.otherModelId.localeCompare(b.otherModelId) || a.fieldName.localeCompare(b.fieldName) || a.edgeId.localeCompare(b.edgeId));
  }
  function renderResults(model: ExplorerModel, options: ExplorerOptions = {}): string {
    const relationships = results(model, options), limit = Math.max(1, options.limit ?? 40);
    if (!relationships.length) return '<p class="erd-panel__hint erd-connections__empty">'
      + (model.relationships.length ? 'No matching connections. Try another model or field name.' : 'This model has no declared relationships.')
      + '</p>';
    return '<ul class="erd-connections__list">' + relationships.slice(0, limit).map(r => {
      const selected = r.edgeId === options.revealedEdgeId;
      const direction = r.direction === 'self' ? 'Self-reference'
        : r.kind === 'inheritance' ? (r.direction === 'incoming' ? 'Inherited by' : 'Inherits from')
        : r.direction === 'incoming' ? 'Referenced by' : 'References';
      const tone = r.kind.replace('reverse_', '').replaceAll('_', '-');
      return '<li><button type="button" class="erd-connection-row' + (selected ? ' is-revealed' : '')
        + '" data-preview-relationship data-relationship-edge-id="' + escape(r.edgeId) + '" aria-pressed="' + selected
        + '" aria-label="' + escape('Preview: ' + direction + ' ' + r.otherModelId + ', field ' + r.fieldName) + '">'
        + '<span class="erd-connection-row__top"><strong>' + escape(name(r.otherModelId)) + '</strong>'
        + '<span class="erd-connection-row__direction">' + escape(direction) + '</span></span>'
        + '<span class="erd-connection-row__field">' + escape(r.fieldName) + '</span>'
        + '<span class="erd-connection-row__meta"><span>' + escape(r.otherModelId.split('.')[0]) + '</span>'
        + '<span class="erd-badge erd-badge--relation-' + escape(tone) + '">' + escape(kindLabel(r.kind)) + '</span></span>'
        + '</button></li>';
    }).join('') + '</ul>' + (relationships.length > limit
      ? '<button type="button" class="erd-tool erd-connections__more" data-connections-more>Show next '
        + Math.min(40, relationships.length - limit) + ' · ' + (relationships.length - limit) + ' remaining</button>' : '');
  }
  function render(model: ExplorerModel, options: ExplorerOptions = {}): string {
    const filters = [['all', 'All'], ['outgoing', 'References'], ['incoming', 'Referenced by'], ['inheritance', 'Inheritance']];
    return '<section class="erd-connections" aria-label="Model connections">'
      + '<div class="erd-connections__heading"><h3>Connections <span>' + model.relationships.length + '</span></h3>'
      + '<button type="button" class="erd-tool" data-model-back ' + (options.canGoBack ? '' : 'disabled') + '>← Back</button></div>'
      + '<button type="button" class="erd-tool erd-related-open" data-related-diagram-open>Related diagram</button>'
      + '<p class="erd-panel__hint">Gather connected models into a compact diagram. Select a connection to inspect its fields.</p>'
      + (options.groupLabel ? '<div class="erd-connections__group"><span>' + escape(options.groupLabel) + '</span><button type="button" class="erd-tool" data-related-clear>All connections</button></div>' : '')
      + '<label class="erd-connections__search"><span class="erd-visually-hidden">Filter connections by model or field</span>'
      + '<input type="search" name="connection-query" data-connections-search placeholder="Find a model or field…" value="' + escape(options.query || '') + '"'
      + ' autocomplete="off" spellcheck="false" aria-controls="erd-connection-results" /></label>'
      + '<div class="erd-connections__filters" role="group" aria-label="Connection direction">'
      + filters.map(([id, label]) => '<button type="button" data-connections-filter="' + id + '" aria-pressed="'
        + ((options.filter || 'all') === id) + '">' + label + ' <span>'
        + model.relationships.filter(r => matchesFilter(r, id)).length + '</span></button>').join('') + '</div>'
      + '<p class="erd-connections__count" data-connections-count role="status">' + results(model, options).length + ' connections</p>'
      + '<div id="erd-connection-results" data-connection-results>' + renderResults(model, options) + '</div></section>';
  }
  return {escape, name, pair, kindLabel, matchesFilter, results, renderResults, render};
}
