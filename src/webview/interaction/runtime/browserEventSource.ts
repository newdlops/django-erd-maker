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

        for (const button of methodButtons) {
          button.addEventListener("click", () => {
            dispatch({
              methodName: button.dataset.methodName,
              modelId: button.dataset.modelId,
              type: "toggle-method",
            });
          });
        }

        for (const button of layoutButtons) {
          button.addEventListener("click", () => {
            logErd("info", "event.layout.click", {
              layoutMode: button.dataset.layoutMode,
            });
            dispatch({
              layoutMode: button.dataset.layoutMode,
              type: "set-layout-mode",
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
            vscode?.postMessage({
              settings: { ...state.settings },
              type: "diagram.requestRefresh",
            });
          });
        }

        for (const button of showHiddenButtons) {
          button.addEventListener("click", () => {
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
          });
        }

        for (const button of tableToggleButtons) {
          button.addEventListener("click", () => {
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
        }

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
            const zoomDelta = 0.12 * getInteractionSetting(state, "zoomSpeed");
            logErd("info", "event.zoom.click", {
              action: button.dataset.zoomAction,
              zoom: state.viewport.zoom,
            });

            switch (button.dataset.zoomAction) {
              case "in":
                dispatch({
                  type: "set-viewport-zoom",
                  zoom: state.viewport.zoom + zoomDelta,
                });
                break;
              case "out":
                dispatch({
                  type: "set-viewport-zoom",
                  zoom: state.viewport.zoom - zoomDelta,
                });
                break;
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
          const zoomDelta = 0.08 * getInteractionSetting(state, "zoomSpeed");
          dispatch({
            type: "set-viewport-zoom",
            zoom: state.viewport.zoom + (event.deltaY < 0 ? zoomDelta : -zoomDelta),
          });
        }, { passive: false });
  `;
}
