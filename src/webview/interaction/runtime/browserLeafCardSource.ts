import { createLeafCardConnectionTools } from "../../state/leafCardConnections";

export function getBrowserLeafCardSource(): string {
  return `
        let leafCardsVisible = true;
        let hoveredLeafCardId = "";
        const leafCardConnectionTools = (${createLeafCardConnectionTools.toString()})();

        function createLeafCardRecords(cards, tablesById, selectedModelId) {
          return leafCardConnectionTools.bounds(cards, tablesById, selectedModelId);
        }

        function projectLeafCardEdges(scene, routes) {
          const cards = scene.leafBundles.filter(card => card.kind === "leaf-card");
          scene.leafCardMemberIds = new Set(cards.flatMap(card => card.memberModelIds));
          if (!cards.length) return routes;
          const entries = [];
          for (const meta of individualEdgeMeta.length ? individualEdgeMeta : edgeMeta) {
            const sourceTable = tableMetaById.get(meta.sourceModelId), targetTable = tableMetaById.get(meta.targetModelId);
            if (!sourceTable || !targetTable || !isVisibleModel(meta.sourceModelId) || !isVisibleModel(meta.targetModelId)) continue;
            entries.push({meta, sourceTable, targetTable,
              sourcePosition: getCurrentPosition(meta.sourceModelId), targetPosition: getCurrentPosition(meta.targetModelId)});
          }
          const asMeta = route => ({...route.meta, points: route.points.map(point => point.x + "," + point.y).join(" ")});
          const currentById = new Map(routes.map(route => [route.edgeId, asMeta(route)]));
          const base = getActiveEdgeMeta().map(meta => currentById.get(meta.edgeId) || meta);
          const individual = getStaticOrCatalogEdgePaths(entries).map(asMeta);
          const projected = leafCardConnectionTools.project(base, individual, cards);
          scene.leafCardInternalEdgeIds = projected.internalEdgeIds;
          return projected.edges.map(meta => ({edgeId: meta.edgeId, meta, points: parseEdgePoints(meta.points)}));
        }

        function setLeafCardsVisible(visible) {
          leafCardsVisible = Boolean(visible);
          for (const button of document.querySelectorAll("[data-leaf-cards-toggle]")) {
            const enabled = leafCardsVisible && (renderModel.leafCards || []).length > 0;
            button.classList.toggle("is-active", enabled);
            button.setAttribute("aria-pressed", String(enabled));
          }
          hoveredLeafCardId = "";
          if (typeof synchronizeIndependentView === "function") synchronizeIndependentView();
          invalidateSceneGraph();
          if (typeof updateRelationshipReadouts === "function") updateRelationshipReadouts();
        }

        function updateLeafCardControls() {
          const cards = renderModel.leafCards || [];
          for (const button of document.querySelectorAll("[data-leaf-cards-toggle]")) {
            button.textContent = "Leaf cards (" + cards.length + ")";
            button.disabled = cards.length === 0;
          }
          for (const select of document.querySelectorAll("[data-leaf-card-select]")) {
            select.replaceChildren();
            const empty = document.createElement("option");
            empty.value = ""; empty.textContent = "Find leaf card";
            select.appendChild(empty);
            for (const card of cards.slice().sort((a, b) => b.memberModelIds.length - a.memberModelIds.length || a.label.localeCompare(b.label))) {
              const option = document.createElement("option");
              option.value = card.id;
              option.textContent = card.label + " · " + card.memberModelIds.length + " leaves";
              select.appendChild(option);
            }
            select.hidden = select.disabled = cards.length === 0;
          }
          setLeafCardsVisible(leafCardsVisible);
        }

        function findLeafCardAtCanvasPoint(event) {
          if (!leafCardsVisible || state.collapseClusters) return undefined;
          const point = toWorldPoint(event);
          return ensureSceneGraph().leafBundles.find((card) => card.kind === "leaf-card"
            && point.x >= card.x && point.x <= card.x + card.width
            && point.y >= card.y && point.y <= card.y + card.height);
        }

        function announceLeafCard(message) {
          const status = document.querySelector("[data-leaf-card-status]");
          if (status) status.textContent = message;
        }

        function focusLeafCard(id) {
          if (state.collapseClusters) {
            state.collapseClusters = false;
            for (const button of document.querySelectorAll("[data-cluster-collapse-toggle]")) {
              button.classList.remove("is-active");
            }
          }
          setLeafCardsVisible(true);
          const record = ensureSceneGraph().leafBundles.find((card) => card.id === id);
          if (!record) {
            applyState();
            announceLeafCard("This leaf card is hidden or its models are too far apart.");
            return;
          }
          dispatch({ type: "select-model", modelId: record.parentModelId });
          const rect = getViewportScreenRect();
          const zoom = clampZoom(Math.min((rect.width - 64) / record.width, (rect.height - 64) / record.height, 1.5));
          dispatch({ type: "set-viewport-zoom", zoom,
            panX: rect.width / 2 - (record.x + record.width / 2) * zoom,
            panY: rect.height / 2 - (record.y + record.height / 2) * zoom });
          announceLeafCard(record.parentName + ": " + record.leafCount + " leaf models. Drag its header to move the group. Shift and arrow keys move the selected group.");
        }

        function applyLeafCardMove(positions, dx, dy) {
          for (const [modelId, position] of Object.entries(positions)) {
            state = reduceState(state, { type: "set-table-manual-position", modelId,
              manualPosition: { x: round2(position.x + dx), y: round2(position.y + dy) } });
          }
          invalidateSceneGraph();
          applyState();
        }
  `;
}
