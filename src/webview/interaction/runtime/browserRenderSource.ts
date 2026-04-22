export function getBrowserRenderSource(): string {
  return `
        let viewportRenderFrame = 0;

        function applyState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          renderSummary();
          renderSetupControls();
          renderTables();
          renderEdgesAndCrossings();
          renderOverlays();
          renderPanels();
          renderHiddenTableList();
          drawCanvas("full");
        }

        function applyViewportState() {
          viewport.dataset.transform =
            "translate(" + state.viewport.panX + " " + state.viewport.panY + ") scale(" + state.viewport.zoom + ")";
          drawCanvas("viewport");
        }

        function cancelViewportRender() {
          if (!viewportRenderFrame) {
            return;
          }

          window.cancelAnimationFrame(viewportRenderFrame);
          viewportRenderFrame = 0;
        }

        function scheduleViewportRender() {
          if (viewportRenderFrame) {
            return;
          }

          viewportRenderFrame = window.requestAnimationFrame(() => {
            viewportRenderFrame = 0;
            applyViewportState();
          });
        }

        function dispatch(action) {
          state = reduceState(state, action);
          if (
            action.type === "set-viewport-pan" ||
            action.type === "set-viewport-zoom" ||
            action.type === "fit-viewport" ||
            action.type === "center-viewport"
          ) {
            scheduleViewportRender();
            return;
          }

          if (action.type === "set-interaction-setting") {
            renderSetupControls();
            return;
          }

          if (renderModel.modelCatalogMode && isCatalogSceneAction(action)) {
            invalidateCatalogSceneCache();
          }

          cancelViewportRender();
          applyState();
        }

        function isCatalogSceneAction(action) {
          switch (action.type) {
            case "reset-view":
            case "set-layout-mode":
            case "set-table-hidden":
            case "set-table-manual-position":
              return true;
            default:
              return false;
          }
        }

        function isVisibleModel(modelId) {
          return !getTableOptions(state, modelId).hidden;
        }

        function renderCrossingMarkup(crossing) {
          return '<div class="erd-crossing erd-crossing--bridge" data-crossing-id="' +
            crossing.id +
            '" data-x="' +
            crossing.position.x +
            '" data-y="' +
            crossing.position.y +
            '"></div>';
        }

        function renderEdgesAndCrossings() {
          const visibleEdges = [];
          renderedEdges = [];

          for (const meta of edgeMeta) {
            const sourceTable = tableMetaById.get(meta.sourceModelId);
            const targetTable = tableMetaById.get(meta.targetModelId);
            if (!sourceTable || !targetTable) {
              continue;
            }

            const sourceHidden = !isVisibleModel(meta.sourceModelId);
            const targetHidden = !isVisibleModel(meta.targetModelId);
            meta.element.toggleAttribute("hidden", sourceHidden || targetHidden);
            if (sourceHidden || targetHidden) {
              continue;
            }

            const points = buildOrthogonalPath(
              getCurrentPosition(meta.sourceModelId),
              sourceTable,
              getCurrentPosition(meta.targetModelId),
              targetTable,
            );
            meta.element.setAttribute("points", pointsToAttribute(points));
            meta.element.dataset.points = pointsToAttribute(points);
            visibleEdges.push({
              edgeId: meta.edgeId,
              points,
            });
            renderedEdges.push({
              edgeId: meta.edgeId,
              meta,
              points,
            });
          }

          if (!crossingsLayer) {
            return;
          }

          const crossings = [];
          let crossingIndex = 1;

          for (let left = 0; left < visibleEdges.length; left += 1) {
            for (let right = left + 1; right < visibleEdges.length; right += 1) {
              for (const leftSegment of findSegments(visibleEdges[left].points)) {
                for (const rightSegment of findSegments(visibleEdges[right].points)) {
                  const intersection = segmentIntersection(leftSegment, rightSegment);
                  if (!intersection) {
                    continue;
                  }

                  if (
                    isPointAtSegmentEndpoint(intersection, leftSegment) ||
                    isPointAtSegmentEndpoint(intersection, rightSegment)
                  ) {
                    continue;
                  }

                  crossings.push({
                    id: "runtime-crossing-" + crossingIndex,
                    position: intersection,
                  });
                  crossingIndex += 1;
                }
              }
            }
          }

          renderedCrossings = crossings;
          crossingsLayer.innerHTML = crossings.map(renderCrossingMarkup).join("");
        }

        function renderHiddenTableList() {
          for (const [modelId, item] of hiddenItemsById.entries()) {
            item.toggleAttribute("hidden", !getTableOptions(state, modelId).hidden);
          }
        }

        function renderOverlays() {
          renderedOverlays = [];

          for (const meta of overlayMeta) {
            const sourceTable = tableMetaById.get(meta.sourceModelId);
            const targetTable = tableMetaById.get(meta.targetModelId);
            if (!sourceTable || !targetTable) {
              continue;
            }

            const sourcePosition = getCurrentPosition(meta.sourceModelId);
            const targetPosition = getCurrentPosition(meta.targetModelId);
            const sourceCenter = getCenter(sourcePosition, sourceTable);
            const targetCenter = getCenter(targetPosition, targetTable);
            const active =
              state.selectedMethodContext &&
              state.selectedMethodContext.modelId === meta.sourceModelId &&
              state.selectedMethodContext.methodName === meta.methodName &&
              getTableOptions(state, meta.sourceModelId).showMethodHighlights &&
              isVisibleModel(meta.sourceModelId) &&
              isVisibleModel(meta.targetModelId);

            meta.element.dataset.x1 = String(sourceCenter.x);
            meta.element.dataset.y1 = String(sourceCenter.y);
            meta.element.dataset.x2 = String(targetCenter.x);
            meta.element.dataset.y2 = String(targetCenter.y);
            meta.element.classList.toggle("is-active", Boolean(active));
            meta.element.toggleAttribute("hidden", !active);
            renderedOverlays.push({
              active: Boolean(active),
              x1: sourceCenter.x,
              x2: targetCenter.x,
              y1: sourceCenter.y,
              y2: targetCenter.y,
            });
          }

          for (const [modelId, meta] of tableMetaById.entries()) {
            meta.element.classList.toggle("is-method-target", isMethodTarget(modelId));
          }
        }

        function renderPanels() {
          for (const [modelId, meta] of panelMetaById.entries()) {
            const selected = state.selectedModelId === modelId;
            const options = getTableOptions(state, modelId);

            meta.element.classList.toggle("is-selected", selected);
            meta.element.toggleAttribute("hidden", !selected);
            if (meta.methodHiddenHint) {
              meta.methodHiddenHint.toggleAttribute("hidden", options.showMethods);
            }
            if (meta.methodList) {
              const hasMethods = meta.methodList.children.length > 0;
              meta.methodList.toggleAttribute("hidden", !options.showMethods || !hasMethods);
            }
            if (meta.emptyMethodHint) {
              const hasMethods = meta.methodList && meta.methodList.children.length > 0;
              meta.emptyMethodHint.toggleAttribute("hidden", Boolean(hasMethods));
            }
            if (meta.propertyHiddenHint) {
              meta.propertyHiddenHint.toggleAttribute("hidden", options.showProperties);
            }
            if (meta.propertyList) {
              const hasProperties = meta.propertyList.children.length > 0;
              meta.propertyList.toggleAttribute("hidden", !options.showProperties || !hasProperties);
            }
            if (meta.emptyPropertyHint) {
              const hasProperties = meta.propertyList && meta.propertyList.children.length > 0;
              meta.emptyPropertyHint.toggleAttribute("hidden", Boolean(hasProperties));
            }

            for (const button of meta.toggleButtons) {
              updateToggleButton(button, options);
            }
          }

          for (const button of methodButtons) {
            const active =
              state.selectedMethodContext &&
              state.selectedMethodContext.modelId === button.dataset.modelId &&
              state.selectedMethodContext.methodName === button.dataset.methodName;
            button.classList.toggle("is-active", Boolean(active));
          }
        }

        function renderSummary() {
          for (const element of layoutReadouts) {
            element.textContent = state.layoutMode;
          }

          for (const element of hiddenCountReadouts) {
            element.textContent = String(
              state.tableOptions.filter((options) => options.hidden).length,
            );
          }

          for (const button of layoutButtons) {
            button.classList.toggle("is-active", button.dataset.layoutMode === state.layoutMode);
          }
        }

        function renderSetupControls() {
          for (const control of setupControls) {
            const key = control.dataset.setupControl;
            if (!key) {
              continue;
            }

            const nextValue = getInteractionSetting(state, key);
            if (Number(control.value) !== nextValue) {
              control.value = String(nextValue);
            }
          }

          for (const element of setupValueReadouts) {
            const key = element.dataset.setupValue;
            if (!key) {
              continue;
            }

            element.textContent = formatInteractionSettingValue(key);
          }
        }

        function renderTables() {
          for (const [modelId, meta] of tableMetaById.entries()) {
            const selected = state.selectedModelId === modelId;
            const options = getTableOptions(state, modelId);
            const position = getCurrentPosition(modelId);
            const isDraggingTable = drag && drag.kind === "table" && drag.modelId === modelId;

            meta.element.classList.toggle("is-selected", selected);
            meta.element.classList.toggle("is-dragging", Boolean(isDraggingTable));
            meta.element.setAttribute(
              "transform",
              "translate(" + position.x + " " + position.y + ")",
            );
            meta.element.dataset.hidden = String(options.hidden);
            meta.element.dataset.methodHighlights = String(options.showMethodHighlights);
            meta.element.dataset.showMethods = String(options.showMethods);
            meta.element.dataset.showProperties = String(options.showProperties);
            meta.element.toggleAttribute("hidden", options.hidden);

            if (meta.methodsSection) {
              meta.methodsSection.toggleAttribute("hidden", !options.showMethods);
            }
            if (meta.propertiesSection) {
              meta.propertiesSection.toggleAttribute("hidden", !options.showProperties);
            }
            if (meta.dividers.methods) {
              meta.dividers.methods.toggleAttribute(
                "hidden",
                !options.showMethods || !meta.methodsSection || meta.methodsSection.children.length === 0,
              );
            }
            if (meta.dividers.properties) {
              meta.dividers.properties.toggleAttribute(
                "hidden",
                !options.showProperties || !meta.propertiesSection || meta.propertiesSection.children.length === 0,
              );
            }
          }
        }

        function isMethodTarget(modelId) {
          return overlayMeta.some((meta) =>
            meta.element.classList.contains("is-active") &&
            meta.targetModelId === modelId,
          );
        }

        function updateToggleButton(button, options) {
          const toggle = button.dataset.tableToggle;
          const label = button.children[0];
          const status = button.querySelector("[data-control-status]");
          let active = false;
          let labelText = label ? label.textContent || "" : "";
          let statusText = "Off";

          switch (toggle) {
            case "hidden":
              active = !options.hidden;
              labelText = options.hidden ? "Show Table" : "Hide Table";
              statusText = options.hidden ? "Hidden" : "Visible";
              break;
            case "showMethods":
              active = options.showMethods;
              statusText = options.showMethods ? "On" : "Off";
              break;
            case "showProperties":
              active = options.showProperties;
              statusText = options.showProperties ? "On" : "Off";
              break;
            case "showMethodHighlights":
              active = options.showMethodHighlights;
              statusText = options.showMethodHighlights ? "On" : "Off";
              break;
          }

          button.classList.toggle("is-active", active);
          if (label) {
            label.textContent = labelText;
          }
          if (status) {
            status.textContent = statusText;
          }
        }
  `;
}
