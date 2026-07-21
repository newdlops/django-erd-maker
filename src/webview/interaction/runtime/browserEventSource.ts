export function getBrowserEventSource(): string {
  return `
        function capturePointer(element, pointerId) {
          if (!element || typeof element.setPointerCapture !== "function") {
            return;
          }

          try {
            element.setPointerCapture(pointerId);
          } catch (_error) {
            // Ignore missing capture support inside the webview host.
          }
        }

        function releasePointer(element, pointerId) {
          if (!element || typeof element.releasePointerCapture !== "function") {
            return;
          }

          try {
            element.releasePointerCapture(pointerId);
          } catch (_error) {
            // Ignore fast pointer release races.
          }
        }

        let minimapDrag = null;
        let resizeRenderFrame = 0;
        let layoutRefreshPending = false;

        function setLayoutRefreshPending(pending) {
          layoutRefreshPending = Boolean(pending);
          root.toggleAttribute("aria-busy", layoutRefreshPending);
          for (const button of document.querySelectorAll(
            "[data-layout-mode], [data-panel-refresh], [data-cluster-graph-toggle], " +
            "[data-bubble-toggle], [data-optimized-toggle]",
          )) {
            button.disabled = layoutRefreshPending;
          }
          for (const button of document.querySelectorAll("[data-optimized-toggle]")) {
            if (!button.dataset.idleLabel) {
              button.dataset.idleLabel = button.textContent || "ML Optimized";
            }
            button.textContent = layoutRefreshPending && state.optimizedLayout
              ? "ML Analyzing"
              : button.dataset.idleLabel;
          }
        }

        function requestDiagramRefresh(message) {
          if (layoutRefreshPending) {
            return false;
          }
          setLayoutRefreshPending(true);
          vscode?.postMessage(message);
          return true;
        }

        function moveViewportFromMinimapEvent(event) {
          const worldPoint = getMinimapWorldPoint(event);
          if (!worldPoint) {
            return;
          }

          dispatch(createViewportPanToWorldPointAction(worldPoint));
        }

        function scheduleResizeRender() {
          if (resizeRenderFrame) {
            return;
          }

          resizeRenderFrame = window.requestAnimationFrame(() => {
            resizeRenderFrame = 0;
            cancelViewportRender();
            applyState();
          });
        }

        for (const button of sidebarTabButtons) {
          button.addEventListener("click", () => {
            setSidebarSheet(button.dataset.sidebarTab || "model", false);
          });
          button.addEventListener("keydown", (event) => {
            if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") {
              return;
            }
            event.preventDefault();
            const currentIndex = sidebarTabButtons.indexOf(button);
            const direction = event.key === "ArrowRight" ? 1 : -1;
            const nextIndex = (currentIndex + direction + sidebarTabButtons.length) % sidebarTabButtons.length;
            setSidebarSheet(sidebarTabButtons[nextIndex]?.dataset.sidebarTab || "model", true);
          });
        }

        for (const button of layoutButtons) {
          button.addEventListener("click", () => {
            logErd("info", "event.layout.click", {
              layoutMode: button.dataset.layoutMode,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            requestDiagramRefresh({
              layoutMode: button.dataset.layoutMode,
              refreshKind: "layout",
              settings: { ...state.settings },
              viewState: createRefreshViewStateSnapshot(state),
              type: "diagram.requestRefresh",
            });
          });
        }

        for (const button of resetViewButtons) {
          button.addEventListener("click", () => {
            logErd("info", "event.viewport.reset", {
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            dispatch({
              initialState,
              type: "reset-view",
            });
          });
        }

        for (const button of document.querySelectorAll("[data-panel-refresh]")) {
          button.addEventListener("click", () => {
            logErd("info", "event.refresh.request", {
              layoutMode: state.layoutMode,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            requestDiagramRefresh({
              layoutMode: state.layoutMode,
              refreshKind: "full",
              settings: { ...state.settings },
              viewState: createRefreshViewStateSnapshot(state),
              type: "diagram.requestRefresh",
            });
          });
        }

        for (const button of document.querySelectorAll("[data-cluster-collapse-toggle]")) {
          if (state.collapseClusters) {
            button.classList.add("is-active");
          }
          button.addEventListener("click", () => {
            state.collapseClusters = !state.collapseClusters;
            button.classList.toggle("is-active", state.collapseClusters);
            logErd("info", "event.cluster.collapse.toggle", {
              enabled: state.collapseClusters,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            invalidateSceneGraph();
            applyState();
          });
        }

        for (const button of document.querySelectorAll("[data-cluster-graph-toggle]")) {
          if (state.clusterGraphLayout) {
            button.classList.add("is-active");
          }
          button.addEventListener("click", () => {
            state.clusterGraphLayout = !state.clusterGraphLayout;
            button.classList.toggle("is-active", state.clusterGraphLayout);
            logErd("info", "event.cluster.graph.toggle", {
              enabled: state.clusterGraphLayout,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            // Cluster-graph layout is computed in C++; trigger a layout refresh.
            requestDiagramRefresh({
              layoutMode: state.layoutMode,
              refreshKind: "layout",
              settings: { ...state.settings },
              viewState: createRefreshViewStateSnapshot(state),
              type: "diagram.requestRefresh",
            });
          });
        }

        for (const button of document.querySelectorAll("[data-bubble-toggle]")) {
          if (state.bubbleLayout) {
            button.classList.add("is-active");
          }
          button.addEventListener("click", () => {
            state.bubbleLayout = !state.bubbleLayout;
            button.classList.toggle("is-active", state.bubbleLayout);
            logErd("info", "event.bubble.toggle", {
              enabled: state.bubbleLayout,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            requestDiagramRefresh({
              layoutMode: state.layoutMode,
              refreshKind: "layout",
              settings: { ...state.settings },
              viewState: createRefreshViewStateSnapshot(state),
              type: "diagram.requestRefresh",
            });
          });
        }

        for (const button of document.querySelectorAll("[data-optimized-toggle]")) {
          if (state.optimizedLayout) {
            button.classList.add("is-active");
          }
          button.addEventListener("click", () => {
            state.optimizedLayout = !state.optimizedLayout;
            button.classList.toggle("is-active", state.optimizedLayout);
            logErd("info", "event.optimized.toggle", {
              enabled: state.optimizedLayout,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            requestDiagramRefresh({
              layoutMode: state.layoutMode,
              refreshKind: "layout",
              settings: { ...state.settings },
              viewState: createRefreshViewStateSnapshot(state),
              type: "diagram.requestRefresh",
            });
          });
        }

        root.addEventListener("click", (event) => {
          const target = event.target;
          if (!(target instanceof Element)) {
            return;
          }

          const button = target.closest("button");
          if (!button) {
            return;
          }

          if (button.matches("[data-method-button]")) {
            dispatch({
              methodName: button.dataset.methodName,
              modelId: button.dataset.modelId,
              type: "toggle-method",
            });
            return;
          }

          if (button.matches("[data-show-hidden-model]")) {
            logErd("info", "event.table.show", {
              modelId: button.dataset.modelId,
            });
            dispatch({
              hidden: false,
              modelId: button.dataset.modelId,
              type: "set-table-hidden",
            });
            dispatch({
              modelId: button.dataset.modelId,
              type: "select-model",
            });
            return;
          }

          if (!button.matches("[data-table-toggle]")) {
            return;
          }

          const modelId = button.dataset.modelId;
          const options = getTableOptions(state, modelId);

          switch (button.dataset.tableToggle) {
            case "hidden":
              logErd("info", "event.table.toggle", {
                hidden: !options.hidden,
                modelId,
                toggle: "hidden",
              });
              dispatch({
                hidden: !options.hidden,
                modelId,
                type: "set-table-hidden",
              });
              break;
            case "showMethods":
              logErd("info", "event.table.toggle", {
                modelId,
                showMethods: !options.showMethods,
                toggle: "showMethods",
              });
              dispatch({
                modelId,
                showMethods: !options.showMethods,
                type: "set-table-show-methods",
              });
              break;
            case "showProperties":
              logErd("info", "event.table.toggle", {
                modelId,
                showProperties: !options.showProperties,
                toggle: "showProperties",
              });
              dispatch({
                modelId,
                showProperties: !options.showProperties,
                type: "set-table-show-properties",
              });
              break;
            case "showMethodHighlights":
              logErd("info", "event.table.toggle", {
                modelId,
                showMethodHighlights: !options.showMethodHighlights,
                toggle: "showMethodHighlights",
              });
              dispatch({
                modelId,
                showMethodHighlights: !options.showMethodHighlights,
                type: "set-table-show-method-highlights",
              });
              break;
          }
        });

        for (const control of setupControls) {
          control.addEventListener("input", () => {
            const key = control.dataset.setupControl;
            if (!key) {
              return;
            }

            dispatch({
              key,
              type: "set-interaction-setting",
              value: Number(control.value),
            });
            vscode?.postMessage({
              settings: { ...state.settings },
              type: "diagram.updateSetupSettings",
            });
          });
          control.addEventListener("change", () => {
            const key = control.dataset.setupControl;
            if (!key) {
              return;
            }

            logErd("info", "event.setup.changed", {
              key,
              value: Number(control.value),
            });
          });
        }

        for (const button of zoomButtons) {
          button.addEventListener("click", () => {
            const zoomFactor = 1 + 0.18 * getInteractionSetting(state, "zoomSpeed");
            logErd("info", "event.zoom.click", {
              action: button.dataset.zoomAction,
              zoom: state.viewport.zoom,
            });

            const drawingRect = drawingCanvas.getBoundingClientRect();
            const anchorX = drawingRect.width / 2;
            const anchorY = drawingRect.height / 2;
            const oldZoom = state.viewport.zoom;
            switch (button.dataset.zoomAction) {
              case "in": {
                const newZoom = clampZoom(oldZoom * zoomFactor);
                if (newZoom === oldZoom) break;
                const ratio = newZoom / oldZoom;
                dispatch({
                  type: "set-viewport-zoom",
                  zoom: newZoom,
                  panX: anchorX - (anchorX - state.viewport.panX) * ratio,
                  panY: anchorY - (anchorY - state.viewport.panY) * ratio,
                });
                break;
              }
              case "out": {
                const newZoom = clampZoom(oldZoom / zoomFactor);
                if (newZoom === oldZoom) break;
                const ratio = newZoom / oldZoom;
                dispatch({
                  type: "set-viewport-zoom",
                  zoom: newZoom,
                  panX: anchorX - (anchorX - state.viewport.panX) * ratio,
                  panY: anchorY - (anchorY - state.viewport.panY) * ratio,
                });
                break;
              }
              case "fit":
                dispatch({
                  type: "fit-viewport",
                });
                break;
              case "center":
                dispatch({
                  type: "center-viewport",
                });
                break;
            }
          });
        }

        // --- Node search + focus -------------------------------------------
        const searchInput = document.querySelector("[data-erd-search]");
        const searchCount = document.querySelector("[data-erd-search-count]");
        let searchMatches = [];
        let searchIndex = -1;

        function rankSearchMatch(meta, q) {
          const name = String(meta.modelName || "").toLowerCase();
          if (name === q) return 0;
          if (name.indexOf(q) === 0) return 1;
          if (name.indexOf(q) >= 0) return 2;
          if (String(meta.tableName || "").toLowerCase().indexOf(q) >= 0) return 3;
          if (String(meta.appLabel || "").toLowerCase().indexOf(q) >= 0) return 4;
          return -1;
        }

        function updateSearchCount() {
          if (!searchCount) return;
          const q = searchInput ? searchInput.value.trim() : "";
          if (!q) {
            searchCount.textContent = "";
            return;
          }
          const total = searchMatches.length;
          if (total === 0) {
            searchCount.textContent = "0";
            return;
          }
          searchCount.textContent =
            searchIndex >= 0 ? (searchIndex + 1) + "/" + total : String(total);
        }

        function runSearch(raw) {
          const q = String(raw || "").trim().toLowerCase();
          searchMatches = [];
          searchIndex = -1;
          if (q) {
            const scored = [];
            for (const meta of tableMetaList) {
              if (!meta || !meta.modelId) continue;
              if (String(meta.modelId).indexOf("__leafbundle.") === 0) continue;
              const rank = rankSearchMatch(meta, q);
              if (rank >= 0) scored.push({ meta: meta, rank: rank });
            }
            scored.sort(function (a, b) {
              return (
                a.rank - b.rank ||
                String(a.meta.modelName || "").length -
                  String(b.meta.modelName || "").length ||
                String(a.meta.modelName || "").localeCompare(
                  String(b.meta.modelName || ""),
                )
              );
            });
            searchMatches = scored.map(function (s) {
              return s.meta;
            });
          }
          updateSearchCount();
        }

        function focusSearchMatch(index) {
          const total = searchMatches.length;
          if (total === 0) return;
          const wrapped = ((index % total) + total) % total;
          searchIndex = wrapped;
          const meta = searchMatches[wrapped];
          dispatch({ type: "focus-model", modelId: meta.modelId, zoom: 1 });
          updateSearchCount();
        }

        if (searchInput) {
          searchInput.addEventListener("input", function () {
            runSearch(searchInput.value);
          });
          searchInput.addEventListener("keydown", function (event) {
            if (event.key === "Enter") {
              event.preventDefault();
              if (searchMatches.length === 0) return;
              focusSearchMatch(event.shiftKey ? searchIndex - 1 : searchIndex + 1);
            } else if (event.key === "Escape") {
              event.preventDefault();
              searchInput.value = "";
              runSearch("");
              searchInput.blur();
            }
          });
          window.addEventListener("keydown", function (event) {
            if (
              (event.metaKey || event.ctrlKey) &&
              (event.key === "f" || event.key === "F")
            ) {
              event.preventDefault();
              searchInput.focus();
              searchInput.select();
            }
          });
        }
        // -------------------------------------------------------------------

        // --- Progressive layout preview (multistart streaming) -------------
        // While a re-layout runs, the extension streams intermediate
        // multistart layouts (one per new-best seed) as { type:
        // "diagram.progress", positions: { modelId: [x, y] } }. Override node
        // positions so the user watches the layout improve; the fully-routed
        // final layout reloads the webview when the refresh completes.
        function applyProgressSemanticRenderModel(preview) {
          if (!preview || !Array.isArray(preview.edges)) {
            return;
          }

          renderModel.edges = preview.edges.slice();
          edgeMeta.splice(0, edgeMeta.length, ...preview.edges.map(readEdgeMeta));

          if (Array.isArray(preview.tables)) {
            renderModel.tables = preview.tables.slice();
            tableMetaList.splice(
              0,
              tableMetaList.length,
              ...preview.tables.map((table) => readTableMeta(table)),
            );
            tableMetaById.clear();
            tableRenderById.clear();
            for (let index = 0; index < tableMetaList.length; index += 1) {
              const meta = tableMetaList[index];
              const table = preview.tables[index];
              tableMetaById.set(meta.modelId, meta);
              tableRenderById.set(meta.modelId, table);
            }
            const nextLayoutVariants = createLayoutVariants(tableMetaList);
            for (const layoutMode of layoutModes) {
              layoutVariants[layoutMode] = nextLayoutVariants[layoutMode] || {};
            }
          }

          renderModel.bundleLeafTiles = Array.isArray(preview.bundleLeafTiles)
            ? preview.bundleLeafTiles.slice()
            : [];
          renderModel.leafBundles = Array.isArray(preview.leafBundles)
            ? preview.leafBundles.slice()
            : [];
          for (const fakeId of Object.keys(bundleLeavesByFakeIdRaw)) {
            delete bundleLeavesByFakeIdRaw[fakeId];
          }
          for (const leafId of Object.keys(bundleLeafToFakeId)) {
            delete bundleLeafToFakeId[leafId];
          }
          const nextBundles = preview.bundleLeavesByFakeId || {};
          for (const fakeId of Object.keys(nextBundles)) {
            const leaves = Array.isArray(nextBundles[fakeId])
              ? nextBundles[fakeId].slice()
              : [];
            bundleLeavesByFakeIdRaw[fakeId] = leaves;
            for (const leafId of leaves) {
              bundleLeafToFakeId[leafId] = fakeId;
            }
          }
          renderModel.bundleLeavesByFakeId = bundleLeavesByFakeIdRaw;

          logErd("info", "progress.semantic.applied", {
            edges: edgeMeta.length,
            leafBundles: renderModel.leafBundles.length,
            tables: tableMetaList.length,
          });
        }

        window.addEventListener("message", function (event) {
          const msg = event && event.data;
          if (msg && msg.type === "diagram.refresh.settled") {
            setLayoutRefreshPending(false);
            logErd(msg.status === "error" ? "warn" : "info", "refresh.settled", {
              requestId: msg.requestId,
              status: msg.status || "complete",
            });
            return;
          }
          if (!msg || msg.type !== "diagram.progress" || !msg.positions) return;
          applyProgressSemanticRenderModel(msg.semanticRenderModel);
          dispatch({ type: "apply-progress-positions", positions: msg.positions });
        });
        // -------------------------------------------------------------------

        if (minimap) {
          minimap.addEventListener("pointerdown", (event) => {
            event.preventDefault();
            event.stopPropagation();
            minimapDrag = {
              startedAt: performance.now(),
              pointerId: event.pointerId,
            };
            logErd("info", "event.minimap.pan.start", {
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            capturePointer(minimap, event.pointerId);
            moveViewportFromMinimapEvent(event);
          });

          minimap.addEventListener("pointermove", (event) => {
            if (!minimapDrag || minimapDrag.pointerId !== event.pointerId) {
              return;
            }

            event.preventDefault();
            event.stopPropagation();
            moveViewportFromMinimapEvent(event);
          });

          minimap.addEventListener("pointerup", (event) => {
            if (!minimapDrag || minimapDrag.pointerId !== event.pointerId) {
              return;
            }

            const completedDrag = minimapDrag;
            minimapDrag = null;
            releasePointer(minimap, event.pointerId);
            logErdDuration("info", "event.minimap.pan.end", completedDrag.startedAt || performance.now(), {
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
          });

          minimap.addEventListener("pointercancel", (event) => {
            if (!minimapDrag || minimapDrag.pointerId !== event.pointerId) {
              return;
            }

            minimapDrag = null;
            releasePointer(minimap, event.pointerId);
            logErd("warn", "event.minimap.pan.cancel", {
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
          });
        }

        if (typeof ResizeObserver === "function") {
          const resizeObserver = new ResizeObserver(() => {
            scheduleResizeRender();
          });
          resizeObserver.observe(canvas);
          resizeObserver.observe(root);
        } else {
          window.addEventListener("resize", scheduleResizeRender);
        }

        canvas.addEventListener("pointerdown", (event) => {
          const canvasTarget = findTableAtCanvasPoint(event);
          const targetModelId = canvasTarget?.modelId || canvasTarget?.meta?.modelId;

          if (targetModelId) {
            drag = {
              currentPosition: getCurrentPosition(targetModelId),
              kind: "table",
              modelId: targetModelId,
              originX: event.clientX,
              originY: event.clientY,
              startedAt: performance.now(),
              startPosition: getCurrentPosition(targetModelId),
            };
            logErd("info", "event.drag.start", {
              kind: "table",
              modelId: targetModelId,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            dispatch({
              modelId: targetModelId,
              type: "select-model",
            });
            canvas.classList.add("is-dragging-table");
          } else {
            dispatch({ type: "clear-selection" });
            drag = {
              kind: "canvas",
              originX: event.clientX,
              originY: event.clientY,
              panX: state.viewport.panX,
              panY: state.viewport.panY,
              startedAt: performance.now(),
            };
            logErd("info", "event.drag.start", {
              kind: "canvas",
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
            canvas.classList.add("is-panning");
          }

          capturePointer(canvas, event.pointerId);
        });

        canvas.addEventListener("pointermove", (event) => {
          if (!drag) {
            return;
          }

          if (drag.kind === "canvas") {
            dispatch({
              panX: drag.panX + (event.clientX - drag.originX) * getInteractionSetting(state, "panSpeed"),
              panY: drag.panY + (event.clientY - drag.originY) * getInteractionSetting(state, "panSpeed"),
              type: "set-viewport-pan",
            });
            return;
          }

          drag.currentPosition = {
            x: round2(drag.startPosition.x + (event.clientX - drag.originX) / state.viewport.zoom),
            y: round2(drag.startPosition.y + (event.clientY - drag.originY) / state.viewport.zoom),
          };
          scheduleViewportRender();
        });

        canvas.addEventListener("pointerup", (event) => {
          const completedDrag = drag;
          drag = null;
          canvas.classList.remove("is-panning");
          canvas.classList.remove("is-dragging-table");
          releasePointer(canvas, event.pointerId);
          if (completedDrag && completedDrag.kind === "table" && completedDrag.currentPosition) {
            dispatch({
              manualPosition: completedDrag.currentPosition,
              modelId: completedDrag.modelId,
              type: "set-table-manual-position",
            });
            const groupLeaves = bundleLeavesByFakeIdRaw[completedDrag.modelId];
            if (Array.isArray(groupLeaves) && completedDrag.startPosition) {
              const dx = completedDrag.currentPosition.x - completedDrag.startPosition.x;
              const dy = completedDrag.currentPosition.y - completedDrag.startPosition.y;
              for (const leafId of groupLeaves) {
                const leafOptions = getTableOptions(state, leafId);
                const base = leafOptions.manualPosition || getBasePosition(leafId);
                dispatch({
                  manualPosition: { x: round2(base.x + dx), y: round2(base.y + dy) },
                  modelId: leafId,
                  type: "set-table-manual-position",
                });
              }
            }
          } else {
            applyState();
          }
          if (completedDrag) {
            logErdDuration("info", "event.drag.end", completedDrag.startedAt || performance.now(), {
              kind: completedDrag.kind,
              modelId: completedDrag.modelId,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
          }
        });

        canvas.addEventListener("pointercancel", () => {
          const canceledDrag = drag;
          drag = null;
          canvas.classList.remove("is-panning");
          canvas.classList.remove("is-dragging-table");
          applyState();
          if (canceledDrag) {
            logErd("warn", "event.drag.cancel", {
              kind: canceledDrag.kind,
              modelId: canceledDrag.modelId,
              renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            });
          }
        });

        canvas.addEventListener("pointerleave", () => {
          if (!drag || drag.kind !== "canvas") {
            return;
          }

          drag = null;
          canvas.classList.remove("is-panning");
          canvas.classList.remove("is-dragging-table");
          applyState();
        });

        canvas.addEventListener("wheel", (event) => {
          event.preventDefault();
          const speed = getInteractionSetting(state, "zoomSpeed");
          const sensitivity = 0.0015 * speed;
          const rawDelta = event.deltaY;
          const clampedDelta = Math.max(-100, Math.min(100, rawDelta));
          const factor = Math.exp(-clampedDelta * sensitivity);
          const oldZoom = state.viewport.zoom;
          const requestedZoom = oldZoom * factor;
          const newZoom = clampZoom(requestedZoom);
          if (newZoom === oldZoom) {
            return;
          }
          const drawingRect = drawingCanvas.getBoundingClientRect();
          const anchorX = drawingRect.width / 2;
          const anchorY = drawingRect.height / 2;
          const ratio = newZoom / oldZoom;
          const newPanX = anchorX - (anchorX - state.viewport.panX) * ratio;
          const newPanY = anchorY - (anchorY - state.viewport.panY) * ratio;
          dispatch({
            type: "set-viewport-zoom",
            zoom: newZoom,
            panX: newPanX,
            panY: newPanY,
          });
        }, { passive: false });

        // Keyboard navigation: arrow keys pan; +/- zoom; shift = 5x pan.
        window.addEventListener("keydown", (event) => {
          const target = event.target;
          if (target && target.tagName) {
            const tag = target.tagName;
            if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return;
            if (target.isContentEditable) return;
          }
          const baseStep = 80;
          const step = event.shiftKey ? baseStep * 5 : baseStep;
          let panDX = 0;
          let panDY = 0;
          let zoomFactor = 0;
          switch (event.key) {
            case "ArrowLeft":  panDX = step;  break;
            case "ArrowRight": panDX = -step; break;
            case "ArrowUp":    panDY = step;  break;
            case "ArrowDown":  panDY = -step; break;
            case "+":
            case "=":          zoomFactor = 1.1; break;
            case "-":
            case "_":          zoomFactor = 1 / 1.1; break;
            default: return;
          }
          event.preventDefault();
          if (panDX !== 0 || panDY !== 0) {
            dispatch({
              panX: state.viewport.panX + panDX,
              panY: state.viewport.panY + panDY,
              type: "set-viewport-pan",
            });
            return;
          }
          if (zoomFactor !== 0) {
            const oldZoom = state.viewport.zoom;
            const newZoom = clampZoom(oldZoom * zoomFactor);
            if (newZoom === oldZoom) return;
            const drawingRect = drawingCanvas.getBoundingClientRect();
            const anchorX = drawingRect.width / 2;
            const anchorY = drawingRect.height / 2;
            const ratio = newZoom / oldZoom;
            const newPanX = anchorX - (anchorX - state.viewport.panX) * ratio;
            const newPanY = anchorY - (anchorY - state.viewport.panY) * ratio;
            dispatch({
              type: "set-viewport-zoom",
              zoom: newZoom,
              panX: newPanX,
              panY: newPanY,
            });
          }
        });
  `;
}
