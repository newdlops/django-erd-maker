import { createCircularDiagramTools } from "../../state/circularDiagram";

export function getBrowserCircularDiagramSource(): string {
  return `
        const circularTools = (${createCircularDiagramTools.toString()})();
        const circularHost = document.querySelector('[data-circular-diagram]');
        const circularPlot = document.querySelector('[data-circular-plot]');
        const circularCanvas = document.querySelector('[data-circular-canvas]');
        let circularOpen = false, circularGraph = null, circularDrag = null, circularFrame = 0;
        let circularCamera = {zoom: 1, panX: 0, panY: 0};
        let circularMetrics = null;
        function isCircularDiagramOpen() {return circularOpen;}
        function openCircularDiagram() {
          if (!circularHost) return;
          if (isRelatedDiagramOpen()) closeRelatedDiagram();
          if (!circularGraph) {
            circularGraph = circularTools.build(inspectorModelById.values());
            const picker = circularHost.querySelector('[data-circular-model]');
            picker.innerHTML = '<option value="">All models</option>' + circularGraph.nodes.map(node =>
              '<option value="' + relationshipExplorer.escape(node.modelId) + '">' + relationshipExplorer.escape(node.modelId) + '</option>').join('');
          }
          circularOpen = true;
          applyState(); circularPlot?.focus({preventScroll: true});
        }
        function closeCircularDiagram() {
          circularOpen = false; circularDrag = null;
          revealedRelationshipEdgeId = ''; connectionOverviewViewport = null;
          if (circularFrame) {window.cancelAnimationFrame(circularFrame); circularFrame = 0;}
          applyState(); document.querySelector('[data-circular-open]')?.focus({preventScroll: true});
        }
        function selectCircularModel(modelId) {
          if (modelId && !inspectorModelById.has(modelId)) return;
          revealedRelationshipEdgeId = '';
          dispatch(modelId ? {type: 'select-model', modelId} : {type: 'clear-selection'});
        }
        function selectCircularEdge(edgeId) {
          const edge = relationshipByEdgeId.get(edgeId);
          if (!edge) return;
          if (![edge.sourceModelId, edge.targetModelId].includes(state.selectedModelId)) selectCircularModel(edge.sourceModelId);
          revealedRelationshipEdgeId = edgeId; applyState();
        }
        function ensureCircularModelVisible(modelId) {
          if (!circularMetrics || !circularGraph) return;
          const node = circularGraph.nodes.find(node => node.modelId === modelId);
          if (!node) return;
          const m = circularMetrics, x = m.cx + node.x * m.scale, y = m.cy + node.y * m.scale;
          if (x >= 32 && x <= m.width - 32 && y >= 32 && y <= m.height - 32) return;
          circularCamera.panX = -node.x * m.scale; circularCamera.panY = -node.y * m.scale;
          scheduleCircularDraw();
        }
        function scheduleCircularDraw() {
          if (circularFrame) return;
          circularFrame = window.requestAnimationFrame(() => {circularFrame = 0; drawCircularDiagram();});
        }
        function zoomCircularDiagram(factor, x, y) {
          if (!circularMetrics) return;
          const old = circularCamera.zoom, zoom = Math.max(0.5, Math.min(32, old * factor)), ratio = zoom / old;
          const anchorX = x ?? circularMetrics.width / 2, anchorY = y ?? circularMetrics.height / 2;
          circularCamera.panX = anchorX - circularMetrics.width / 2 - (anchorX - circularMetrics.cx) * ratio;
          circularCamera.panY = anchorY - circularMetrics.height / 2 - (anchorY - circularMetrics.cy) * ratio;
          circularCamera.zoom = zoom;
          for (const button of circularHost.querySelectorAll('[data-circular-zoom]')) button.disabled = button.dataset.circularZoom === 'in' ? zoom >= 32 : zoom <= 0.5;
          scheduleCircularDraw();
        }
        function renderCircularDiagram() {
          if (!circularHost) return false;
          circularHost.hidden = !circularOpen;
          document.querySelector('.erd-stage')?.classList.toggle('is-circular-diagram', circularOpen);
          for (const button of document.querySelectorAll('[data-circular-open]')) button.setAttribute('aria-pressed', String(circularOpen));
          if (!circularOpen) return false;
          canvas.hidden = true;
          document.querySelector('[data-related-diagram]').hidden = true;
          document.querySelector('[data-connection-context]').hidden = true;
          const graph = circularGraph;
          const model = inspectorModelById.get(state.selectedModelId);
          const edge = getPreviewRelationship();
          circularHost.querySelector('[data-circular-count]').textContent = graph.nodes.length.toLocaleString('en-US') + ' models · '
            + graph.relationshipCount.toLocaleString('en-US') + ' connections · ' + graph.apps.length + ' apps'
            + (graph.unavailableCount ? ' · ' + graph.unavailableCount + ' unavailable endpoints' : '');
          const picker = circularHost.querySelector('[data-circular-model]');
          picker.value = model?.modelId || ''; picker.disabled = !graph.nodes.length;
          circularHost.querySelector('[data-circular-clear]').disabled = !model && !edge;
          for (const button of circularHost.querySelectorAll('[data-circular-zoom]')) button.disabled = !graph.nodes.length
            || (button.dataset.circularZoom === 'in' ? circularCamera.zoom >= 32 : circularCamera.zoom <= 0.5);
          circularHost.querySelector('[data-circular-fit]').disabled = !graph.nodes.length;
          circularHost.querySelector('[data-circular-empty]').hidden = Boolean(graph.nodes.length);
          const center = circularHost.querySelector('[data-circular-center]');
          center.hidden = !graph.nodes.length;
          center.innerHTML = '<strong>' + relationshipExplorer.escape(model?.modelName || 'All models') + '</strong><span>'
            + relationshipExplorer.escape(model ? model.appLabel + ' · ' + (model.relationships?.length || 0) + ' connections' : 'Select a model to trace its connections') + '</span>';
          circularHost.querySelector('[data-circular-status]').textContent = edge
            ? edge.sourceModelId + '.' + edge.fieldName + ' → ' + edge.targetModelId + ' · ' + relationshipExplorer.kindLabel(edge.kind)
            : model ? model.modelId + ' · ' + (model.relationships?.length ? 'Connected curves are highlighted. Fields are listed in the model panel.' : 'This model has no declared connections.')
            : graph.nodes.length ? 'Every model remains on the ring. Search or select a model to inspect its connections.' : 'No models are available in this diagram.';
          drawCircularDiagram(); return true;
        }
        function drawCircularDiagram() {
          if (!circularOpen || !circularCanvas || !circularPlot || !circularGraph) return;
          const rect = circularPlot.getBoundingClientRect(), width = rect.width, height = rect.height;
          if (!width || !height) return;
          const context = circularCanvas.getContext('2d');
          circularHost.querySelector('[data-circular-error]').hidden = Boolean(context);
          if (!context) return;
          const device = Math.min(2, window.devicePixelRatio || 1);
          const pixelWidth = Math.max(1, Math.round(width * device)), pixelHeight = Math.max(1, Math.round(height * device));
          if (circularCanvas.width !== pixelWidth || circularCanvas.height !== pixelHeight) {circularCanvas.width = pixelWidth; circularCanvas.height = pixelHeight;}
          context.setTransform(device, 0, 0, device, 0, 0); context.clearRect(0, 0, width, height);
          const graph = circularGraph, radius = graph.radius;
          const margin = Math.min(90, Math.min(width, height) * 0.18);
          const scale = Math.max(0.0001, Math.min(width - margin * 2, height - margin * 2) / (2 * radius)) * circularCamera.zoom;
          const cx = width / 2 + circularCamera.panX, cy = height / 2 + circularCamera.panY;
          circularMetrics = {width, height, scale, cx, cy};
          const tokens = getComputedStyle(root), text = tokens.getPropertyValue('--text').trim(), muted = tokens.getPropertyValue('--muted').trim();
          const accent = tokens.getPropertyValue('--accent').trim(), amber = tokens.getPropertyValue('--accent-2').trim();
          const colors = [accent, '#82bdff', amber, '#c6a4ff'];
          const selected = state.selectedModelId, connected = new Set();
          const activeEdges = graph.edges.filter(edge => edge.sourceModelId === selected || edge.targetModelId === selected);
          for (const edge of activeEdges) {connected.add(edge.sourceModelId); connected.add(edge.targetModelId);}
          context.translate(cx, cy); context.scale(scale, scale);
          const curve = (edge, emphasized) => {
            const points = edge.points, chosen = edge.edgeId === revealedRelationshipEdgeId;
            const color = relationshipColor([edge.kind], 1);
            context.strokeStyle = chosen ? amber : 'rgb(' + color.slice(0, 3).map(value => Math.round(value * 255)).join(',') + ')';
            context.globalAlpha = chosen ? 1 : emphasized ? 0.82 : selected ? 0.07 : graph.edges.length > 200 ? 0.16 : 0.45;
            context.lineWidth = (chosen ? 2.6 : emphasized ? 1.6 : 0.8) / scale;
            context.beginPath(); context.moveTo(points[0].x, points[0].y);
            context.bezierCurveTo(points[1].x, points[1].y, points[2].x, points[2].y, points[3].x, points[3].y); context.stroke();
            if (emphasized || chosen || graph.edges.length < 60) {
              const tip = circularTools.point(points, 0.94), previous = circularTools.point(points, 0.90);
              const angle = Math.atan2(tip.y - previous.y, tip.x - previous.x), size = 5 / scale;
              context.beginPath(); context.moveTo(tip.x - Math.cos(angle - 0.5) * size, tip.y - Math.sin(angle - 0.5) * size);
              context.lineTo(tip.x, tip.y); context.lineTo(tip.x - Math.cos(angle + 0.5) * size, tip.y - Math.sin(angle + 0.5) * size); context.stroke();
            }
          };
          for (const edge of graph.edges) if (!selected || edge.sourceModelId !== selected && edge.targetModelId !== selected) curve(edge, false);
          for (const edge of activeEdges) curve(edge, true);
          context.globalAlpha = 1;
          const spacing = 2 * Math.PI * radius * scale / Math.max(1, graph.nodes.length);
          for (let i = 0; i < graph.apps.length; i++) {
            const app = graph.apps[i], gap = Math.min(0.025, (app.end - app.start) * 0.08);
            context.strokeStyle = colors[i % colors.length]; context.lineWidth = 3 / scale;
            context.globalAlpha = 0.6; context.beginPath(); context.arc(0, 0, radius - 4 / scale, app.start + gap, app.end - gap); context.stroke();
            if ((app.end - app.start) * radius * scale > 65) {
              const angle = (app.start + app.end) / 2;
              context.save(); context.translate(Math.cos(angle) * (radius - 21 / scale), Math.sin(angle) * (radius - 21 / scale));
              context.scale(1 / scale, 1 / scale); context.globalAlpha = 1; context.fillStyle = muted;
              context.font = '11px ' + tokens.fontFamily; context.textAlign = 'center';
              context.fillText(app.appLabel.length > 20 ? app.appLabel.slice(0, 19) + '…' : app.appLabel, 0, 0); context.restore();
            }
          }
          context.globalAlpha = 1;
          const appIndices = new Map(graph.apps.map((app, index) => [app.appLabel, index]));
          for (const node of graph.nodes) {
            const chosen = node.modelId === selected, peer = connected.has(node.modelId);
            const bar = 6 + Math.min(20, Math.log2(node.degree + 1) * 3);
            context.strokeStyle = chosen ? amber : peer ? accent : colors[appIndices.get(node.appLabel) % colors.length];
            context.globalAlpha = selected && !chosen && !peer ? 0.45 : 1;
            context.lineWidth = Math.max(1, Math.min(chosen ? 10 : 7, spacing * 0.66)) / scale;
            context.beginPath(); context.moveTo(node.x, node.y);
            context.lineTo(Math.cos(node.angle) * (radius + bar / scale), Math.sin(node.angle) * (radius + bar / scale)); context.stroke();
            if (spacing >= 14 || chosen) {
              context.save(); context.translate(Math.cos(node.angle) * (radius + (bar + 7) / scale), Math.sin(node.angle) * (radius + (bar + 7) / scale));
              const left = Math.cos(node.angle) < -0.001;
              context.rotate(node.angle + (left ? Math.PI : 0)); context.scale(1 / scale, 1 / scale);
              context.globalAlpha = 1; context.fillStyle = chosen ? amber : selected && !peer ? muted : text; context.font = (chosen ? '600 ' : '') + '12px ' + tokens.fontFamily;
              context.textAlign = left ? 'right' : 'left'; context.textBaseline = 'middle';
              const nx = Math.cos(node.angle), ny = Math.sin(node.angle);
              const sx = cx + nx * (radius * scale + bar + 7), sy = cy + ny * (radius * scale + bar + 7);
              const available = Math.min(145, Math.abs(nx) < 0.001 ? Infinity : (nx > 0 ? width - sx - 8 : sx - 8) / Math.abs(nx),
                Math.abs(ny) < 0.001 ? Infinity : (ny > 0 ? height - sy - 8 : sy - 8) / Math.abs(ny));
              if (sx >= 8 && sx <= width - 8 && sy >= 8 && sy <= height - 8 && available >= 18) {
                let label = node.modelName;
                if (context.measureText(label).width > available) {
                  while (label.length && context.measureText(label + '…').width > available) label = label.slice(0, -1);
                  label += '…';
                }
                context.fillText(label, 0, 0);
              }
              context.restore();
            }
          }
          context.globalAlpha = 1;
          const center = circularHost.querySelector('[data-circular-center]');
          center.style.left = cx + 'px'; center.style.top = cy + 'px';
          center.hidden = !graph.nodes.length || radius * scale < 90 || circularCamera.zoom > 2.5;
        }
        function handleCircularAction(button) {
          if (button.matches('[data-circular-open]')) {circularOpen ? closeCircularDiagram() : openCircularDiagram(); return true;}
          if (button.matches('[data-circular-close]')) {closeCircularDiagram(); return true;}
          if (button.matches('[data-circular-clear]')) {selectCircularModel(''); return true;}
          if (button.matches('[data-circular-fit]')) {circularCamera = {zoom: 1, panX: 0, panY: 0}; renderCircularDiagram(); return true;}
          if (button.matches('[data-circular-zoom]')) {zoomCircularDiagram(button.dataset.circularZoom === 'in' ? 1.3 : 1 / 1.3); return true;}
          return false;
        }
        if (circularHost && circularPlot) {
          circularHost.querySelector('[data-circular-model]').addEventListener('change', event => selectCircularModel(event.target.value));
          circularPlot.addEventListener('pointerdown', event => {
            if (event.button !== 0) return;
            circularPlot.focus({preventScroll: true}); capturePointer(circularPlot, event.pointerId);
            circularDrag = {id: event.pointerId, x: event.clientX, y: event.clientY, panX: circularCamera.panX, panY: circularCamera.panY, moved: false};
          });
          circularPlot.addEventListener('pointermove', event => {
            if (!circularDrag) {
              if (!circularMetrics) return;
              const rect = circularPlot.getBoundingClientRect(), m = circularMetrics;
              const x = (event.clientX - rect.left - m.cx) / m.scale, y = (event.clientY - rect.top - m.cy) / m.scale;
              const node = circularGraph.nodes.find(node => Math.hypot(x - node.x, y - node.y) < 12 / m.scale);
              circularPlot.style.cursor = node ? 'pointer' : '';
              circularPlot.title = node ? node.modelId + ' · ' + node.degree + ' connections' : '';
              return;
            }
            if (circularDrag.id !== event.pointerId) return;
            const dx = event.clientX - circularDrag.x, dy = event.clientY - circularDrag.y;
            if (Math.hypot(dx, dy) > 4) circularDrag.moved = true;
            if (!circularDrag.moved) return;
            circularCamera.panX = circularDrag.panX + dx; circularCamera.panY = circularDrag.panY + dy;
            circularPlot.classList.add('is-panning'); scheduleCircularDraw();
          });
          const endCircularPointer = event => {
            const drag = circularDrag;
            if (!drag || drag.id !== event.pointerId) return;
            circularDrag = null; releasePointer(circularPlot, event.pointerId); circularPlot.classList.remove('is-panning');
            if (drag.moved || event.type !== 'pointerup' || !circularMetrics) return;
            const rect = circularPlot.getBoundingClientRect(), metrics = circularMetrics;
            const location = {x: (event.clientX - rect.left - metrics.cx) / metrics.scale, y: (event.clientY - rect.top - metrics.cy) / metrics.scale};
            let closest = null, distance = 14 / metrics.scale;
            for (const node of circularGraph.nodes) {const d = Math.hypot(location.x - node.x, location.y - node.y); if (d < distance) {closest = node; distance = d;}}
            if (closest) {selectCircularModel(closest.modelId); return;}
            const edges = state.selectedModelId ? circularGraph.edges.filter(edge => [edge.sourceModelId, edge.targetModelId].includes(state.selectedModelId)) : circularGraph.edges;
            const edge = circularTools.hitEdge(edges, location, 7 / metrics.scale);
            if (edge) selectCircularEdge(edge.edgeId);
          };
          circularPlot.addEventListener('pointerup', endCircularPointer); circularPlot.addEventListener('pointercancel', endCircularPointer);
          circularPlot.addEventListener('lostpointercapture', endCircularPointer);
          circularPlot.addEventListener('wheel', event => {
            event.preventDefault(); const rect = circularPlot.getBoundingClientRect();
            zoomCircularDiagram(Math.exp(-Math.max(-200, Math.min(200, event.deltaY)) * 0.002), event.clientX - rect.left, event.clientY - rect.top);
          }, {passive: false});
          circularPlot.addEventListener('keydown', event => {
            if (event.key === 'Escape') {event.preventDefault(); closeCircularDiagram(); return;}
            if (['+', '=', '-', '_'].includes(event.key)) {event.preventDefault(); zoomCircularDiagram(['+', '='].includes(event.key) ? 1.3 : 1 / 1.3); return;}
            if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End'].includes(event.key)) return;
            event.preventDefault();
            if (event.shiftKey) {
              circularCamera.panX += event.key === 'ArrowLeft' ? 60 : event.key === 'ArrowRight' ? -60 : 0;
              circularCamera.panY += event.key === 'ArrowUp' ? 60 : event.key === 'ArrowDown' ? -60 : 0;
              scheduleCircularDraw(); return;
            }
            const nodes = circularGraph.nodes;
            if (!nodes.length) return;
            const index = nodes.findIndex(node => node.modelId === state.selectedModelId);
            const next = event.key === 'Home' ? 0 : event.key === 'End' ? nodes.length - 1
              : (index + (['ArrowLeft', 'ArrowUp'].includes(event.key) ? -1 : 1) + nodes.length) % nodes.length;
            selectCircularModel(nodes[next].modelId);
          });
        }
  `;
}
