import { createRelationshipExplorerTools } from "../../state/relationshipExplorer";

export function getBrowserRelationshipSource(): string {
  return `
        const relationshipExplorer = (${createRelationshipExplorerTools.toString()})();
        let connectionQuery = "";
        let connectionFilter = "all";
        let connectionLimit = 40;
        let connectionOverviewViewport = null;
        let connectionOverviewCollapse = false;
        const modelNavigationHistory = [];

        function connectionOptions() {
          const peer = typeof selectedRelatedDiagramPeer === 'function' ? selectedRelatedDiagramPeer() : undefined;
          return {query: connectionQuery, filter: connectionFilter, limit: connectionLimit,
            modelIds: peer?.modelIds, groupLabel: peer?.label,
            revealedEdgeId: revealedRelationshipEdgeId, canGoBack: modelNavigationHistory.length > 0 || (typeof isRelatedDiagramOpen === 'function' && isRelatedDiagramOpen())};
        }

        function getPreviewRelationship() {
          return revealedRelationshipEdgeId ? relationshipByEdgeId.get(revealedRelationshipEdgeId) : undefined;
        }

        function isConnectionEndpoint(modelId) {
          const relationship = getPreviewRelationship();
          return Boolean(relationship && (relationship.sourceModelId === modelId || relationship.targetModelId === modelId));
        }

        function rememberModelNavigation(action) {
          if (action.type === 'reset-view' && typeof closeRelatedDiagram === 'function') closeRelatedDiagram();
          if (action.type === "clear-selection") {
            if (connectionOverviewViewport) {
              state.viewport = {...connectionOverviewViewport};
              state.collapseClusters = connectionOverviewCollapse;
            }
            revealedRelationshipEdgeId = "";
            connectionOverviewViewport = null;
            return;
          }
          if (action.type === "reset-view") {
            modelNavigationHistory.length = 0;
            revealedRelationshipEdgeId = "";
            connectionOverviewViewport = null;
            connectionQuery = ""; connectionFilter = "all"; connectionLimit = 40;
            return;
          }
          if (!["select-model", "focus-model"].includes(action.type) || !action.modelId || action.modelId === state.selectedModelId) return;
          if (state.selectedModelId) {
            modelNavigationHistory.push({modelId: state.selectedModelId, viewport: {...state.viewport},
              revealedEdgeId: revealedRelationshipEdgeId, query: connectionQuery, filter: connectionFilter, limit: connectionLimit,
              relatedDiagram: typeof relatedDiagramNavigation === 'function' ? relatedDiagramNavigation() : undefined,
              sidebarScroll: document.querySelector('[data-sidebar-sheet="model"]')?.scrollTop || 0,
              collapseClusters: state.collapseClusters, overviewViewport: connectionOverviewViewport, overviewCollapse: connectionOverviewCollapse});
            if (modelNavigationHistory.length > 50) {
              modelNavigationHistory.shift();
              if (typeof isRelatedDiagramOpen === 'function' && isRelatedDiagramOpen() && relatedDiagramOrigin.historyLength > 0) relatedDiagramOrigin.historyLength--;
            }
          }
          if (connectionOverviewViewport) state.collapseClusters = connectionOverviewCollapse;
          revealedRelationshipEdgeId = "";
          connectionOverviewViewport = null;
          connectionQuery = ""; connectionFilter = "all"; connectionLimit = 40;
          if (typeof restoreRelatedDiagramNavigation === 'function') restoreRelatedDiagramNavigation();
          const sidebar = document.querySelector('[data-sidebar-sheet="model"]');
          if (sidebar) sidebar.scrollTop = 0;
        }

        function goBackToModel() {
          if (typeof isRelatedDiagramOpen === 'function' && isRelatedDiagramOpen() && modelNavigationHistory.length <= relatedDiagramOrigin.historyLength) {
            closeRelatedDiagram(); return;
          }
          let previous;
          while (modelNavigationHistory.length && !previous) {
            const entry = modelNavigationHistory.pop();
            if (tableMetaById.has(entry.modelId) || (typeof isRelatedDiagramOpen === 'function' && isRelatedDiagramOpen() && inspectorModelById.has(entry.modelId))) previous = entry;
          }
          if (!previous) return;
          state = reduceState(state, {type: "select-model", modelId: previous.modelId});
          state.viewport = {...previous.viewport};
          state.collapseClusters = previous.collapseClusters;
          revealedRelationshipEdgeId = previous.revealedEdgeId;
          connectionOverviewViewport = previous.overviewViewport;
          connectionOverviewCollapse = previous.overviewCollapse;
          connectionQuery = previous.query; connectionFilter = previous.filter; connectionLimit = previous.limit;
          if (typeof restoreRelatedDiagramNavigation === 'function') restoreRelatedDiagramNavigation(previous.relatedDiagram);
          setSidebarSheet("model", false);
          invalidateSceneGraph(); applyState();
          const sidebar = document.querySelector('[data-sidebar-sheet="model"]');
          if (sidebar) sidebar.scrollTop = previous.sidebarScroll;
          focusConnectionControl(previous.revealedEdgeId);
        }

        function focusConnectionControl(edgeId) {
          const row = Array.from(document.querySelectorAll("[data-preview-relationship]")).find(button => button.dataset.relationshipEdgeId === edgeId);
          (row || document.querySelector("[data-connections-search]"))?.focus({preventScroll: true});
        }

        function buildConnectionPreviewRoutes(scene) {
          const relationship = getPreviewRelationship();
          if (!relationship) return undefined;
          const sourceId = relationship.sourceModelId, targetId = relationship.targetModelId;
          const sourceTable = tableMetaById.get(sourceId), targetTable = tableMetaById.get(targetId);
          const cards = scene.leafBundles.filter(card => card.kind === "leaf-card");
          scene.relationshipPreview = {status: "connection", grouped: cards.some(card => card.memberModelIds.includes(sourceId) || card.memberModelIds.includes(targetId))};
          if (!sourceTable || !targetTable) {scene.relationshipPreview.status = "missing"; return [];}
          if (!isVisibleModel(sourceId) || !isVisibleModel(targetId)) {scene.relationshipPreview.status = "hidden"; return [];}
          if (sourceId === targetId) {scene.relationshipPreview.status = "self"; return [];}
          const canonical = (individualEdgeMeta.length ? individualEdgeMeta : edgeMeta).find(edge =>
            relationshipExplorer.pair(edge.sourceModelId, edge.targetModelId) === relationshipExplorer.pair(sourceId, targetId));
          let points = canonical ? canonical.points : "";
          if (canonical && canonical.sourceModelId !== sourceId) points = points.trim().split(/\\s+/).reverse().join(" ");
          const meta = {...(canonical || {}), edgeId: relationship.edgeId, sourceModelId: sourceId, targetModelId: targetId,
            points, memberEdgeIds: [relationship.edgeId], logicalEndpointModelIds: [sourceId, targetId],
            physicalEndpointModelIds: [sourceId, targetId], leafCardEndpointIds: undefined,
            carrierRole: "direct", carrierFamily: relationship.kind === "inheritance" ? "inheritance" : "association",
            cssKind: relationship.kind.replaceAll("_", "-"), provenance: "declared", crossingIds: [],
            markerStartId: "", markerEndId: "", preserveRouteEndpoints: true};
          const paths = getStaticOrCatalogEdgePaths([{meta, sourceTable, targetTable,
            sourcePosition: getCurrentPosition(sourceId), targetPosition: getCurrentPosition(targetId)}]);
          const direct = paths.map(route => ({...route.meta, points: route.points.map(point => point.x + "," + point.y).join(" ")}));
          const projected = leafCardConnectionTools.project(direct, direct, cards);
          if (projected.internalEdgeIds.length) scene.relationshipPreview.status = "internal";
          const routes = projected.edges.map(edge => ({edgeId: edge.edgeId, meta: edge, points: parseEdgePoints(edge.points)}));
          if (routes.length) scene.relationshipPreview.endpoints = [routes[0].points[0], routes[0].points.at(-1)];
          return routes;
        }

        function fitConnectionPreview() {
          const relationship = getPreviewRelationship();
          if (!relationship) return;
          const scene = ensureSceneGraph(), ids = [relationship.sourceModelId, relationship.targetModelId];
          const boxes = ids.map(id => scene.leafBundles.find(card => card.kind === "leaf-card" && card.memberModelIds.includes(id))
            || scene.tablesById.get(id)).filter(Boolean);
          if (!boxes.length) return;
          const left = Math.min(...boxes.map(box => box.x)), top = Math.min(...boxes.map(box => box.y));
          const right = Math.max(...boxes.map(box => box.x + box.width)), bottom = Math.max(...boxes.map(box => box.y + box.height));
          const rect = getViewportScreenRect();
          const zoom = clampZoom(Math.min(1.2, Math.max(80, rect.width - 112) / Math.max(1, right - left),
            Math.max(80, rect.height - 112) / Math.max(1, bottom - top)));
          dispatch({type: "set-viewport-zoom", zoom, panX: rect.width / 2 - (left + right) / 2 * zoom,
            panY: rect.height / 2 - (top + bottom) / 2 * zoom});
        }

        function previewRelationship(edgeId) {
          if (!relationshipByEdgeId.has(edgeId)) return;
          if (typeof isCircularDiagramOpen === 'function' && isCircularDiagramOpen()) {selectCircularEdge(edgeId); return;}
          if (!revealedRelationshipEdgeId) {
            connectionOverviewViewport = {...state.viewport};
            connectionOverviewCollapse = state.collapseClusters;
          }
          revealedRelationshipEdgeId = edgeId;
          state.collapseClusters = false;
          invalidateSceneGraph(); applyState(); fitConnectionPreview();
        }

        function clearConnectionPreview() {
          if (typeof isCircularDiagramOpen === 'function' && isCircularDiagramOpen()) {revealedRelationshipEdgeId = ''; applyState(); return;}
          const previousEdgeId = revealedRelationshipEdgeId;
          revealedRelationshipEdgeId = "";
          if (connectionOverviewViewport) {
            state.viewport = {...connectionOverviewViewport};
            state.collapseClusters = connectionOverviewCollapse;
          }
          connectionOverviewViewport = null;
          invalidateSceneGraph(); applyState();
          focusConnectionControl(previousEdgeId);
        }

        function renderConnectionContext() {
          const host = document.querySelector("[data-connection-context]");
          if (!host) return;
          const relationship = getPreviewRelationship();
          const active = document.activeElement;
          const focusedAction = active && host.contains(active)
            ? Array.from(active.attributes).find(attribute => attribute.name.startsWith("data-")) : null;
          host.hidden = !relationship;
          if (!relationship) {host.replaceChildren(); return;}
          const preview = ensureSceneGraph().relationshipPreview || {};
          const escape = relationshipExplorer.escape;
          const modelButton = id => '<button type="button" class="erd-connection-context__model" data-go-connection-model="'
            + escape(id) + '" title="' + escape('Go to ' + id) + '" ' + (tableMetaById.has(id) ? '' : 'disabled') + '>' + escape(relationshipExplorer.name(id)) + '</button>';
          const hint = preview.status === "self" ? "This field refers to the same model."
            : preview.status === "internal" ? "Both models are inside the same leaf card; their cards are highlighted."
            : preview.status === "hidden" ? "A model in this connection is hidden. Show it to trace the line."
            : preview.status === "missing" ? "A model in this connection is unavailable in this layout."
            : preview.grouped ? "One connection to the leaf card boundary. Other connections are temporarily hidden."
            : "Showing one connection. Other connections are temporarily hidden.";
          host.innerHTML = '<div class="erd-connection-context__flow"><span class="erd-connection-context__eyebrow">Connection</span>'
            + modelButton(relationship.sourceModelId) + '<span class="erd-connection-context__field">.' + escape(relationship.fieldName) + '</span>'
            + '<span aria-hidden="true">→</span>' + modelButton(relationship.targetModelId)
            + '<span class="erd-badge">' + escape(relationshipExplorer.kindLabel(relationship.kind)) + '</span></div>'
            + '<div class="erd-connection-context__actions"><span class="erd-panel__hint" role="status">' + hint + '</span>'
            + (preview.status === "hidden" ? '<button type="button" class="erd-tool" data-connection-show-hidden>Show hidden models</button>' : '')
            + '<button type="button" class="erd-tool" data-connection-fit ' + (["missing", "hidden"].includes(preview.status) ? 'disabled' : '') + '>Show both</button>'
            + '<button type="button" class="erd-tool" data-connection-clear>Back to overview</button></div>';
          if (focusedAction) Array.from(host.querySelectorAll("button")).find(button => button.getAttribute(focusedAction.name) === focusedAction.value)?.focus({preventScroll: true});
        }

        function renderConnectionAnchors() {
          const host = document.querySelector("[data-connection-anchors]");
          if (!host) return;
          const relationship = getPreviewRelationship();
          host.hidden = !relationship;
          if (!relationship) {host.replaceChildren(); return;}
          const scene = ensureSceneGraph(), preview = scene.relationshipPreview || {}, rect = getViewportScreenRect();
          if (["hidden", "missing"].includes(preview.status)) {host.replaceChildren(); return;}
          const ids = [...new Set([relationship.sourceModelId, relationship.targetModelId])];
          const key = relationship.edgeId;
          if (host.dataset.edgeId !== key || host.children.length !== ids.length) {
            host.dataset.edgeId = key;
            host.innerHTML = ids.map((id, index) => '<button type="button" class="erd-connection-anchor" data-go-connection-model="'
              + relationshipExplorer.escape(id) + '" title="' + relationshipExplorer.escape('Go to ' + id) + '"><span>'
              + (ids.length === 1 ? 'Self' : index === 0 ? 'From' : 'To') + '</span> '
              + relationshipExplorer.escape(relationshipExplorer.name(id)) + '</button>').join('');
          }
          let previous;
          ids.forEach((id, index) => {
            const table = scene.tablesById.get(id);
            const point = preview.endpoints?.[index] || (table ? {x: table.x, y: table.y} : getCurrentPosition(id));
            if (!point) return;
            const button = host.children[index];
            const screenX = point.x * state.viewport.zoom + state.viewport.panX;
            const screenY = point.y * state.viewport.zoom + state.viewport.panY;
            button.hidden = screenX < 0 || screenX > rect.width || screenY < 0 || screenY > rect.height;
            const otherPoint = preview.endpoints?.[index === 0 ? 1 : 0];
            const outsideX = otherPoint && point.x < otherPoint.x ? screenX - 248 : screenX + 12;
            const x = Math.max(8, Math.min(rect.width - 248, outsideX));
            let y = Math.max(8, Math.min(rect.height - 72, screenY - 48));
            if (previous && Math.abs(x - previous.x) < 248 && Math.abs(y - previous.y) < 60) y = Math.min(rect.height - 72, screenY + 16);
            button.style.left = x + 'px'; button.style.top = y + 'px';
            button.classList.toggle('is-origin', id === state.selectedModelId);
            previous = {x, y};
          });
        }

        function updateConnectionResults() {
          if (typeof isRelatedDiagramOpen === 'function' && isRelatedDiagramOpen()) {
            relatedDiagramPage = 0; relatedDiagramPeerKey = ''; revealedRelationshipEdgeId = '';
            applyState(); return;
          }
          const model = inspectorModelById.get(state.selectedModelId), results = document.querySelector("[data-connection-results]");
          if (!model || !results) return;
          results.innerHTML = relationshipExplorer.renderResults(model, connectionOptions());
          const count = document.querySelector("[data-connections-count]");
          if (count) count.textContent = relationshipExplorer.results(model, connectionOptions()).length + " connections";
        }

        function handleConnectionAction(button) {
          if (typeof handleRelatedDiagramAction === 'function' && handleRelatedDiagramAction(button)) return true;
          if (button.matches("[data-preview-relationship]")) {
            if (typeof isCircularDiagramOpen === 'function' && isCircularDiagramOpen()) {selectCircularEdge(button.dataset.relationshipEdgeId); return true;}
            if (typeof openRelatedDiagram === 'function') openRelatedDiagram(button.dataset.relationshipEdgeId);
            else previewRelationship(button.dataset.relationshipEdgeId);
            return true;
          }
          if (button.matches("[data-connections-filter]")) {
            connectionFilter = button.dataset.connectionsFilter; connectionLimit = 40;
            for (const filter of document.querySelectorAll("[data-connections-filter]")) filter.setAttribute("aria-pressed", String(filter.dataset.connectionsFilter === connectionFilter));
            updateConnectionResults(); return true;
          }
          if (button.matches("[data-connections-more]")) {
            connectionLimit += 40;
            if (typeof isRelatedDiagramOpen === 'function' && isRelatedDiagramOpen()) renderPanels();
            else updateConnectionResults();
            const rows = Array.from(document.querySelectorAll("[data-preview-relationship]"));
            rows[Math.max(0, connectionLimit - 40)]?.focus({preventScroll: true}); return true;
          }
          if (button.matches("[data-model-back]")) {goBackToModel(); return true;}
          if (button.matches("[data-connection-clear]")) {clearConnectionPreview(); return true;}
          if (button.matches("[data-connection-fit]")) {fitConnectionPreview(); return true;}
          if (button.matches("[data-go-connection-model]")) {
            const modelId = button.dataset.goConnectionModel;
            if (tableMetaById.has(modelId)) {
              if (modelId === state.selectedModelId) clearConnectionPreview();
              dispatch({type: "focus-model", modelId, zoom: 1});
              focusConnectionControl();
            }
            return true;
          }
          if (button.matches("[data-connection-show-hidden]")) {
            const relationship = getPreviewRelationship();
            if (relationship) {
              for (const modelId of [relationship.sourceModelId, relationship.targetModelId]) state = reduceState(state, {type: "set-table-hidden", modelId, hidden: false});
              invalidateSceneGraph(); applyState(); fitConnectionPreview();
            }
            return true;
          }
          return false;
        }
  `;
}
