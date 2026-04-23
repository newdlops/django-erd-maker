export function getBrowserEventSource(): string {
  return `
        function captureDrawingPointer(pointerId) {
          if (typeof drawingCanvas.setPointerCapture !== "function") {
            return;
          }

          try {
            drawingCanvas.setPointerCapture(pointerId);
          } catch (_error) {
            // Pointer capture can fail on some webview event sequences. Interaction should still continue.
          }
        }

        function releaseDrawingPointer(pointerId) {
          if (typeof drawingCanvas.releasePointerCapture !== "function") {
            return;
          }

          try {
            if (
              typeof drawingCanvas.hasPointerCapture !== "function" ||
              drawingCanvas.hasPointerCapture(pointerId)
            ) {
              drawingCanvas.releasePointerCapture(pointerId);
            }
          } catch (_error) {
            // Releasing a missing capture should not kill all subsequent interactions.
          }
        }

        function captureMinimapPointer(pointerId) {
          if (!minimap || typeof minimap.setPointerCapture !== "function") {
            return;
          }

          try {
            minimap.setPointerCapture(pointerId);
          } catch (_error) {
            // Minimap drag should still work as a click even if capture fails.
          }
        }

        function releaseMinimapPointer(pointerId) {
          if (!minimap || typeof minimap.releasePointerCapture !== "function") {
            return;
          }

          try {
            if (
              typeof minimap.hasPointerCapture !== "function" ||
              minimap.hasPointerCapture(pointerId)
            ) {
              minimap.releasePointerCapture(pointerId);
            }
          } catch (_error) {
            // Ignore missing capture during fast pointer sequences.
          }
        }

        let minimapDrag = null;

        function moveViewportFromMinimapEvent(event) {
          const worldPoint = getMinimapWorldPoint(event);
          if (!worldPoint) {
            return;
          }

          dispatch(createViewportPanToWorldPointAction(worldPoint));
        }

        for (const table of tables) {
          table.addEventListener("click", () => {
            dispatch({ modelId: table.dataset.modelId, type: "select-model" });
          });
          table.addEventListener("keydown", (event) => {
            if (event.key === "Enter" || event.key === " ") {
              event.preventDefault();
              dispatch({ modelId: table.dataset.modelId, type: "select-model" });
            }
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
            dispatch({
              layoutMode: button.dataset.layoutMode,
              type: "set-layout-mode",
            });
          });
        }

        for (const button of resetViewButtons) {
          button.addEventListener("click", () => {
            dispatch({
              initialState,
              type: "reset-view",
            });
          });
        }

        for (const button of document.querySelectorAll("[data-panel-refresh]")) {
          button.addEventListener("click", () => {
            vscode?.postMessage({
              layoutMode: state.layoutMode,
              settings: { ...state.settings },
              type: "diagram.requestRefresh",
            });
          });
        }

        for (const button of showHiddenButtons) {
          button.addEventListener("click", () => {
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
                dispatch({
                  hidden: !options.hidden,
                  modelId,
                  type: "set-table-hidden",
                });
                break;
              case "showMethods":
                dispatch({
                  modelId,
                  showMethods: !options.showMethods,
                  type: "set-table-show-methods",
                });
                break;
              case "showProperties":
                dispatch({
                  modelId,
                  showProperties: !options.showProperties,
                  type: "set-table-show-properties",
                });
                break;
              case "showMethodHighlights":
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
        }

        for (const button of zoomButtons) {
          button.addEventListener("click", () => {
            const zoomDelta = 0.12 * getInteractionSetting(state, "zoomSpeed");

            switch (button.dataset.zoomAction) {
              case "in":
                dispatch(createCenteredViewportZoomAction(state.viewport.zoom + zoomDelta));
                break;
              case "out":
                dispatch(createCenteredViewportZoomAction(state.viewport.zoom - zoomDelta));
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
              pointerId: event.pointerId,
            };
            canvas.classList.add("is-minimap-dragging");
            captureMinimapPointer(event.pointerId);
            moveViewportFromMinimapEvent(event);
          });

          minimap.addEventListener("pointermove", (event) => {
            if (!minimapDrag || minimapDrag.pointerId !== event.pointerId) {
              return;
            }

            event.preventDefault();
            moveViewportFromMinimapEvent(event);
          });

          minimap.addEventListener("pointerup", (event) => {
            if (!minimapDrag || minimapDrag.pointerId !== event.pointerId) {
              return;
            }

            minimapDrag = null;
            canvas.classList.remove("is-minimap-dragging");
            releaseMinimapPointer(event.pointerId);
          });

          minimap.addEventListener("pointercancel", (event) => {
            if (!minimapDrag || minimapDrag.pointerId !== event.pointerId) {
              return;
            }

            minimapDrag = null;
            canvas.classList.remove("is-minimap-dragging");
            releaseMinimapPointer(event.pointerId);
          });
        }

        drawingCanvas.addEventListener("pointerdown", (event) => {
          const target = event.target && event.target.closest ? event.target.closest(".erd-table") : null;
          const canvasTarget = target ? null : findTableAtCanvasPoint(event);
          const targetModelId = target ? target.dataset.modelId : canvasTarget?.modelId;

          if (targetModelId) {
            drag = {
              changed: false,
              kind: "table",
              modelId: targetModelId,
              originX: event.clientX,
              originY: event.clientY,
              startPosition: getCurrentPosition(targetModelId),
            };
            dispatch({
              modelId: targetModelId,
              type: "select-model",
            });
            canvas.classList.add("is-dragging-table");
          } else {
            drag = {
              changed: false,
              kind: "canvas",
              originX: event.clientX,
              originY: event.clientY,
              panX: state.viewport.panX,
              panY: state.viewport.panY,
            };
            canvas.classList.add("is-panning");
          }

          captureDrawingPointer(event.pointerId);
        });

        drawingCanvas.addEventListener("pointermove", (event) => {
          if (!drag) {
            return;
          }

          if (drag.kind === "canvas") {
            drag.changed = true;
            dispatch({
              panX: drag.panX + (event.clientX - drag.originX) * getInteractionSetting(state, "panSpeed"),
              panY: drag.panY + (event.clientY - drag.originY) * getInteractionSetting(state, "panSpeed"),
              type: "set-viewport-pan",
            });
            return;
          }

          drag.changed = true;
          dispatch({
            manualPosition: {
              x: round2(drag.startPosition.x + (event.clientX - drag.originX) / state.viewport.zoom),
              y: round2(drag.startPosition.y + (event.clientY - drag.originY) / state.viewport.zoom),
            },
            modelId: drag.modelId,
            type: "set-table-manual-position",
          });
        });

        drawingCanvas.addEventListener("pointerup", (event) => {
          const completedDrag = drag;
          drag = null;
          canvas.classList.remove("is-panning");
          canvas.classList.remove("is-dragging-table");
          releaseDrawingPointer(event.pointerId);
          cancelDragPreviewRender();

          if (!completedDrag) {
            return;
          }

          if (completedDrag.kind === "canvas") {
            cancelViewportRender();
            applyViewportState();
            return;
          }

          if (!completedDrag.changed) {
            return;
          }

          if (renderModel.modelCatalogMode) {
            invalidateCatalogSceneCache();
            applyGeometryState();
            return;
          }
          applyManualPositionState(completedDrag.modelId);
        });

        drawingCanvas.addEventListener("pointercancel", (event) => {
          const completedDrag = drag;
          drag = null;
          canvas.classList.remove("is-panning");
          canvas.classList.remove("is-dragging-table");
          releaseDrawingPointer(event.pointerId);
          cancelDragPreviewRender();

          if (!completedDrag) {
            return;
          }

          if (completedDrag.kind === "canvas") {
            cancelViewportRender();
            applyViewportState();
            return;
          }

          if (renderModel.modelCatalogMode) {
            invalidateCatalogSceneCache();
            applyGeometryState();
            return;
          }
          applyManualPositionState(completedDrag.modelId);
        });

        drawingCanvas.addEventListener("pointerleave", () => {
          if (!drag || drag.kind !== "canvas") {
            return;
          }

          drag = null;
          canvas.classList.remove("is-panning");
          canvas.classList.remove("is-dragging-table");
          applyViewportState();
        });

        drawingCanvas.addEventListener("wheel", (event) => {
          event.preventDefault();
          const zoomDelta = 0.08 * getInteractionSetting(state, "zoomSpeed");
          dispatch(
            createCenteredViewportZoomAction(
              state.viewport.zoom + (event.deltaY < 0 ? zoomDelta : -zoomDelta),
            ),
          );
        }, { passive: false });
  `;
}
