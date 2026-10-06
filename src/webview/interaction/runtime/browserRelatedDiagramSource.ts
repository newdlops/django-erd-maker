import { createRelatedDiagramTools } from "../../state/relatedDiagram";

export function getBrowserRelatedDiagramSource(): string {
  return `
        const relatedDiagramTools = (${createRelatedDiagramTools.toString()})();
        let relatedDiagramOrigin = null;
        let relatedDiagramPage = 0;
        let relatedDiagramPeerKey = "";
        let relatedDiagramGraph = null;
        let relatedDiagramRenderKey = "";
        function isRelatedDiagramOpen() { return Boolean(relatedDiagramOrigin); }
        function relatedDiagramNavigation() { return {page: relatedDiagramPage, peerKey: relatedDiagramPeerKey}; }
        function restoreRelatedDiagramNavigation(value) {
          relatedDiagramPage = value?.page || 0; relatedDiagramPeerKey = value?.peerKey || "";
          relatedDiagramGraph = null; relatedDiagramRenderKey = "";
        }
        function selectedRelatedDiagramPeer() {
          if (!isRelatedDiagramOpen() || !relatedDiagramPeerKey) return undefined;
          if (relatedDiagramPeerKey === "self") return {modelIds: [state.selectedModelId], label: 'Self references'};
          return relatedDiagramGraph?.peers.find(peer => peer.key === relatedDiagramPeerKey);
        }
        function openRelatedDiagram(edgeId) {
          if (!inspectorModelById.has(state.selectedModelId)) return;
          if (typeof isCircularDiagramOpen === 'function' && isCircularDiagramOpen()) closeCircularDiagram();
          const wasOpen = isRelatedDiagramOpen();
          if (!relatedDiagramOrigin) {
            relatedDiagramOrigin = {modelId: state.selectedModelId, viewport: {...(connectionOverviewViewport || state.viewport)},
              collapse: connectionOverviewViewport ? connectionOverviewCollapse : state.collapseClusters,
              method: state.selectedMethodContext, query: connectionQuery, filter: connectionFilter, limit: connectionLimit,
              historyLength: modelNavigationHistory.length,
              sidebarScroll: document.querySelector('[data-sidebar-sheet="model"]')?.scrollTop || 0};
            connectionOverviewViewport = null;
            relatedDiagramRenderKey = "";
          }
          revealedRelationshipEdgeId = edgeId || "";
          if (!edgeId) relatedDiagramPeerKey = "";
          setSidebarSheet("model", false);
          applyState();
          if (!wasOpen && !edgeId) document.querySelector('[data-related-diagram] h2')?.focus({preventScroll: true});
        }
        function closeRelatedDiagram() {
          const previous = relatedDiagramOrigin;
          if (!previous) return;
          relatedDiagramOrigin = null; relatedDiagramGraph = null; relatedDiagramRenderKey = "";
          relatedDiagramPage = 0; relatedDiagramPeerKey = ""; revealedRelationshipEdgeId = "";
          state = reduceState(state, {type: 'select-model', modelId: previous.modelId});
          state.viewport = {...previous.viewport}; state.collapseClusters = previous.collapse;
          state.selectedMethodContext = previous.method;
          connectionQuery = previous.query; connectionFilter = previous.filter; connectionLimit = previous.limit;
          modelNavigationHistory.length = previous.historyLength;
          invalidateSceneGraph(); applyState();
          const sidebar = document.querySelector('[data-sidebar-sheet="model"]');
          if (sidebar) sidebar.scrollTop = previous.sidebarScroll;
          document.querySelector('[data-related-diagram-open]')?.focus({preventScroll: true});
        }

        function renderRelatedDiagram() {
          const host = document.querySelector('[data-related-diagram]');
          if (!host) return false;
          const active = isRelatedDiagramOpen();
          host.hidden = !active; canvas.hidden = active;
          document.querySelector('.erd-stage')?.classList.toggle('is-related-diagram', active);
          for (const button of document.querySelectorAll('[data-related-diagram-open]')) {
            button.disabled = !inspectorModelById.has(state.selectedModelId);
            button.setAttribute('aria-pressed', String(active));
          }
          if (!active) return false;
          const model = inspectorModelById.get(state.selectedModelId);
          if (!model) {closeRelatedDiagram(); return false;}
          const narrow = host.clientWidth < 760;
          const filtered = relationshipExplorer.results(model, {query: connectionQuery, filter: connectionFilter});
          relatedDiagramGraph = relatedDiagramTools.build(model, filtered, inspectorModelById, renderModel.leafCards || [],
            {page: relatedDiagramPage, pageSize: narrow ? 4 : 8, edgeId: revealedRelationshipEdgeId});
          const graph = relatedDiagramGraph;
          relatedDiagramPage = graph.page;
          if (revealedRelationshipEdgeId) relatedDiagramPeerKey = graph.self.some(r => r.edgeId === revealedRelationshipEdgeId) ? 'self'
            : graph.peers.find(peer => peer.relationships.some(r => r.edgeId === revealedRelationshipEdgeId))?.key || "";
          else if (relatedDiagramPeerKey !== 'self' && !graph.peers.some(peer => peer.key === relatedDiagramPeerKey)) relatedDiagramPeerKey = "";
          const renderKey = JSON.stringify([model.modelId, connectionQuery, connectionFilter, graph.page, narrow, relatedDiagramPeerKey, revealedRelationshipEdgeId]);
          if (renderKey === relatedDiagramRenderKey) {drawRelatedDiagramLines(); return true;}
          const changedModelOrPage = host.dataset.modelId !== model.modelId || host.dataset.page !== String(graph.page);
          const changedLayout = host.dataset.narrow !== String(narrow);
          const changedPeer = host.dataset.peerKey !== relatedDiagramPeerKey;
          const oldScroll = host.querySelector('[data-related-scroll]');
          const scroll = oldScroll ? {top: oldScroll.scrollTop, left: oldScroll.scrollLeft} : null;
          const focused = document.activeElement;
          const focusedAttribute = focused && host.contains(focused)
            ? Array.from(focused.attributes).find(a => a.name.startsWith('data-related-')) : null;
          relatedDiagramRenderKey = renderKey;
          host.dataset.modelId = model.modelId; host.dataset.page = String(graph.page);
          host.dataset.narrow = String(narrow); host.dataset.peerKey = relatedDiagramPeerKey;
          const esc = relationshipExplorer.escape;
          const count = (value, noun) => value + ' ' + noun + (value === 1 ? '' : 's');
          const peerMarkup = peer => '<article class="erd-related-card' + (peer.key === relatedDiagramPeerKey ? ' is-selected' : '')
            + '" data-related-card="' + esc(peer.key) + '">'
            + '<button type="button" class="erd-related-card__select" data-related-peer="' + esc(peer.key) + '" aria-pressed="' + (peer.key === relatedDiagramPeerKey) + '">'
            + '<span class="erd-related-card__direction">' + esc(relatedDiagramTools.direction(peer)) + '</span>'
            + '<strong>' + esc(peer.label) + '</strong>'
            + (peer.grouped ? '<span class="erd-related-card__members">' + peer.modelIds.slice().sort().slice(0, 2).map(id => esc(relationshipExplorer.name(id))).join(' · ')
              + (peer.modelIds.length > 2 ? ' · +' + (peer.modelIds.length - 2) + ' more' : '') + '</span>' : '')
            + '<span class="erd-related-card__meta">'
            + (peer.grouped ? count(peer.modelIds.length, 'related model') + ' · ' : esc(peer.modelIds[0].split('.')[0]) + ' · ')
            + count(peer.relationships.length, 'connection') + (peer.available ? '' : ' · unavailable') + '</span></button>'
            + (peer.grouped ? '<label class="erd-related-card__member"><span class="erd-visually-hidden">Explore a model in ' + esc(peer.label) + '</span>'
              + '<select data-related-member><option value="">Explore a member…</option>' + peer.modelIds.slice().sort().map(id => '<option value="' + esc(id) + '"' + (inspectorModelById.has(id) ? '' : ' disabled') + '>' + esc(relationshipExplorer.name(id)) + (inspectorModelById.has(id) ? '' : ' (unavailable)') + '</option>').join('') + '</select></label>'
              : '<button type="button" class="erd-related-card__follow" data-related-center="' + esc(peer.modelIds[0]) + '" ' + (peer.available ? '' : 'disabled') + '>Explore model →</button>') + '</article>';
          const left = narrow ? [] : graph.visiblePeers.filter((_, i) => i % 2 === 0);
          const right = narrow ? graph.visiblePeers : graph.visiblePeers.filter((_, i) => i % 2 === 1);
          const chosen = getPreviewRelationship();
          const choice = chosen ? esc(chosen.sourceModelId + '.' + chosen.fieldName) + ' → ' + esc(chosen.targetModelId) + ' · ' + esc(relationshipExplorer.kindLabel(chosen.kind))
            : selectedRelatedDiagramPeer() ? esc(selectedRelatedDiagramPeer().label) + ' · Connections are shown in the model panel.'
            : 'Direct connections only. Select a card to inspect its fields; explore a model to make it the center.';
          host.innerHTML = '<header class="erd-related-header"><div><h2 tabindex="-1">Related to ' + esc(model.modelName) + '</h2>'
            + '<p class="erd-panel__hint">' + count(graph.modelCount, 'model') + ' · ' + count(graph.relationshipCount, 'connection') + ' · ' + count(graph.peers.length, 'connected card') + '</p></div>'
            + '<div class="erd-related-actions"><button type="button" class="erd-tool" data-related-back>← Back</button>'
            + '<button type="button" class="erd-tool" data-related-close>Full diagram</button></div></header>'
            + '<div class="erd-related-status"><p role="status">' + choice + '</p>'
            + (relatedDiagramPeerKey ? '<button type="button" class="erd-tool" data-related-clear>All connections</button>' : '') + '</div>'
            + '<nav class="erd-related-pagination" aria-label="Related diagram pages"><span>'
            + (graph.peers.length ? 'Cards ' + (graph.page * graph.pageSize + 1) + '–' + Math.min(graph.peers.length, (graph.page + 1) * graph.pageSize) + ' of ' + graph.peers.length : 'No connected cards')
            + '</span><button type="button" class="erd-tool" data-related-page="-1" ' + (graph.page === 0 ? 'disabled' : '') + '>Previous</button>'
            + '<span>Page ' + (graph.page + 1) + ' / ' + graph.pageCount + '</span>'
            + '<button type="button" class="erd-tool" data-related-page="1" ' + (graph.page + 1 === graph.pageCount ? 'disabled' : '') + '>Next</button></nav>'
            + '<div class="erd-related-scroll" data-related-scroll tabindex="0" aria-label="Compact related diagram">'
            + (!filtered.length ? '<div class="erd-related-empty"><p>' + (model.relationships.length ? 'No connections match the current filters.' : 'This model has no declared connections.') + '</p>'
              + (model.relationships.length ? '<button type="button" class="erd-tool" data-related-reset-filter>Clear filters</button>' : '') + '</div>' : '')
            + '<div class="erd-related-graph' + (narrow ? ' is-narrow' : '') + '" data-related-graph>'
            + '<svg class="erd-related-lines" data-related-lines aria-hidden="true"></svg>'
            + '<div class="erd-related-wing erd-related-wing--left">' + left.map(peerMarkup).join('') + '</div>'
            + '<article class="erd-related-center" data-related-center-card><span>Selected model</span><h3 tabindex="-1">' + esc(model.modelName) + '</h3><p>' + esc(model.modelId) + '</p>'
            + '<button type="button" class="erd-related-card__follow" data-related-clear>All ' + count(graph.relationshipCount, 'connection') + '</button>'
            + (graph.self.length ? '<button type="button" class="erd-related-card__follow" data-related-peer="self">↻ ' + count(graph.self.length, 'self reference') + '</button>' : '')
            + '</article><div class="erd-related-wing erd-related-wing--right">' + right.map(peerMarkup).join('') + '</div></div></div>';
          drawRelatedDiagramLines();
          const scroller = host.querySelector('[data-related-scroll]');
          if (changedModelOrPage || !scroll) {
            const center = host.querySelector('[data-related-center-card]');
            scroller.scrollTop = Math.max(0, center.offsetTop - (scroller.clientHeight - center.offsetHeight) / 2);
          } else {scroller.scrollTop = scroll.top; scroller.scrollLeft = scroll.left;}
          if (relatedDiagramPeerKey && (changedModelOrPage || changedLayout || changedPeer)) {
            const selectedCard = relatedDiagramPeerKey === 'self' ? host.querySelector('[data-related-center-card]')
              : Array.from(host.querySelectorAll('[data-related-card]')).find(card => card.dataset.relatedCard === relatedDiagramPeerKey);
            if (selectedCard) {
              const top = Math.max(0, selectedCard.offsetTop - 16);
              const bottom = selectedCard.offsetTop + selectedCard.offsetHeight + 16;
              if (top < scroller.scrollTop || selectedCard.offsetHeight + 32 > scroller.clientHeight) scroller.scrollTop = top;
              else if (bottom > scroller.scrollTop + scroller.clientHeight) scroller.scrollTop = bottom - scroller.clientHeight;
            }
          }
          if (focusedAttribute) {
            const control = Array.from(host.querySelectorAll('button, select')).find(element => element.getAttribute(focusedAttribute.name) === focusedAttribute.value && !element.disabled);
            (control || host.querySelector('h2'))?.focus({preventScroll: true});
          }
          return true;
        }

        function drawRelatedDiagramLines() {
          const host = document.querySelector('[data-related-graph]');
          if (!host || !relatedDiagramGraph) return;
          const graphRect = host.getBoundingClientRect();
          const box = element => {const rect = element.getBoundingClientRect(); return {x: rect.left - graphRect.left, y: rect.top - graphRect.top, width: rect.width, height: rect.height};};
          const center = box(host.querySelector('[data-related-center-card]'));
          const boxes = new Map(Array.from(host.querySelectorAll('[data-related-card]')).map(element => [element.dataset.relatedCard, box(element)]));
          const links = relatedDiagramTools.links(relatedDiagramGraph.visiblePeers, boxes, center);
          const svg = host.querySelector('[data-related-lines]');
          svg.setAttribute('width', String(graphRect.width)); svg.setAttribute('height', String(graphRect.height));
          svg.innerHTML = '<defs><marker id="erd-related-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M1 1 L7 4 L1 7" fill="none" stroke="#e7f2f0" stroke-width="1.3" /></marker></defs>'
            + links.map(line => '<line x1="' + line.from.x + '" y1="' + line.from.y + '" x2="' + line.to.x + '" y2="' + line.to.y + '" stroke="' + line.color + '" stroke-width="' + (line.key === relatedDiagramPeerKey ? 3 : 1.6) + '" opacity="' + (!relatedDiagramPeerKey || line.key === relatedDiagramPeerKey ? 1 : 0.25) + '"'
              + (line.towardCenter ? ' marker-start="url(#erd-related-arrow)"' : '') + (line.towardPeer ? ' marker-end="url(#erd-related-arrow)"' : '') + ' />').join('');
        }

        function navigateRelatedDiagram(modelId) {
          if (!inspectorModelById.has(modelId)) return;
          dispatch({type: 'select-model', modelId});
          document.querySelector('[data-related-center-card] h3')?.focus({preventScroll: true});
        }
        function handleRelatedDiagramAction(button) {
          if (button.matches('[data-related-diagram-open]')) {openRelatedDiagram(); return true;}
          if (button.matches('[data-related-close]')) {closeRelatedDiagram(); return true;}
          if (button.matches('[data-related-back]')) {goBackToModel(); return true;}
          if (button.matches('[data-related-center]')) {navigateRelatedDiagram(button.dataset.relatedCenter); return true;}
          if (button.matches('[data-related-peer], [data-related-clear]')) {
            relatedDiagramPeerKey = button.dataset.relatedPeer || ''; revealedRelationshipEdgeId = '';
            applyState(); return true;
          }
          if (button.matches('[data-related-page]')) {
            relatedDiagramPage += Number(button.dataset.relatedPage); relatedDiagramPeerKey = ''; revealedRelationshipEdgeId = '';
            applyState(); return true;
          }
          if (button.matches('[data-related-reset-filter]')) {
            connectionQuery = ''; connectionFilter = 'all'; relatedDiagramPeerKey = ''; revealedRelationshipEdgeId = ''; relatedDiagramPage = 0;
            applyState(); return true;
          }
          return false;
        }
  `;
}
