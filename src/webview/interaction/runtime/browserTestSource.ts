export function getBrowserTestSource(): string {
  return `
        function createTestSnapshot() {
          const crossings = crossingsLayer
            ? Array.from(crossingsLayer.querySelectorAll("[data-crossing-id]")).map((element) => ({
                hidden: element.hasAttribute("hidden"),
                id: element.dataset.crossingId || "",
              }))
            : [];

          return {
            catalogCrossings: latestCatalogCrossings.map((crossing) => ({
              x: crossing.position.x,
              y: crossing.position.y,
            })),
            crossings,
            edges: edgeMeta.map((meta) => ({
              edgeId: meta.edgeId,
              hidden: meta.element.hasAttribute("hidden"),
              points: meta.element.getAttribute("points") || "",
              sourceModelId: meta.sourceModelId,
              targetModelId: meta.targetModelId,
            })),
            hiddenModelIds: state.tableOptions
              .filter((options) => options.hidden)
              .map((options) => options.modelId),
            layoutButtons: layoutButtons.map((button) => ({
              active: button.classList.contains("is-active"),
              layoutMode: button.dataset.layoutMode || "",
              text: (button.textContent || "").trim(),
            })),
            overlays: overlayMeta.map((meta) => ({
              active: meta.element.classList.contains("is-active"),
              hidden: meta.element.hasAttribute("hidden"),
              id: meta.element.id || "",
              methodName: meta.methodName,
              sourceModelId: meta.sourceModelId,
              targetModelId: meta.targetModelId,
            })),
            panels: Array.from(panelMetaById.entries()).map(([modelId, meta]) => ({
              activeMethodNames: Array.from(
                meta.element.querySelectorAll("[data-method-button].is-active"),
              ).map((button) => button.dataset.methodName || ""),
              hidden: meta.element.hasAttribute("hidden"),
              methodListHidden: meta.methodList ? meta.methodList.hasAttribute("hidden") : true,
              modelId,
              propertyListHidden: meta.propertyList ? meta.propertyList.hasAttribute("hidden") : true,
              selected: meta.element.classList.contains("is-selected"),
            })),
            scene: createSceneSnapshot(),
            state: cloneState(state),
            tables: Array.from(tableMetaById.entries()).map(([modelId, meta]) => ({
              hidden: meta.element.hasAttribute("hidden"),
              isMethodTarget: meta.element.classList.contains("is-method-target"),
              modelId,
              selected: meta.element.classList.contains("is-selected"),
              screenRect: getTableScreenRect(modelId, meta),
              showMethodHighlights: meta.element.dataset.methodHighlights === "true",
              showMethods: meta.element.dataset.showMethods === "true",
              showProperties: meta.element.dataset.showProperties === "true",
              tableName: meta.tableName,
              transform: meta.element.getAttribute("transform") || "",
              height: meta.height,
              width: meta.width,
            })),
          };
        }

        function createSceneSnapshot() {
          const canvasRect = canvas.getBoundingClientRect();
          const drawingCanvasRect = drawingCanvas.getBoundingClientRect();
          const visibleBounds = getVisibleWorldBounds();
          const visibleTableIds = [];

          for (const [modelId, meta] of tableMetaById.entries()) {
            if (!isVisibleModel(modelId)) {
              continue;
            }

            const position = getCurrentPosition(modelId);
            if (rectIntersectsBounds(position.x, position.y, meta.width, meta.height, visibleBounds, 0)) {
              visibleTableIds.push(modelId);
            }
          }

          return {
            canvasInkSample: sampleCanvasInk(),
            canvasRect: {
              height: canvasRect.height,
              width: canvasRect.width,
            },
            drawingCanvas: {
              height: drawingCanvas.height,
              rectHeight: drawingCanvasRect.height,
              rectWidth: drawingCanvasRect.width,
              width: drawingCanvas.width,
            },
            minimap: createMinimapTestSnapshot(),
            minimapVisible: Boolean(minimap && !minimap.hasAttribute("hidden")),
            visibleTableIds: visibleTableIds.sort(),
          };
        }

        function createMinimapTestSnapshot() {
          const canvasRect = minimapCanvas
            ? minimapCanvas.getBoundingClientRect()
            : { height: 0, width: 0 };
          const viewportRect = readMinimapViewportRect();

          return {
            canvasRect: {
              height: canvasRect.height,
              width: canvasRect.width,
            },
            viewport: viewportRect,
            visible: Boolean(minimap && !minimap.hasAttribute("hidden")),
          };
        }

        function readMinimapViewportRect() {
          if (!minimapViewport) {
            return {
              height: 0,
              width: 0,
              x: 0,
              y: 0,
            };
          }

          const transform = minimapViewport.style.transform || "";
          const match = transform.match(/translate\\((-?[0-9.]+)px,\\s*(-?[0-9.]+)px\\)/);

          return {
            height: Number.parseFloat(minimapViewport.style.height || "0") || 0,
            width: Number.parseFloat(minimapViewport.style.width || "0") || 0,
            x: match ? Number(match[1]) : 0,
            y: match ? Number(match[2]) : 0,
          };
        }

        function sampleCanvasInk() {
          const width = drawingCanvas.width;
          const height = drawingCanvas.height;
          if (!width || !height) {
            return {
              inkPixels: 0,
              sampledPixels: 0,
            };
          }

          const data = drawingContext.getImageData(0, 0, width, height).data;
          const targetSamples = 14000;
          const step = Math.max(1, Math.floor(Math.sqrt((width * height) / targetSamples)));
          let inkPixels = 0;
          let sampledPixels = 0;

          for (let y = 0; y < height; y += step) {
            for (let x = 0; x < width; x += step) {
              const alphaIndex = (y * width + x) * 4 + 3;
              sampledPixels += 1;
              if (data[alphaIndex] > 0) {
                inkPixels += 1;
              }
            }
          }

          return {
            inkPixels,
            sampledPixels,
          };
        }

        function getTableScreenRect(modelId, meta) {
          const position = getCurrentPosition(modelId);

          return {
            bottom: position.y * state.viewport.zoom + state.viewport.panY + meta.height * state.viewport.zoom,
            left: position.x * state.viewport.zoom + state.viewport.panX,
            right: position.x * state.viewport.zoom + state.viewport.panX + meta.width * state.viewport.zoom,
            top: position.y * state.viewport.zoom + state.viewport.panY,
          };
        }

        function requireElement(element, label) {
          if (!element) {
            throw new Error("Missing test target: " + label);
          }

          return element;
        }

        function dispatchPointer(type, clientX, clientY, pointerId, buttons) {
          drawingCanvas.dispatchEvent(new PointerEvent(type, {
            bubbles: true,
            button: 0,
            buttons,
            clientX,
            clientY,
            composed: true,
            pointerId,
            pointerType: "mouse",
          }));
        }

        function toClientPointFromWorld(worldPoint) {
          const rect = drawingCanvas.getBoundingClientRect();

          return {
            x: rect.left + worldPoint.x * state.viewport.zoom + state.viewport.panX,
            y: rect.top + worldPoint.y * state.viewport.zoom + state.viewport.panY,
          };
        }

        function getTableCenterWorldPoint(modelId) {
          const meta = requireElement(
            tableMetaById.get(modelId),
            "table meta " + modelId,
          );
          const position = getCurrentPosition(modelId);

          return {
            x: position.x + meta.width / 2,
            y: position.y + meta.height / 2,
          };
        }

        function pointerSelectTable(modelId) {
          const clientPoint = toClientPointFromWorld(getTableCenterWorldPoint(modelId));

          dispatchPointer("pointerdown", clientPoint.x, clientPoint.y, 1, 1);
          dispatchPointer("pointerup", clientPoint.x, clientPoint.y, 1, 0);
        }

        function pointerDragTableBy(modelId, delta) {
          const start = toClientPointFromWorld(getTableCenterWorldPoint(modelId));
          const end = {
            x: start.x + delta.x * state.viewport.zoom,
            y: start.y + delta.y * state.viewport.zoom,
          };

          dispatchPointer("pointerdown", start.x, start.y, 1, 1);
          dispatchPointer("pointermove", end.x, end.y, 1, 1);
          dispatchPointer("pointerup", end.x, end.y, 1, 0);
        }

        function pointerPanBy(delta) {
          const start = findCanvasPanClientPoint();
          const end = {
            x: start.x + delta.x,
            y: start.y + delta.y,
          };

          dispatchPointer("pointerdown", start.x, start.y, 2, 1);
          dispatchPointer("pointermove", end.x, end.y, 2, 1);
          dispatchPointer("pointerup", end.x, end.y, 2, 0);
        }

        function findCanvasPanClientPoint() {
          const rect = drawingCanvas.getBoundingClientRect();
          const candidates = [
            { x: rect.left + 18, y: rect.top + 18 },
            { x: rect.left + rect.width - 18, y: rect.top + rect.height - 18 },
            { x: rect.left + 18, y: rect.top + rect.height - 18 },
            { x: rect.left + rect.width - 18, y: rect.top + 18 },
            { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 },
          ];

          for (const candidate of candidates) {
            if (!findTableAtCanvasPoint(candidate)) {
              return candidate;
            }
          }

          return candidates[0];
        }

        function runTestAction(action) {
          switch (action.type) {
            case "snapshot":
              return;
            case "clickLayoutMode":
              requireElement(
                layoutButtons.find((button) => button.dataset.layoutMode === action.layoutMode),
                "layout button " + action.layoutMode,
              ).click();
              return;
            case "clickZoomAction":
              requireElement(
                zoomButtons.find((button) => button.dataset.zoomAction === action.zoomAction),
                "zoom button " + action.zoomAction,
              ).click();
              return;
            case "clickMethod":
              requireElement(
                Array.from(document.querySelectorAll("[data-method-button]")).find((button) =>
                  button.dataset.modelId === action.modelId &&
                  button.dataset.methodName === action.methodName,
                ),
                "method button " + action.modelId + "." + action.methodName,
              ).click();
              return;
            case "clickShowHiddenModel":
              requireElement(
                Array.from(document.querySelectorAll("[data-show-hidden-model]")).find(
                  (button) => button.dataset.modelId === action.modelId,
                ),
                "show hidden button " + action.modelId,
              ).click();
              return;
            case "clickTable":
              requireElement(
                tableMetaById.get(action.modelId) && tableMetaById.get(action.modelId).element,
                "table " + action.modelId,
              ).click();
              return;
            case "clickTableToggle":
              requireElement(
                Array.from(document.querySelectorAll("[data-table-toggle]")).find((button) =>
                  button.dataset.modelId === action.modelId &&
                  button.dataset.tableToggle === action.toggle,
                ),
                "table toggle " + action.modelId + "." + action.toggle,
              ).click();
              return;
            case "dragTableTo":
              dispatch({
                manualPosition: {
                  x: action.position.x,
                  y: action.position.y,
                },
                modelId: action.modelId,
                type: "set-table-manual-position",
              });
              return;
            case "pointerSelectTable":
              pointerSelectTable(action.modelId);
              return;
            case "pointerDragTableBy":
              pointerDragTableBy(action.modelId, action.delta);
              return;
            case "pointerPanBy":
              pointerPanBy(action.delta);
              return;
            case "setSetupControl": {
              const control = requireElement(
                setupControls.find((candidate) => candidate.dataset.setupControl === action.key),
                "setup control " + action.key,
              );
              control.value = String(action.value);
              control.dispatchEvent(new Event("input", { bubbles: true }));
              return;
            }
            case "resetView":
              dispatch({
                initialState,
                type: "reset-view",
              });
              return;
            default:
              throw new Error("Unsupported test action " + action.type);
          }
        }

        window.addEventListener("message", (event) => {
          const message = event.data;
          if (!message || message.type !== "diagram.test.run") {
            return;
          }

          try {
            runTestAction(message.action);
            vscode?.postMessage({
              requestId: message.requestId,
              snapshot: createTestSnapshot(),
              type: "diagram.test.snapshot",
            });
          } catch (error) {
            vscode?.postMessage({
              message: error instanceof Error ? error.message : String(error),
              requestId: message.requestId,
              type: "diagram.test.error",
            });
          }
        });
  `;
}
