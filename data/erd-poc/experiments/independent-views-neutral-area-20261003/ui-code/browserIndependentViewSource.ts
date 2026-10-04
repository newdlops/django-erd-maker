/** Switch complete saved geometry, retaining edits and viewport per view. */
export function getBrowserIndependentViewSource(): string {
  return `
        let independentViewActive = false;
        const independentEdgeMeta = (renderModel.individualView?.edges || []).map(readEdgeMeta);
        const overviewBasePositions = createPayloadLayout(tableMetaList);
        const overviewClusterOutlines = renderModel.clusterOutlines;
        let geometryViewHistory = {};

        function rememberGeometryView(currentState) {
          return {
            positions: Object.fromEntries(currentState.tableOptions
              .filter(options => options.manualPosition)
              .map(options => [options.modelId, {...options.manualPosition}])),
            viewport: {...currentState.viewport},
          };
        }

        function restoreGeometryView(currentState, saved) {
          return {
            ...currentState,
            tableOptions: currentState.tableOptions.map(options => ({...options,
              manualPosition: saved?.positions[options.modelId]
                ? {...saved.positions[options.modelId]} : undefined,
            })),
            viewport: saved ? {...saved.viewport} : {...currentState.viewport},
          };
        }

        function synchronizeIndependentView(forceOverview) {
          const next = !forceOverview && Boolean(renderModel.individualView && juneOverviewExpanded
            && (!leafCardsVisible || !(renderModel.leafCards || []).length));
          if (next === independentViewActive) return;
          geometryViewHistory[independentViewActive ? "individual" : "overview"] = rememberGeometryView(state);
          independentViewActive = next;
          const positions = next ? renderModel.individualView.positions : overviewBasePositions;
          for (const table of tableMetaList) {
            table.basePosition = {...positions[table.modelId]};
            const rendered = tableRenderById.get(table.modelId);
            if (rendered) rendered.position = {...table.basePosition};
          }
          const variants = createLayoutVariants(tableMetaList);
          for (const mode of layoutModes) layoutVariants[mode] = variants[mode];
          renderModel.clusterOutlines = next ? renderModel.individualView.clusterOutlines : overviewClusterOutlines;
          state = restoreGeometryView(state, geometryViewHistory[next ? "individual" : "overview"]);
          drag = null;
          selectedEdgeMeta = null;
          selectedEdgeModelId = "";
          invalidateSceneGraph();
        }

        function getOverviewRefreshState(currentState) {
          // A refreshed document opens the overview. Never feed its loader
          // absolute positions or viewport belonging to the individual view.
          return independentViewActive
            ? restoreGeometryView(currentState, geometryViewHistory.overview)
            : currentState;
        }

        function forgetIndependentViewHistory() {
          geometryViewHistory = {};
        }
  `;
}
