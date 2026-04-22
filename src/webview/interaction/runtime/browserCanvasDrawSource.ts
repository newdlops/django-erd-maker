export function getBrowserCanvasDrawSource(): string {
  return `
        function drawCanvas() {
          resizeDrawingCanvas();
          drawingContext.clearRect(0, 0, drawingCanvas.width, drawingCanvas.height);
          drawingContext.save();
          drawingContext.translate(state.viewport.panX, state.viewport.panY);
          drawingContext.scale(state.viewport.zoom, state.viewport.zoom);
          drawEdges();
          drawMethodOverlays();
          drawCrossings();
          drawTables();
          drawingContext.restore();
        }

        function drawCrossings() {
          drawingContext.save();
          drawingContext.strokeStyle = "rgba(255, 191, 105, 0.92)";
          drawingContext.fillStyle = "#0c141c";
          drawingContext.lineWidth = 1.4;
          for (const crossing of renderedCrossings) {
            drawingContext.beginPath();
            drawingContext.arc(crossing.position.x, crossing.position.y, 6.5, 0, Math.PI * 2);
            drawingContext.fill();
            drawingContext.stroke();
            drawingContext.beginPath();
            drawingContext.moveTo(crossing.position.x - 7, crossing.position.y);
            drawingContext.quadraticCurveTo(
              crossing.position.x,
              crossing.position.y - 7,
              crossing.position.x + 7,
              crossing.position.y,
            );
            drawingContext.stroke();
          }
          drawingContext.restore();
        }

        function drawEdges() {
          for (const edge of renderedEdges) {
            drawingContext.save();
            drawingContext.strokeStyle = edge.meta.cssKind.includes("many-to-many")
              ? "#f7d18a"
              : edge.meta.provenance === "derived_reverse"
                ? "#9dcfe1"
                : "#b6e7d9";
            drawingContext.lineWidth = 2.3;
            drawingContext.lineCap = "round";
            drawingContext.lineJoin = "round";
            if (edge.meta.provenance === "derived_reverse") {
              drawingContext.setLineDash([7, 6]);
            }
            drawPolyline(edge.points);
            drawingContext.restore();
          }
        }

        function drawMethodOverlays() {
          for (const overlay of renderedOverlays) {
            if (!overlay.active) {
              continue;
            }

            drawingContext.save();
            drawingContext.strokeStyle = "rgba(255, 191, 105, 0.72)";
            drawingContext.lineWidth = 3;
            drawingContext.setLineDash([10, 8]);
            drawingContext.beginPath();
            drawingContext.moveTo(overlay.x1, overlay.y1);
            drawingContext.lineTo(overlay.x2, overlay.y2);
            drawingContext.stroke();
            drawingContext.restore();
          }
        }

        function drawTables() {
          for (const [modelId, meta] of tableMetaById.entries()) {
            const options = getTableOptions(state, modelId);
            if (options.hidden) {
              continue;
            }

            const table = tableRenderById.get(modelId);
            if (!table) {
              continue;
            }

            const position = getCurrentPosition(modelId);
            drawTableFrame(position, meta, table, options);
            drawModelTableContent(position, meta, table);
          }
        }

        function drawTableFrame(position, meta, table, options) {
          const selected = state.selectedModelId === table.modelId;
          const methodTarget = isMethodTarget(table.modelId);
          const dragging = drag && drag.kind === "table" && drag.modelId === table.modelId;

          drawingContext.save();
          drawingContext.shadowColor = "rgba(0, 0, 0, 0.3)";
          drawingContext.shadowBlur = 28;
          drawingContext.shadowOffsetY = 14;
          drawingContext.fillStyle = "#0f1e2c";
          drawingContext.strokeStyle = dragging
            ? "rgba(168, 216, 255, 0.72)"
            : methodTarget
              ? "rgba(255, 191, 105, 0.72)"
              : selected
                ? "rgba(109, 208, 176, 0.62)"
                : "rgba(123, 196, 170, 0.26)";
          drawingContext.lineWidth = selected || methodTarget || dragging ? 2.2 : 1.4;
          drawRoundRect(position.x, position.y, meta.width, meta.height, 7);
          drawingContext.fill();
          drawingContext.stroke();
          drawingContext.shadowColor = "transparent";
          drawingContext.fillStyle = selected ? "rgba(109, 208, 176, 0.16)" : "rgba(123, 196, 170, 0.11)";
          drawingContext.save();
          drawRoundRect(position.x, position.y, meta.width, meta.height, 7);
          drawingContext.clip();
          drawingContext.fillRect(position.x + 1, position.y + 1, meta.width - 2, 32);
          drawingContext.restore();
          drawingContext.strokeStyle = "rgba(154, 184, 177, 0.22)";
          drawingContext.lineWidth = 1;
          drawingContext.beginPath();
          drawingContext.moveTo(position.x, position.y + 33);
          drawingContext.lineTo(position.x + meta.width, position.y + 33);
          drawingContext.stroke();
          drawingContext.restore();
        }

        function drawModelTableContent(position, meta, table) {
          const tableName = table.databaseTableName || meta.tableName || table.modelName;
          const modelName = table.modelName || meta.modelName || table.modelId;
          drawingContext.save();
          drawingContext.textAlign = "left";
          drawingContext.textBaseline = "middle";
          drawFittedText(modelName, position.x + 12, position.y + 17, meta.width - 24, "#f1f7f4", "700 13px system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif");
          drawFittedText(tableName, position.x + 12, position.y + 55, meta.width - 24, "#9fb7b0", "500 12px system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif");
          drawingContext.restore();
        }

        function drawPolyline(points) {
          if (points.length === 0) {
            return;
          }

          drawingContext.beginPath();
          drawingContext.moveTo(points[0].x, points[0].y);
          for (const point of points.slice(1)) {
            drawingContext.lineTo(point.x, point.y);
          }
          drawingContext.stroke();
        }

        function drawRoundRect(x, y, width, height, radius) {
          drawingContext.beginPath();
          if (drawingContext.roundRect) {
            drawingContext.roundRect(x, y, width, height, radius);
            return;
          }

          drawingContext.moveTo(x + radius, y);
          drawingContext.lineTo(x + width - radius, y);
          drawingContext.quadraticCurveTo(x + width, y, x + width, y + radius);
          drawingContext.lineTo(x + width, y + height - radius);
          drawingContext.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
          drawingContext.lineTo(x + radius, y + height);
          drawingContext.quadraticCurveTo(x, y + height, x, y + height - radius);
          drawingContext.lineTo(x, y + radius);
          drawingContext.quadraticCurveTo(x, y, x + radius, y);
          drawingContext.closePath();
        }

        function drawText(text, x, y, color, font) {
          drawingContext.fillStyle = color;
          drawingContext.font = font;
          drawingContext.fillText(String(text), x, y);
        }

        function drawFittedText(text, x, y, maxWidth, color, font) {
          const value = String(text);
          drawingContext.fillStyle = color;
          drawingContext.font = font;
          if (drawingContext.measureText(value).width <= maxWidth) {
            drawingContext.fillText(value, x, y);
            return;
          }

          let truncated = value;
          while (truncated.length > 1 && drawingContext.measureText(truncated + "…").width > maxWidth) {
            truncated = truncated.slice(0, -1);
          }
          drawingContext.fillText(truncated.length > 1 ? truncated + "…" : "…", x, y);
        }

        function resizeDrawingCanvas() {
          const rect = canvas.getBoundingClientRect();
          if (rect.width <= 0 || rect.height <= 0) {
            return;
          }

          const deviceScale = window.devicePixelRatio || 1;
          const width = Math.max(1, Math.round(rect.width * deviceScale));
          const height = Math.max(1, Math.round(rect.height * deviceScale));
          if (drawingCanvas.width !== width || drawingCanvas.height !== height) {
            drawingCanvas.width = width;
            drawingCanvas.height = height;
          }
          drawingContext.setTransform(deviceScale, 0, 0, deviceScale, 0, 0);
        }
  `;
}
