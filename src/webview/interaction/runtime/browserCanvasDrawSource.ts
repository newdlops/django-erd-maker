export function getBrowserCanvasDrawSource(): string {
  return `
        const GPU_TILE_SIZE = 960;
        const GPU_TABLE_LABEL_ZOOM = 0.16;
        const GPU_TABLE_SUBTITLE_ZOOM = 0.24;
        const GPU_TABLE_DETAIL_ZOOM = 0.58;
        const GPU_TABLE_DETAIL_LIMIT = 56;
        const GPU_LABEL_ATLAS_SIZE = 2048;
        const GPU_MAX_SEGMENTS_PER_FRAME = 240000;
        const GPU_MAX_LABELS_PER_FRAME = 2200;
        const GPU_EDGE_LOD_DISABLE_ZOOM = 0.58;
        const GPU_EDGE_LOD_DENSE_SEGMENTS = 7000;
        const GPU_EDGE_LOD_OVERVIEW_ZOOM = 0.38;
        const GPU_EDGE_LOD_OVERVIEW_TARGET = 5600;
        const GPU_EDGE_LOD_LOW_ZOOM_TARGET = 3600;
        const GPU_EDGE_LOD_MID_ZOOM_TARGET = 7600;
        const GPU_DENSE_LABEL_ZOOM = 0.42;
        const GPU_DENSE_LABEL_TABLE_LIMIT = 520;
        const RENDER_FRAME_SAMPLE_MS = 1000;
        const RENDER_STATS_INTERVAL_MS = 1000;
        const RENDER_STATS_MIN_FRAMES = 60;
        const WEBGPU_UNIFORM_BYTES = 32;

        let gpuRenderer = null;
        let lastRenderFrameEndedAt = 0;
        let lastRenderFrameSampleAt = 0;
        let latestLiveDragEdgeCount = 0;
        let latestLiveDragSegmentCount = 0;
        let latestEdgeLodStats = createEmptyEdgeLodStats();
        let renderFrameSequence = 0;
        let renderStats = createEmptyRenderStats(performance.now());
        let sceneGraph = null;
        let latestCatalogCrossings = [];

        function detectGpuSupport() {
          const hasWebgl2 = typeof window.WebGL2RenderingContext === "function";
          const hasWebgpu = typeof navigator !== "undefined" && Boolean(navigator.gpu);

          return {
            hasWebgl2,
            hasWebgpu,
            reason:
              "This ERD view requires WebGL2 or WebGPU support. Update the VS Code host or enable GPU acceleration.",
            supported: hasWebgl2 || hasWebgpu,
          };
        }

        function showGpuUnsupportedWarning(reason) {
          if (!gpuWarning) {
            return;
          }

          gpuWarning.hidden = false;
          const message = gpuWarning.querySelector("[data-erd-gpu-warning-message]");
          if (message) {
            message.textContent = reason;
          }
        }

        function invalidateCatalogSceneCache() {
          invalidateSceneGraph();
        }

        function invalidateSceneGraph() {
          sceneGraph = null;
          renderedEdges = [];
          renderedCrossings = [];
          latestCatalogCrossings = [];
        }

        async function createGpuRenderer(gpuSupport) {
          if (gpuSupport.hasWebgpu) {
            try {
              const webGpuRenderer = await createWebGpuRenderer();
              if (webGpuRenderer) {
                return webGpuRenderer;
              }
            } catch (error) {
              console.warn("WebGPU renderer initialization failed; falling back to WebGL2.", error);
              logErd("warn", "renderer.webgpu.fallback", {
                reason: error instanceof Error ? error.message : String(error),
                renderer: "webgl2",
              });
            }
          }

          if (!gpuSupport.hasWebgl2) {
            return null;
          }

          return createWebGl2Renderer();
        }

        async function createWebGpuRenderer() {
          if (!navigator.gpu) {
            return null;
          }

          const adapter = await navigator.gpu.requestAdapter({
            powerPreference: "high-performance",
          });
          if (!adapter) {
            return null;
          }

          const device = await adapter.requestDevice();
          device.pushErrorScope("validation");
          const format = navigator.gpu.getPreferredCanvasFormat();
          const commonBindGroupLayout = device.createBindGroupLayout({
            entries: [
              {
                binding: 0,
                buffer: { type: "uniform" },
                visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
              },
            ],
          });
          const spriteBindGroupLayout = device.createBindGroupLayout({
            entries: [
              {
                binding: 0,
                sampler: { type: "filtering" },
                visibility: GPUShaderStage.FRAGMENT,
              },
              {
                binding: 1,
                texture: { sampleType: "float" },
                visibility: GPUShaderStage.FRAGMENT,
              },
            ],
          });
          const renderer = {
            atlas: createWebGpuLabelAtlas(device),
            backend: "webgpu",
            commonBindGroupLayout,
            context: null,
            device,
            format,
            segment: createWebGpuSegmentPipeline(device, format, commonBindGroupLayout),
            sprite: createWebGpuSpritePipeline(
              device,
              format,
              commonBindGroupLayout,
              spriteBindGroupLayout,
            ),
            spriteBindGroupLayout,
            table: createWebGpuTablePipeline(device, format, commonBindGroupLayout),
            uniformBuffer: device.createBuffer({
              size: WEBGPU_UNIFORM_BYTES,
              usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.UNIFORM,
            }),
          };

          renderer.commonBindGroup = device.createBindGroup({
            entries: [
              {
                binding: 0,
                resource: {
                  buffer: renderer.uniformBuffer,
                },
              },
            ],
            layout: commonBindGroupLayout,
          });
          renderer.spriteBindGroup = createWebGpuSpriteBindGroup(renderer);
          const validationError = await device.popErrorScope();
          if (validationError) {
            logErd("warn", "renderer.webgpu.validation_failed", {
              message: validationError.message,
              renderer: "webgpu",
            });
            return null;
          }

          bindWebGpuDiagnostics(renderer);
          const context = drawingCanvas.getContext("webgpu");
          if (!context) {
            return null;
          }

          renderer.context = context;
          configureWebGpuCanvas(renderer);
          return renderer;
        }

        function bindWebGpuDiagnostics(renderer) {
          renderer.device.addEventListener("uncapturederror", (event) => {
            logErd("error", "renderer.webgpu.error", {
              message: event.error ? event.error.message : String(event),
              renderer: "webgpu",
            });
          });

          renderer.device.lost.then((info) => {
            logErd("error", "renderer.webgpu.lost", {
              message: info.message || "",
              reason: info.reason || "unknown",
              renderer: "webgpu",
            });
          });
        }

        function createWebGl2Renderer() {
          const gl = drawingCanvas.getContext("webgl2", {
            alpha: false,
            antialias: true,
            depth: false,
            desynchronized: true,
            powerPreference: "high-performance",
            preserveDrawingBuffer: false,
            stencil: false,
          });
          if (!gl) {
            return null;
          }

          const tableProgram = createProgram(gl, tableVertexShaderSource(), tableFragmentShaderSource());
          const segmentProgram = createProgram(gl, segmentVertexShaderSource(), segmentFragmentShaderSource());
          const spriteProgram = createProgram(gl, spriteVertexShaderSource(), spriteFragmentShaderSource());
          if (!tableProgram || !segmentProgram || !spriteProgram) {
            return null;
          }

          const renderer = {
            atlas: createLabelAtlas(gl),
            backend: "webgl2",
            gl,
            segment: {
              buffers: {
                corners: createStaticBuffer(gl, new Float32Array([0, -1, 1, -1, 0, 1, 1, 1])),
                instances: gl.createBuffer(),
              },
              program: segmentProgram,
            },
            sprite: {
              buffers: {
                corners: createStaticBuffer(gl, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1])),
                instances: gl.createBuffer(),
              },
              program: spriteProgram,
            },
            table: {
              buffers: {
                corners: createStaticBuffer(gl, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1])),
                instances: gl.createBuffer(),
              },
              program: tableProgram,
            },
          };

          gl.disable(gl.DEPTH_TEST);
          gl.enable(gl.BLEND);
          gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
          return renderer;
        }

        function configureWebGpuCanvas(renderer) {
          renderer.context.configure({
            alphaMode: "opaque",
            device: renderer.device,
            format: renderer.format,
            usage: GPUTextureUsage.RENDER_ATTACHMENT,
          });
        }

        function createWebGpuLabelAtlas(device) {
          const canvas = document.createElement("canvas");
          canvas.width = GPU_LABEL_ATLAS_SIZE;
          canvas.height = GPU_LABEL_ATLAS_SIZE;
          const context = canvas.getContext("2d", {
            alpha: true,
            colorSpace: "srgb",
            willReadFrequently: true,
          });
          if (!context) {
            return null;
          }

          const texture = device.createTexture({
            format: "rgba8unorm",
            size: [canvas.width, canvas.height],
            usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING,
          });
          const sampler = device.createSampler({
            addressModeU: "clamp-to-edge",
            addressModeV: "clamp-to-edge",
            magFilter: "linear",
            minFilter: "linear",
          });

          return {
            canvas,
            context,
            map: new Map(),
            nextX: 8,
            nextY: 8,
            rowHeight: 0,
            sampler,
            texture,
          };
        }

        function createWebGpuTablePipeline(device, format, commonBindGroupLayout) {
          const pipelineLayout = device.createPipelineLayout({
            bindGroupLayouts: [commonBindGroupLayout],
          });

          return {
            corners: createWebGpuStaticBuffer(device, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1])),
            instanceBuffer: null,
            instanceBytes: 0,
            pipeline: device.createRenderPipeline({
              fragment: {
                entryPoint: "fs",
                module: device.createShaderModule({ code: webGpuTableShaderSource() }),
                targets: [createWebGpuBlendTarget(format)],
              },
              layout: pipelineLayout,
              primitive: { topology: "triangle-strip" },
              vertex: {
                buffers: [
                  {
                    arrayStride: 8,
                    attributes: [{ format: "float32x2", offset: 0, shaderLocation: 0 }],
                    stepMode: "vertex",
                  },
                  {
                    arrayStride: 56,
                    attributes: [
                      { format: "float32x4", offset: 0, shaderLocation: 1 },
                      { format: "float32x4", offset: 16, shaderLocation: 2 },
                      { format: "float32x4", offset: 32, shaderLocation: 3 },
                      { format: "float32x2", offset: 48, shaderLocation: 4 },
                    ],
                    stepMode: "instance",
                  },
                ],
                entryPoint: "vs",
                module: device.createShaderModule({ code: webGpuTableShaderSource() }),
              },
            }),
          };
        }

        function createWebGpuSegmentPipeline(device, format, commonBindGroupLayout) {
          const pipelineLayout = device.createPipelineLayout({
            bindGroupLayouts: [commonBindGroupLayout],
          });

          return {
            corners: createWebGpuStaticBuffer(device, new Float32Array([0, -1, 1, -1, 0, 1, 1, 1])),
            instanceBuffer: null,
            instanceBytes: 0,
            pipeline: device.createRenderPipeline({
              fragment: {
                entryPoint: "fs",
                module: device.createShaderModule({ code: webGpuSegmentShaderSource() }),
                targets: [createWebGpuBlendTarget(format)],
              },
              layout: pipelineLayout,
              primitive: { topology: "triangle-strip" },
              vertex: {
                buffers: [
                  {
                    arrayStride: 8,
                    attributes: [{ format: "float32x2", offset: 0, shaderLocation: 0 }],
                    stepMode: "vertex",
                  },
                  {
                    arrayStride: 48,
                    attributes: [
                      { format: "float32x4", offset: 0, shaderLocation: 1 },
                      { format: "float32", offset: 16, shaderLocation: 2 },
                      { format: "float32x4", offset: 32, shaderLocation: 3 },
                    ],
                    stepMode: "instance",
                  },
                ],
                entryPoint: "vs",
                module: device.createShaderModule({ code: webGpuSegmentShaderSource() }),
              },
            }),
          };
        }

        function createWebGpuSpritePipeline(
          device,
          format,
          commonBindGroupLayout,
          spriteBindGroupLayout,
        ) {
          const pipelineLayout = device.createPipelineLayout({
            bindGroupLayouts: [commonBindGroupLayout, spriteBindGroupLayout],
          });

          return {
            corners: createWebGpuStaticBuffer(device, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1])),
            instanceBuffer: null,
            instanceBytes: 0,
            pipeline: device.createRenderPipeline({
              fragment: {
                entryPoint: "fs",
                module: device.createShaderModule({ code: webGpuSpriteShaderSource() }),
                targets: [createWebGpuBlendTarget(format)],
              },
              layout: pipelineLayout,
              primitive: { topology: "triangle-strip" },
              vertex: {
                buffers: [
                  {
                    arrayStride: 8,
                    attributes: [{ format: "float32x2", offset: 0, shaderLocation: 0 }],
                    stepMode: "vertex",
                  },
                  {
                    arrayStride: 36,
                    attributes: [
                      { format: "float32x4", offset: 0, shaderLocation: 1 },
                      { format: "float32x4", offset: 16, shaderLocation: 2 },
                      { format: "float32", offset: 32, shaderLocation: 3 },
                    ],
                    stepMode: "instance",
                  },
                ],
                entryPoint: "vs",
                module: device.createShaderModule({ code: webGpuSpriteShaderSource() }),
              },
            }),
          };
        }

        function createWebGpuBlendTarget(format) {
          return {
            blend: {
              alpha: {
                dstFactor: "one-minus-src-alpha",
                operation: "add",
                srcFactor: "one",
              },
              color: {
                dstFactor: "one-minus-src-alpha",
                operation: "add",
                srcFactor: "src-alpha",
              },
            },
            format,
          };
        }

        function createWebGpuSpriteBindGroup(renderer) {
          if (!renderer.atlas) {
            return null;
          }

          return renderer.device.createBindGroup({
            entries: [
              {
                binding: 0,
                resource: renderer.atlas.sampler,
              },
              {
                binding: 1,
                resource: renderer.atlas.texture.createView(),
              },
            ],
            layout: renderer.spriteBindGroupLayout,
          });
        }

        function createWebGpuStaticBuffer(device, data) {
          const buffer = device.createBuffer({
            mappedAtCreation: true,
            size: alignWebGpuBufferSize(data.byteLength),
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.VERTEX,
          });
          new Float32Array(buffer.getMappedRange()).set(data);
          buffer.unmap();
          return buffer;
        }

        function ensureWebGpuInstanceBuffer(device, target, byteLength) {
          const requiredBytes = alignWebGpuBufferSize(Math.max(4, byteLength));
          if (!target.instanceBuffer || target.instanceBytes < requiredBytes) {
            target.instanceBuffer = device.createBuffer({
              size: requiredBytes,
              usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.VERTEX,
            });
            target.instanceBytes = requiredBytes;
          }

          return target.instanceBuffer;
        }

        function alignWebGpuBufferSize(byteLength) {
          return Math.max(4, Math.ceil(byteLength / 4) * 4);
        }

        function createLabelAtlas(gl) {
          const canvas = document.createElement("canvas");
          canvas.width = GPU_LABEL_ATLAS_SIZE;
          canvas.height = GPU_LABEL_ATLAS_SIZE;
          const context = canvas.getContext("2d", {
            alpha: true,
            colorSpace: "srgb",
            willReadFrequently: true,
          });
          if (!context) {
            return null;
          }

          const texture = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, texture);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
          gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.RGBA,
            canvas.width,
            canvas.height,
            0,
            gl.RGBA,
            gl.UNSIGNED_BYTE,
            null,
          );

          return {
            canvas,
            context,
            map: new Map(),
            nextX: 8,
            nextY: 8,
            rowHeight: 0,
            texture,
          };
        }

        function createProgram(gl, vertexSource, fragmentSource) {
          const vertexShader = createShader(gl, gl.VERTEX_SHADER, vertexSource);
          const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentSource);
          if (!vertexShader || !fragmentShader) {
            return null;
          }

          const program = gl.createProgram();
          gl.attachShader(program, vertexShader);
          gl.attachShader(program, fragmentShader);
          gl.linkProgram(program);
          gl.deleteShader(vertexShader);
          gl.deleteShader(fragmentShader);

          if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            console.warn(gl.getProgramInfoLog(program) || "GPU program link failed.");
            gl.deleteProgram(program);
            return null;
          }

          return program;
        }

        function createShader(gl, type, source) {
          const shader = gl.createShader(type);
          gl.shaderSource(shader, source);
          gl.compileShader(shader);

          if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            console.warn(gl.getShaderInfoLog(shader) || "GPU shader compile failed.");
            gl.deleteShader(shader);
            return null;
          }

          return shader;
        }

        function createStaticBuffer(gl, data) {
          const buffer = gl.createBuffer();
          gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
          gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
          return buffer;
        }

        function ensureSceneGraph() {
          if (sceneGraph) {
            return sceneGraph;
          }

          const startedAt = performance.now();
          const nextScene = {
            edgeBuckets: new Map(),
            edgeSegments: [],
            tableBuckets: new Map(),
            tables: [],
            tablesById: new Map(),
          };

          for (const [modelId, meta] of tableMetaById.entries()) {
            if (!isVisibleModel(modelId)) {
              continue;
            }

            const position = getCurrentPosition(modelId);
            const record = {
              height: meta.height,
              maxX: position.x + meta.width,
              maxY: position.y + meta.height,
              meta,
              modelId,
              options: getTableOptions(state, modelId),
              table: tableRenderById.get(modelId),
              width: meta.width,
              x: position.x,
              y: position.y,
            };

            nextScene.tables.push(record);
            nextScene.tablesById.set(modelId, record);
            addToBuckets(
              nextScene.tableBuckets,
              {
                bottom: record.maxY,
                left: record.x,
                right: record.maxX,
                top: record.y,
              },
              modelId,
            );
          }

          const visibleEdgeEntries = [];
          for (const meta of edgeMeta) {
            const sourceTable = tableMetaById.get(meta.sourceModelId);
            const targetTable = tableMetaById.get(meta.targetModelId);
            if (!sourceTable || !targetTable || !isVisibleModel(meta.sourceModelId) || !isVisibleModel(meta.targetModelId)) {
              continue;
            }

            visibleEdgeEntries.push({
              meta,
              sourcePosition: getCurrentPosition(meta.sourceModelId),
              sourceTable,
              targetPosition: getCurrentPosition(meta.targetModelId),
              targetTable,
            });
          }

          renderedEdges = renderModel.modelCatalogMode
            ? getStaticOrCatalogEdgePaths(visibleEdgeEntries)
            : visibleEdgeEntries.map((entry) => ({
                edgeId: entry.meta.edgeId,
                meta: entry.meta,
                points: getStaticOrLiveEdgePath(entry),
              }));

          for (const edge of renderedEdges) {
            for (const segment of findSegments(edge.points)) {
              const visibleSegments = clipSegmentAgainstTables(segment, edge.meta, nextScene);
              for (const visibleSegment of visibleSegments) {
                const segmentIndex = nextScene.edgeSegments.length;
                const bounds = {
                  bottom: Math.max(visibleSegment.start.y, visibleSegment.end.y),
                  left: Math.min(visibleSegment.start.x, visibleSegment.end.x),
                  right: Math.max(visibleSegment.start.x, visibleSegment.end.x),
                  top: Math.min(visibleSegment.start.y, visibleSegment.end.y),
                };

                nextScene.edgeSegments.push({
                  bounds,
                  edgeId: edge.edgeId,
                  meta: edge.meta,
                  points: edge.points,
                  segment: visibleSegment,
                  segmentIndex,
                });
                addToBuckets(nextScene.edgeBuckets, bounds, segmentIndex);
              }
            }
          }

          renderedCrossings = [];
          sceneGraph = nextScene;
          logErdDuration("info", "scene.graph.built", startedAt, {
            edgeSegments: nextScene.edgeSegments.length,
            renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            tables: nextScene.tables.length,
          });
          return sceneGraph;
        }

        function addToBuckets(buckets, bounds, value) {
          const range = getBucketRange(bounds);

          for (let row = range.startRow; row <= range.endRow; row += 1) {
            for (let column = range.startColumn; column <= range.endColumn; column += 1) {
              const key = column + ":" + row;
              if (!buckets.has(key)) {
                buckets.set(key, []);
              }
              buckets.get(key).push(value);
            }
          }
        }

        function getBucketRange(bounds) {
          return {
            endColumn: Math.floor(bounds.right / GPU_TILE_SIZE),
            endRow: Math.floor(bounds.bottom / GPU_TILE_SIZE),
            startColumn: Math.floor(bounds.left / GPU_TILE_SIZE),
            startRow: Math.floor(bounds.top / GPU_TILE_SIZE),
          };
        }

        function clipSegmentAgainstTables(segment, meta, scene) {
          const vertical = Math.abs(segment.start.x - segment.end.x) < 0.01;
          const horizontal = Math.abs(segment.start.y - segment.end.y) < 0.01;
          if (!vertical && !horizontal) {
            return [segment];
          }

          const intervals = [];
          const padding = 5;
          const sourceModelId = meta.sourceModelId || "";
          const targetModelId = meta.targetModelId || "";

          if (vertical) {
            const x = segment.start.x;
            const minY = Math.min(segment.start.y, segment.end.y);
            const maxY = Math.max(segment.start.y, segment.end.y);
            const tables = collectClipCandidateTables(scene, {
              bottom: maxY + padding,
              left: x - padding,
              right: x + padding,
              top: minY - padding,
            });
            for (const table of tables) {
              if (table.modelId === sourceModelId || table.modelId === targetModelId) {
                continue;
              }

              const left = table.x - padding;
              const right = table.x + table.width + padding;
              if (x <= left || x >= right) {
                continue;
              }

              const top = table.y - padding;
              const bottom = table.y + table.height + padding;
              if (bottom <= minY || top >= maxY) {
                continue;
              }
              intervals.push({ end: Math.min(maxY, bottom), start: Math.max(minY, top) });
            }

            return createClippedAxisSegments(
              segment.start.y,
              segment.end.y,
              intervals,
              (start, end) => ({
                end: { x, y: end },
                start: { x, y: start },
              }),
            );
          }

          const y = segment.start.y;
          const minX = Math.min(segment.start.x, segment.end.x);
          const maxX = Math.max(segment.start.x, segment.end.x);
          const tables = collectClipCandidateTables(scene, {
            bottom: y + padding,
            left: minX - padding,
            right: maxX + padding,
            top: y - padding,
          });
          for (const table of tables) {
            if (table.modelId === sourceModelId || table.modelId === targetModelId) {
              continue;
            }

            const top = table.y - padding;
            const bottom = table.y + table.height + padding;
            if (y <= top || y >= bottom) {
              continue;
            }

            const left = table.x - padding;
            const right = table.x + table.width + padding;
            if (right <= minX || left >= maxX) {
              continue;
            }
            intervals.push({ end: Math.min(maxX, right), start: Math.max(minX, left) });
          }

          return createClippedAxisSegments(
            segment.start.x,
            segment.end.x,
            intervals,
            (start, end) => ({
              end: { x: end, y },
              start: { x: start, y },
            }),
          );
        }

        function collectClipCandidateTables(scene, bounds) {
          return collectBucketValues(scene.tableBuckets, bounds)
            .map((modelId) => scene.tablesById.get(modelId))
            .filter(Boolean);
        }

        function createClippedAxisSegments(axisStart, axisEnd, intervals, createSegment) {
          if (!intervals.length) {
            return [createSegment(axisStart, axisEnd)];
          }

          const reversed = axisEnd < axisStart;
          const minAxis = Math.min(axisStart, axisEnd);
          const maxAxis = Math.max(axisStart, axisEnd);
          const sorted = intervals
            .filter((interval) => interval.end - interval.start > 1)
            .sort((left, right) => left.start - right.start || left.end - right.end);
          const segments = [];
          let cursor = minAxis;

          for (const interval of sorted) {
            const start = Math.max(minAxis, interval.start);
            const end = Math.min(maxAxis, interval.end);
            if (end <= cursor) {
              continue;
            }
            if (start - cursor > 1) {
              segments.push(
                reversed
                  ? createSegment(start, cursor)
                  : createSegment(cursor, start),
              );
            }
            cursor = Math.max(cursor, end);
          }

          if (maxAxis - cursor > 1) {
            segments.push(
              reversed
                ? createSegment(maxAxis, cursor)
                : createSegment(cursor, maxAxis),
            );
          }

          return segments;
        }

        function queryTableMetaNearWorldPoint(point) {
          const scene = ensureSceneGraph();
          const ids = collectBucketValues(scene.tableBuckets, {
            bottom: point.y,
            left: point.x,
            right: point.x,
            top: point.y,
          });

          return ids
            .map((modelId) => scene.tablesById.get(modelId))
            .filter(Boolean)
            .sort((left, right) => right.y - left.y || right.x - left.x);
        }

        function collectBucketValues(buckets, bounds) {
          const values = new Set();
          const range = getBucketRange(bounds);

          for (let row = range.startRow; row <= range.endRow; row += 1) {
            for (let column = range.startColumn; column <= range.endColumn; column += 1) {
              const bucket = buckets.get(column + ":" + row);
              if (!bucket) {
                continue;
              }

              for (const value of bucket) {
                values.add(value);
              }
            }
          }

          return Array.from(values);
        }

        function drawCanvas(renderMode) {
          if (!gpuRenderer) {
            return;
          }

          const startedAt = performance.now();
          const resizeStartedAt = performance.now();
          resizeDrawingCanvas();
          const resizeMs = performance.now() - resizeStartedAt;
          const sceneStartedAt = performance.now();
          const scene = ensureSceneGraph();
          const sceneMs = performance.now() - sceneStartedAt;
          const cullStartedAt = performance.now();
          const visibleBounds = getVisibleWorldBounds(96);
          const visibleTables = collectVisibleTables(scene, visibleBounds);
          const visibleSegments = collectVisibleSegments(scene, visibleBounds);
          const visibleOverlays = collectVisibleOverlaySegments(visibleBounds);
          const cullMs = performance.now() - cullStartedAt;
          const labelStartedAt = performance.now();
          const labels = collectVisibleLabels(visibleTables);
          const labelMs = performance.now() - labelStartedAt;
          const drawStartedAt = performance.now();

          if (gpuRenderer.backend === "webgpu") {
            drawWebGpuScene(gpuRenderer, visibleSegments, visibleOverlays, visibleTables, labels);
          } else {
            clearGpuScene(gpuRenderer);
            drawSegmentBatch(gpuRenderer, visibleSegments, false);
            drawSegmentBatch(gpuRenderer, visibleOverlays, true);
            drawTableBatch(gpuRenderer, visibleTables);
            drawLabelBatch(gpuRenderer, labels);
          }

          logRenderFrame(
            startedAt,
            renderMode,
            scene,
            visibleTables,
            visibleSegments,
            visibleOverlays,
            labels,
            {
              cullMs,
              drawMs: performance.now() - drawStartedAt,
              labelMs,
              resizeMs,
              sceneMs,
            },
          );
        }

        function createEmptyRenderStats(startedAt) {
          return {
            fullFrames: 0,
            maxMs: 0,
            slowFrames: 0,
            startedAt,
            totalFrames: 0,
            totalMs: 0,
            viewportFrames: 0,
          };
        }

        function recordRenderStats(durationMs, renderMode, endedAt) {
          renderStats.totalFrames += 1;
          renderStats.totalMs += durationMs;
          renderStats.maxMs = Math.max(renderStats.maxMs, durationMs);
          if (durationMs >= ERD_LOG_SLOW_RENDER_MS) {
            renderStats.slowFrames += 1;
          }

          if (renderMode === "full") {
            renderStats.fullFrames += 1;
          } else if (renderMode === "viewport") {
            renderStats.viewportFrames += 1;
          }

          const elapsedMs = endedAt - renderStats.startedAt;
          if (
            renderStats.totalFrames < RENDER_STATS_MIN_FRAMES &&
            elapsedMs < RENDER_STATS_INTERVAL_MS
          ) {
            return;
          }

          const totalFrames = renderStats.totalFrames;
          logErd("info", "render.stats", {
            avgFrameMs: round2(renderStats.totalMs / Math.max(1, totalFrames)),
            elapsedMs: round2(elapsedMs),
            fps: round2((totalFrames / Math.max(1, elapsedMs)) * 1000),
            fullFrames: renderStats.fullFrames,
            maxFrameMs: round2(renderStats.maxMs),
            renderer: gpuRenderer.backend,
            slowFrames: renderStats.slowFrames,
            totalFrames,
            viewportFrames: renderStats.viewportFrames,
          });
          renderStats = createEmptyRenderStats(endedAt);
        }

        function getRenderFrameLogReason(renderMode, durationMs, endedAt) {
          if (renderMode === "full") {
            return "full";
          }

          if (durationMs >= ERD_LOG_SLOW_RENDER_MS) {
            return "slow";
          }

          if (endedAt - lastRenderFrameSampleAt >= RENDER_FRAME_SAMPLE_MS) {
            lastRenderFrameSampleAt = endedAt;
            return "sample";
          }

          return "";
        }

        function logRenderFrame(
          startedAt,
          renderMode,
          scene,
          visibleTables,
          visibleSegments,
          visibleOverlays,
          labels,
          timings,
        ) {
          const endedAt = performance.now();
          const durationMs = round2(endedAt - startedAt);
          const sinceLastFrameMs = lastRenderFrameEndedAt
            ? round2(endedAt - lastRenderFrameEndedAt)
            : null;
          const fps = sinceLastFrameMs && sinceLastFrameMs > 0
            ? round2(1000 / sinceLastFrameMs)
            : null;
          const frameId = renderFrameSequence + 1;
          const reason = getRenderFrameLogReason(renderMode, durationMs, endedAt);

          renderFrameSequence = frameId;
          lastRenderFrameEndedAt = endedAt;
          recordRenderStats(durationMs, renderMode, endedAt);

          if (!reason) {
            return;
          }

          logErd(durationMs >= ERD_LOG_SLOW_RENDER_MS ? "warn" : "info", "render.frame", {
            cullMs: round2(timings.cullMs),
            durationMs,
            drawMs: round2(timings.drawMs),
            fps,
            frameId,
            labelMs: round2(timings.labelMs),
            liveDragEdges: latestLiveDragEdgeCount,
            liveDragSegments: latestLiveDragSegmentCount,
            canvasHeight: drawingCanvas.height,
            canvasWidth: drawingCanvas.width,
            labels: labels.length,
            mode: renderMode || "unknown",
            overlaySegments: visibleOverlays.length,
            panX: round2(state.viewport.panX),
            panY: round2(state.viewport.panY),
            reason,
            renderer: gpuRenderer.backend,
            resizeMs: round2(timings.resizeMs),
            sceneMs: round2(timings.sceneMs),
            edgeLod: latestEdgeLodStats.applied,
            edgeLodLimit: latestEdgeLodStats.limit,
            edgeLodSkippedSegments: latestEdgeLodStats.skippedSegments,
            edgeLodSourceSegments: latestEdgeLodStats.sourceSegments,
            segments: visibleSegments.length,
            sinceLastFrameMs,
            tables: visibleTables.length,
            totalSegments: scene.edgeSegments.length,
            totalTables: scene.tables.length,
            zoom: round2(state.viewport.zoom),
          });
        }

        function collectVisibleTables(scene, bounds) {
          const ids = collectBucketValues(scene.tableBuckets, bounds);

          const records = ids
            .map((modelId) => scene.tablesById.get(modelId))
            .filter((record) =>
              record &&
              rectIntersectsBounds(record.x, record.y, record.width, record.height, bounds, 0),
            )
            .sort((left, right) => left.y - right.y || left.x - right.x);

          return applyLiveDragTableRecord(scene, records, bounds);
        }

        function collectVisibleSegments(scene, bounds) {
          const records = collectBucketValues(scene.edgeBuckets, bounds)
            .slice(0, GPU_MAX_SEGMENTS_PER_FRAME)
            .map((segmentIndex) => scene.edgeSegments[segmentIndex])
            .filter((record) =>
              record &&
              segmentIntersectsBounds(
                record.segment.start.x,
                record.segment.start.y,
                record.segment.end.x,
                record.segment.end.y,
                bounds,
                80,
              ),
            );

          return applyLiveDragEdgeSegments(applyEdgeSegmentLod(records), bounds);
        }

        function createEmptyEdgeLodStats() {
          return {
            applied: false,
            limit: 0,
            skippedSegments: 0,
            sourceSegments: 0,
          };
        }

        function getEdgeSegmentLodLimit(segmentCount, zoom) {
          if (segmentCount <= GPU_EDGE_LOD_DENSE_SEGMENTS || zoom >= GPU_EDGE_LOD_DISABLE_ZOOM) {
            return segmentCount;
          }

          if (zoom < 0.24) {
            return Math.min(segmentCount, GPU_EDGE_LOD_LOW_ZOOM_TARGET);
          }

          if (zoom < GPU_EDGE_LOD_OVERVIEW_ZOOM) {
            return Math.min(segmentCount, GPU_EDGE_LOD_OVERVIEW_TARGET);
          }

          return Math.min(segmentCount, GPU_EDGE_LOD_MID_ZOOM_TARGET);
        }

        function isImportantEdgeSegment(record) {
          const selectedModelId = state.selectedModelId || "";
          if (
            selectedModelId &&
            (record.meta.sourceModelId === selectedModelId ||
              record.meta.targetModelId === selectedModelId)
          ) {
            return true;
          }

          const selectedMethod = state.selectedMethodContext;
          return Boolean(
            selectedMethod &&
              record.meta.sourceModelId === selectedMethod.modelId &&
              record.meta.methodName === selectedMethod.methodName,
          );
        }

        function groupVisibleEdgeSegments(records) {
          const groupsByEdgeId = new Map();

          for (const record of records) {
            const edgeId = record.edgeId || "segment:" + String(record.segmentIndex || 0);
            let group = groupsByEdgeId.get(edgeId);
            if (!group) {
              group = {
                important: false,
                records: [],
              };
              groupsByEdgeId.set(edgeId, group);
            }

            group.important = group.important || isImportantEdgeSegment(record);
            group.records.push(record);
          }

          return Array.from(groupsByEdgeId.values());
        }

        function sampleEdgeSegmentGroups(groups, segmentBudget) {
          if (segmentBudget <= 0 || !groups.length) {
            return [];
          }

          const totalSegments = groups.reduce((total, group) => total + group.records.length, 0);
          if (totalSegments <= segmentBudget) {
            return groups.flatMap((group) => group.records);
          }

          const stride = Math.max(1, Math.ceil(totalSegments / segmentBudget));
          const sampled = [];
          const usedGroups = new Set();

          for (let offset = 0; offset < stride && sampled.length < segmentBudget; offset += 1) {
            for (let index = offset; index < groups.length && sampled.length < segmentBudget; index += stride) {
              if (usedGroups.has(index)) {
                continue;
              }

              const group = groups[index];
              if (sampled.length > 0 && sampled.length + group.records.length > segmentBudget) {
                continue;
              }

              sampled.push(...group.records);
              usedGroups.add(index);
            }
          }

          return sampled;
        }

        function applyEdgeSegmentLod(records) {
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const limit = getEdgeSegmentLodLimit(records.length, zoom);

          if (limit >= records.length) {
            latestEdgeLodStats = {
              applied: false,
              limit,
              skippedSegments: 0,
              sourceSegments: records.length,
            };
            return records;
          }

          const importantRecords = [];
          const regularGroups = [];

          for (const group of groupVisibleEdgeSegments(records)) {
            if (group.important) {
              importantRecords.push(...group.records);
            } else {
              regularGroups.push(group);
            }
          }

          const remainingLimit = Math.max(0, limit - importantRecords.length);
          const sampledRecords = sampleEdgeSegmentGroups(regularGroups, remainingLimit);
          const nextRecords = importantRecords.concat(sampledRecords);

          latestEdgeLodStats = {
            applied: true,
            limit,
            skippedSegments: Math.max(0, records.length - nextRecords.length),
            sourceSegments: records.length,
          };
          return nextRecords;
        }

        function getActiveTableDrag() {
          return drag && drag.kind === "table" && drag.currentPosition ? drag : null;
        }

        function applyLiveDragTableRecord(scene, records, bounds) {
          const activeDrag = getActiveTableDrag();
          if (!activeDrag) {
            return records;
          }

          const baseRecord = scene.tablesById.get(activeDrag.modelId);
          if (!baseRecord) {
            return records;
          }

          const position = activeDrag.currentPosition;
          const liveRecord = {
            ...baseRecord,
            maxX: position.x + baseRecord.width,
            maxY: position.y + baseRecord.height,
            options: getTableOptions(state, activeDrag.modelId),
            x: position.x,
            y: position.y,
          };
          const nextRecords = records.filter((record) => record.modelId !== activeDrag.modelId);
          if (
            rectIntersectsBounds(
              liveRecord.x,
              liveRecord.y,
              liveRecord.width,
              liveRecord.height,
              bounds,
              0,
            )
          ) {
            nextRecords.push(liveRecord);
          }

          return nextRecords.sort((left, right) => left.y - right.y || left.x - right.x);
        }

        function applyLiveDragEdgeSegments(records, bounds) {
          const activeDrag = getActiveTableDrag();
          if (!activeDrag) {
            latestLiveDragEdgeCount = 0;
            latestLiveDragSegmentCount = 0;
            return records;
          }

          const filteredRecords = records.filter(
            (record) =>
              record.meta.sourceModelId !== activeDrag.modelId &&
              record.meta.targetModelId !== activeDrag.modelId,
          );

          const liveRecords = collectLiveDragEdgeSegments(activeDrag, bounds);
          latestLiveDragSegmentCount = liveRecords.length;
          return filteredRecords.concat(liveRecords);
        }

        function collectLiveDragEdgeSegments(activeDrag, bounds) {
          const visibleEdgeEntries = [];
          for (const meta of edgeMeta) {
            if (
              meta.sourceModelId !== activeDrag.modelId &&
              meta.targetModelId !== activeDrag.modelId
            ) {
              continue;
            }

            const sourceTable = tableMetaById.get(meta.sourceModelId);
            const targetTable = tableMetaById.get(meta.targetModelId);
            if (
              !sourceTable ||
              !targetTable ||
              !isVisibleModel(meta.sourceModelId) ||
              !isVisibleModel(meta.targetModelId)
            ) {
              continue;
            }

            visibleEdgeEntries.push({
              meta,
              sourcePosition:
                meta.sourceModelId === activeDrag.modelId
                  ? activeDrag.currentPosition
                  : getCurrentPosition(meta.sourceModelId),
              sourceTable,
              targetPosition:
                meta.targetModelId === activeDrag.modelId
                  ? activeDrag.currentPosition
                  : getCurrentPosition(meta.targetModelId),
              targetTable,
            });
          }
          latestLiveDragEdgeCount = visibleEdgeEntries.length;

          const routedEdges = renderModel.modelCatalogMode
            ? routeCatalogEdgesWithPorts(visibleEdgeEntries).map((routed) => ({
                edgeId: routed.entry.meta.edgeId,
                meta: routed.entry.meta,
                points: routed.points,
              }))
            : visibleEdgeEntries.map((entry) => ({
                edgeId: entry.meta.edgeId,
                meta: entry.meta,
                points: buildOrthogonalPath(
                  entry.sourcePosition,
                  entry.sourceTable,
                  entry.targetPosition,
                  entry.targetTable,
                ),
              }));
          const records = [];

          for (const edge of routedEdges) {
            for (const segment of findSegments(edge.points)) {
              if (
                !segmentIntersectsBounds(
                  segment.start.x,
                  segment.start.y,
                  segment.end.x,
                  segment.end.y,
                  bounds,
                  80,
                )
              ) {
                continue;
              }

              records.push({
                bounds: {
                  bottom: Math.max(segment.start.y, segment.end.y),
                  left: Math.min(segment.start.x, segment.end.x),
                  right: Math.max(segment.start.x, segment.end.x),
                  top: Math.min(segment.start.y, segment.end.y),
                },
                edgeId: edge.edgeId,
                meta: edge.meta,
                points: edge.points,
                segment,
              });
            }
          }

          return records;
        }

        function collectVisibleOverlaySegments(bounds) {
          return renderedOverlays
            .filter((overlay) => overlay.active)
            .map((overlay) => ({
              meta: { cssKind: "method-overlay", provenance: "overlay" },
              segment: {
                end: { x: overlay.x2, y: overlay.y2 },
                start: { x: overlay.x1, y: overlay.y1 },
              },
            }))
            .filter((record) =>
              segmentIntersectsBounds(
                record.segment.start.x,
                record.segment.start.y,
                record.segment.end.x,
                record.segment.end.y,
                bounds,
                80,
              ),
            );
        }

        function collectVisibleLabels(visibleTables) {
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          if (zoom < GPU_TABLE_LABEL_ZOOM) {
            return [];
          }

          const labels = [];
          const labelTables = getLabelTableRecords(visibleTables, zoom);
          const allowDetails = zoom >= GPU_TABLE_DETAIL_ZOOM && labelTables.length <= GPU_TABLE_DETAIL_LIMIT;

          for (const record of labelTables) {
            const table = record.table;
            if (!table) {
              continue;
            }

            labels.push(createLabelDescriptor(table.modelName, "700 14px Georgia, serif", "#f4f7f1", record.x + 14, record.y + 14, Math.max(40, record.width - 28)));
            if (zoom >= GPU_TABLE_SUBTITLE_ZOOM) {
              labels.push(createLabelDescriptor(table.databaseTableName, "500 12px Georgia, serif", "#9fb7b0", record.x + 14, record.y + 34, Math.max(40, record.width - 28)));
            }

            if (!allowDetails) {
              continue;
            }

            let cursorY = record.y + 56;
            for (const row of record.meta.fieldRows) {
              if (cursorY + 16 > record.y + record.height - 12) {
                break;
              }
              labels.push(createLabelDescriptor(row.text, "500 12px Georgia, serif", row.tone === "enum-option" ? "#e4d3a7" : "#c7d7d4", record.x + 14, cursorY, Math.max(40, record.width - 28)));
              cursorY += 16;
            }

            if (record.options.showProperties) {
              for (const property of record.meta.properties) {
                if (cursorY + 16 > record.y + record.height - 12) {
                  break;
                }
                labels.push(createLabelDescriptor("@ " + property, "500 12px Georgia, serif", "#a8d8ff", record.x + 14, cursorY, Math.max(40, record.width - 28)));
                cursorY += 16;
              }
            }

            if (record.options.showMethods) {
              for (const method of record.meta.methods) {
                if (cursorY + 16 > record.y + record.height - 12) {
                  break;
                }
                labels.push(createLabelDescriptor("fn " + method.name, "500 12px Georgia, serif", "#ffcf8a", record.x + 14, cursorY, Math.max(40, record.width - 28)));
                cursorY += 16;
              }
            }

            if (labels.length >= GPU_MAX_LABELS_PER_FRAME) {
              return labels;
            }
          }

          return labels;
        }

        function getLabelTableRecords(visibleTables, zoom) {
          if (
            zoom >= GPU_DENSE_LABEL_ZOOM ||
            visibleTables.length <= GPU_DENSE_LABEL_TABLE_LIMIT
          ) {
            return visibleTables;
          }

          const importantRecords = [];
          const regularRecords = [];
          for (const record of visibleTables) {
            if (
              state.selectedModelId === record.modelId ||
              (state.selectedMethodContext && state.selectedMethodContext.modelId === record.modelId)
            ) {
              importantRecords.push(record);
            } else {
              regularRecords.push(record);
            }
          }

          const remainingLimit = Math.max(0, GPU_DENSE_LABEL_TABLE_LIMIT - importantRecords.length);
          if (regularRecords.length <= remainingLimit) {
            return importantRecords.concat(regularRecords);
          }

          const step = regularRecords.length / Math.max(1, remainingLimit);
          const sampledRecords = [];
          for (let index = 0; index < remainingLimit; index += 1) {
            sampledRecords.push(regularRecords[Math.floor(index * step)]);
          }

          return importantRecords.concat(sampledRecords);
        }

        function createLabelDescriptor(text, font, color, x, y, maxWidth) {
          return { color, font, maxWidth, text, x, y };
        }

        function drawWebGpuScene(renderer, segments, overlays, tables, labels) {
          const device = renderer.device;
          const validateDraw = (renderer.drawValidationChecks || 0) < 3;

          if (validateDraw) {
            renderer.drawValidationChecks = (renderer.drawValidationChecks || 0) + 1;
            device.pushErrorScope("validation");
          }

          try {
            updateWebGpuCommonUniforms(renderer);
            const encoder = device.createCommandEncoder();
            const pass = encoder.beginRenderPass({
              colorAttachments: [
                {
                  clearValue: { a: 1, b: 0.055, g: 0.035, r: 0.020 },
                  loadOp: "clear",
                  storeOp: "store",
                  view: renderer.context.getCurrentTexture().createView(),
                },
              ],
            });

            drawWebGpuSegmentBatch(renderer, pass, segments, false);
            drawWebGpuSegmentBatch(renderer, pass, overlays, true);
            drawWebGpuTableBatch(renderer, pass, tables);
            drawWebGpuLabelBatch(renderer, pass, labels);
            pass.end();
            device.queue.submit([encoder.finish()]);
          } catch (error) {
            logErd("error", "renderer.webgpu.draw_failed", {
              message: error instanceof Error ? error.message : String(error),
              renderer: "webgpu",
            });
          } finally {
            if (validateDraw) {
              device.popErrorScope()
                .then((error) => {
                  if (!error) {
                    return;
                  }

                  logErd("error", "renderer.webgpu.draw_validation_failed", {
                    message: error.message,
                    renderer: "webgpu",
                  });
                })
                .catch((error) => {
                  logErd("error", "renderer.webgpu.draw_validation_scope_failed", {
                    message: error instanceof Error ? error.message : String(error),
                    renderer: "webgpu",
                  });
                });
            }
          }
        }

        function updateWebGpuCommonUniforms(renderer) {
          renderer.device.queue.writeBuffer(
            renderer.uniformBuffer,
            0,
            new Float32Array([
              drawingCanvas.width,
              drawingCanvas.height,
              state.viewport.panX,
              state.viewport.panY,
              Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM),
              getDeviceScale(),
              0,
              0,
            ]),
          );
        }

        function drawWebGpuSegmentBatch(renderer, pass, segments, overlay) {
          if (!segments.length) {
            return;
          }

          const data = new Float32Array(segments.length * 12);
          for (let index = 0; index < segments.length; index += 1) {
            const record = segments[index];
            const color = overlay ? [0.98, 0.81, 0.54, 0.52] : edgeColor(record.meta);
            const width = overlay ? 1.3 : edgeWidth(record.meta);
            const offset = index * 12;

            data[offset + 0] = record.segment.start.x;
            data[offset + 1] = record.segment.start.y;
            data[offset + 2] = record.segment.end.x;
            data[offset + 3] = record.segment.end.y;
            data[offset + 4] = width;
            data[offset + 8] = color[0];
            data[offset + 9] = color[1];
            data[offset + 10] = color[2];
            data[offset + 11] = color[3];
          }

          const buffer = ensureWebGpuInstanceBuffer(renderer.device, renderer.segment, data.byteLength);
          renderer.device.queue.writeBuffer(buffer, 0, data);
          pass.setPipeline(renderer.segment.pipeline);
          pass.setBindGroup(0, renderer.commonBindGroup);
          pass.setVertexBuffer(0, renderer.segment.corners);
          pass.setVertexBuffer(1, buffer);
          pass.draw(4, segments.length);
        }

        function drawWebGpuTableBatch(renderer, pass, tables) {
          if (!tables.length) {
            return;
          }

          const data = new Float32Array(tables.length * 14);
          for (let index = 0; index < tables.length; index += 1) {
            const record = tables[index];
            const colors = tableColors(record);
            const offset = index * 14;

            data[offset + 0] = record.x;
            data[offset + 1] = record.y;
            data[offset + 2] = record.width;
            data[offset + 3] = record.height;
            data[offset + 4] = colors.fill[0];
            data[offset + 5] = colors.fill[1];
            data[offset + 6] = colors.fill[2];
            data[offset + 7] = colors.fill[3];
            data[offset + 8] = colors.stroke[0];
            data[offset + 9] = colors.stroke[1];
            data[offset + 10] = colors.stroke[2];
            data[offset + 11] = colors.stroke[3];
            data[offset + 12] = 16;
            data[offset + 13] = colors.borderWidth;
          }

          const buffer = ensureWebGpuInstanceBuffer(renderer.device, renderer.table, data.byteLength);
          renderer.device.queue.writeBuffer(buffer, 0, data);
          pass.setPipeline(renderer.table.pipeline);
          pass.setBindGroup(0, renderer.commonBindGroup);
          pass.setVertexBuffer(0, renderer.table.corners);
          pass.setVertexBuffer(1, buffer);
          pass.draw(4, tables.length);
        }

        function drawWebGpuLabelBatch(renderer, pass, labels) {
          if (!labels.length || !renderer.atlas || !renderer.spriteBindGroup) {
            return;
          }

          const instances = [];
          for (const label of labels) {
            const entry = ensureAtlasLabel(renderer, label);
            if (!entry) {
              continue;
            }

            instances.push(
              label.x,
              label.y,
              entry.width,
              entry.height,
              entry.u0,
              entry.v0,
              entry.u1,
              entry.v1,
              1,
            );
          }

          if (!instances.length) {
            return;
          }

          const data = new Float32Array(instances);
          const buffer = ensureWebGpuInstanceBuffer(renderer.device, renderer.sprite, data.byteLength);
          renderer.device.queue.writeBuffer(buffer, 0, data);
          pass.setPipeline(renderer.sprite.pipeline);
          pass.setBindGroup(0, renderer.commonBindGroup);
          pass.setBindGroup(1, renderer.spriteBindGroup);
          pass.setVertexBuffer(0, renderer.sprite.corners);
          pass.setVertexBuffer(1, buffer);
          pass.draw(4, instances.length / 9);
        }

        function clearGpuScene(renderer) {
          const gl = renderer.gl;
          gl.viewport(0, 0, drawingCanvas.width, drawingCanvas.height);
          gl.clearColor(0.020, 0.035, 0.055, 1);
          gl.clear(gl.COLOR_BUFFER_BIT);
        }

        function drawSegmentBatch(renderer, segments, overlay) {
          if (!segments.length) {
            return;
          }

          const gl = renderer.gl;
          const data = new Float32Array(segments.length * 9);
          for (let index = 0; index < segments.length; index += 1) {
            const record = segments[index];
            const color = overlay ? [0.98, 0.81, 0.54, 0.52] : edgeColor(record.meta);
            const width = overlay ? 1.3 : edgeWidth(record.meta);
            const offset = index * 9;

            data[offset + 0] = record.segment.start.x;
            data[offset + 1] = record.segment.start.y;
            data[offset + 2] = record.segment.end.x;
            data[offset + 3] = record.segment.end.y;
            data[offset + 4] = width;
            data[offset + 5] = color[0];
            data[offset + 6] = color[1];
            data[offset + 7] = color[2];
            data[offset + 8] = color[3];
          }

          gl.useProgram(renderer.segment.program);
          bindCommonUniforms(gl, renderer.segment.program);
          bindBufferData(gl, renderer.segment.buffers.instances, data);
          bindCornerAttribute(gl, renderer.segment.program, renderer.segment.buffers.corners, "a_corner");
          bindInstancedFloat(gl, renderer.segment.program, renderer.segment.buffers.instances, "a_segment", 4, 9, 0);
          bindInstancedFloat(gl, renderer.segment.program, renderer.segment.buffers.instances, "a_halfWidth", 1, 9, 4);
          bindInstancedFloat(gl, renderer.segment.program, renderer.segment.buffers.instances, "a_color", 4, 9, 5);
          gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, segments.length);
        }

        function drawTableBatch(renderer, tables) {
          if (!tables.length) {
            return;
          }

          const gl = renderer.gl;
          const data = new Float32Array(tables.length * 14);
          for (let index = 0; index < tables.length; index += 1) {
            const record = tables[index];
            const colors = tableColors(record);
            const offset = index * 14;

            data[offset + 0] = record.x;
            data[offset + 1] = record.y;
            data[offset + 2] = record.width;
            data[offset + 3] = record.height;
            data[offset + 4] = colors.fill[0];
            data[offset + 5] = colors.fill[1];
            data[offset + 6] = colors.fill[2];
            data[offset + 7] = colors.fill[3];
            data[offset + 8] = colors.stroke[0];
            data[offset + 9] = colors.stroke[1];
            data[offset + 10] = colors.stroke[2];
            data[offset + 11] = colors.stroke[3];
            data[offset + 12] = 16;
            data[offset + 13] = colors.borderWidth;
          }

          gl.useProgram(renderer.table.program);
          bindCommonUniforms(gl, renderer.table.program);
          bindBufferData(gl, renderer.table.buffers.instances, data);
          bindCornerAttribute(gl, renderer.table.program, renderer.table.buffers.corners, "a_corner");
          bindInstancedFloat(gl, renderer.table.program, renderer.table.buffers.instances, "a_bounds", 4, 14, 0);
          bindInstancedFloat(gl, renderer.table.program, renderer.table.buffers.instances, "a_fill", 4, 14, 4);
          bindInstancedFloat(gl, renderer.table.program, renderer.table.buffers.instances, "a_stroke", 4, 14, 8);
          bindInstancedFloat(gl, renderer.table.program, renderer.table.buffers.instances, "a_style", 2, 14, 12);
          gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, tables.length);
        }

        function drawLabelBatch(renderer, labels) {
          if (!labels.length || !renderer.atlas) {
            return;
          }

          const gl = renderer.gl;
          const instances = [];

          for (const label of labels) {
            const entry = ensureAtlasLabel(renderer, label);
            if (!entry) {
              continue;
            }

            instances.push(
              label.x,
              label.y,
              entry.width,
              entry.height,
              entry.u0,
              entry.v0,
              entry.u1,
              entry.v1,
              1,
            );
          }

          if (!instances.length) {
            return;
          }

          gl.useProgram(renderer.sprite.program);
          bindCommonUniforms(gl, renderer.sprite.program);
          bindBufferData(gl, renderer.sprite.buffers.instances, new Float32Array(instances));
          bindCornerAttribute(gl, renderer.sprite.program, renderer.sprite.buffers.corners, "a_corner");
          bindInstancedFloat(gl, renderer.sprite.program, renderer.sprite.buffers.instances, "a_bounds", 4, 9, 0);
          bindInstancedFloat(gl, renderer.sprite.program, renderer.sprite.buffers.instances, "a_uvBounds", 4, 9, 4);
          bindInstancedFloat(gl, renderer.sprite.program, renderer.sprite.buffers.instances, "a_alpha", 1, 9, 8);
          gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D, renderer.atlas.texture);
          gl.uniform1i(gl.getUniformLocation(renderer.sprite.program, "u_texture"), 0);
          gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, instances.length / 9);
        }

        function ensureAtlasLabel(renderer, label) {
          const atlas = renderer.atlas;
          const key = label.font + "|" + label.color + "|" + trimTextToWidth(atlas.context, label.font, label.text, label.maxWidth);
          if (atlas.map.has(key)) {
            return atlas.map.get(key);
          }

          const text = key.split("|").slice(2).join("|");
          const fontSize = Math.max(12, Number.parseInt(label.font.match(/([0-9]+)px/)?.[1] || "12", 10));
          atlas.context.font = label.font;
          const width = Math.max(2, Math.ceil(atlas.context.measureText(text).width));
          const height = Math.max(14, Math.ceil(fontSize * 1.5));
          const slot = allocateAtlasSlot(atlas, width + 8, height + 8);
          if (!slot) {
            return null;
          }

          atlas.context.clearRect(slot.x, slot.y, slot.width, slot.height);
          atlas.context.font = label.font;
          atlas.context.fillStyle = label.color;
          atlas.context.textBaseline = "top";
          atlas.context.fillText(text, slot.x + 4, slot.y + 4);

          const pixels = atlas.context.getImageData(slot.x, slot.y, slot.width, slot.height);
          if (renderer.backend === "webgpu") {
            writeWebGpuAtlasSlot(renderer, atlas, slot, pixels);
          } else {
            renderer.gl.bindTexture(renderer.gl.TEXTURE_2D, atlas.texture);
            renderer.gl.texSubImage2D(renderer.gl.TEXTURE_2D, 0, slot.x, slot.y, renderer.gl.RGBA, renderer.gl.UNSIGNED_BYTE, pixels);
          }

          const entry = {
            height: height + 2,
            u0: slot.x / atlas.canvas.width,
            u1: (slot.x + slot.width) / atlas.canvas.width,
            v0: slot.y / atlas.canvas.height,
            v1: (slot.y + slot.height) / atlas.canvas.height,
            width: width + 2,
          };
          atlas.map.set(key, entry);
          return entry;
        }

        function writeWebGpuAtlasSlot(renderer, atlas, slot, pixels) {
          const sourceBytesPerRow = slot.width * 4;
          const bytesPerRow = Math.ceil(sourceBytesPerRow / 256) * 256;
          const upload = new Uint8Array(bytesPerRow * slot.height);

          for (let row = 0; row < slot.height; row += 1) {
            const sourceStart = row * sourceBytesPerRow;
            upload.set(
              pixels.data.subarray(sourceStart, sourceStart + sourceBytesPerRow),
              row * bytesPerRow,
            );
          }

          renderer.device.queue.writeTexture(
            {
              origin: { x: slot.x, y: slot.y },
              texture: atlas.texture,
            },
            upload,
            {
              bytesPerRow,
              rowsPerImage: slot.height,
            },
            {
              height: slot.height,
              width: slot.width,
            },
          );
        }

        function allocateAtlasSlot(atlas, width, height) {
          if (atlas.nextX + width > atlas.canvas.width - 8) {
            atlas.nextX = 8;
            atlas.nextY += atlas.rowHeight + 8;
            atlas.rowHeight = 0;
          }

          if (atlas.nextY + height > atlas.canvas.height - 8) {
            atlas.context.clearRect(0, 0, atlas.canvas.width, atlas.canvas.height);
            atlas.map.clear();
            atlas.nextX = 8;
            atlas.nextY = 8;
            atlas.rowHeight = 0;
          }

          const slot = {
            height,
            width,
            x: atlas.nextX,
            y: atlas.nextY,
          };
          atlas.nextX += width + 8;
          atlas.rowHeight = Math.max(atlas.rowHeight, height);
          return slot;
        }

        function trimTextToWidth(context, font, text, maxWidth) {
          context.font = font;
          if (context.measureText(text).width <= maxWidth) {
            return text;
          }

          let trimmed = text;
          while (trimmed.length > 4 && context.measureText(trimmed + "…").width > maxWidth) {
            trimmed = trimmed.slice(0, -1);
          }
          return trimmed + "…";
        }

        function tableColors(record) {
          const selected = state.selectedModelId === record.modelId;
          const methodTarget = isMethodTarget(record.modelId);
          const dragging = drag && drag.kind === "table" && drag.modelId === record.modelId;

          return {
            borderWidth: selected || dragging ? 3.2 : 1.4,
            fill: selected ? [0.15, 0.24, 0.22, 0.98] : [0.06, 0.12, 0.18, 0.96],
            stroke: dragging
              ? [0.66, 0.85, 1.0, 0.9]
              : selected
                ? [1.0, 0.75, 0.41, 0.92]
                : methodTarget
                  ? [0.66, 0.85, 1.0, 0.74]
                  : [0.48, 0.77, 0.67, 0.28],
          };
        }

        function edgeColor(meta) {
          if ((meta.cssKind || "").includes("many-to-many")) {
            return [0.97, 0.82, 0.54, meta.provenance === "derived_reverse" ? 0.52 : 0.72];
          }
          if ((meta.cssKind || "").includes("one-to-one")) {
            return [0.66, 0.85, 1.0, meta.provenance === "derived_reverse" ? 0.48 : 0.68];
          }
          return meta.provenance === "derived_reverse"
            ? [0.62, 0.81, 0.88, 0.44]
            : [0.71, 0.91, 0.85, 0.56];
        }

        function edgeWidth(meta) {
          return (meta.cssKind || "").includes("many-to-many") ? 4.2 : 3.2;
        }

        function bindCommonUniforms(gl, program) {
          gl.uniform2f(gl.getUniformLocation(program, "u_canvas"), drawingCanvas.width, drawingCanvas.height);
          gl.uniform2f(gl.getUniformLocation(program, "u_pan"), state.viewport.panX, state.viewport.panY);
          gl.uniform1f(gl.getUniformLocation(program, "u_zoom"), Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM));
          gl.uniform1f(gl.getUniformLocation(program, "u_deviceScale"), getDeviceScale());
        }

        function bindBufferData(gl, buffer, data) {
          gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
          gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
        }

        function bindCornerAttribute(gl, program, buffer, name) {
          const location = gl.getAttribLocation(program, name);
          gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
          gl.enableVertexAttribArray(location);
          gl.vertexAttribPointer(location, 2, gl.FLOAT, false, 0, 0);
          gl.vertexAttribDivisor(location, 0);
        }

        function bindInstancedFloat(gl, program, buffer, name, size, strideFloats, offsetFloats) {
          const location = gl.getAttribLocation(program, name);
          gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
          gl.enableVertexAttribArray(location);
          gl.vertexAttribPointer(location, size, gl.FLOAT, false, strideFloats * 4, offsetFloats * 4);
          gl.vertexAttribDivisor(location, 1);
        }

        function getViewportRect() {
          const rect = getViewportScreenRect();
          return { height: Math.max(1, rect.height), width: Math.max(1, rect.width) };
        }

        function resizeDrawingCanvas() {
          const viewportRect = getViewportRect();
          const deviceScale = getDeviceScale();
          const width = Math.max(1, Math.round(viewportRect.width * deviceScale));
          const height = Math.max(1, Math.round(viewportRect.height * deviceScale));

          if (drawingCanvas.width !== width || drawingCanvas.height !== height) {
            drawingCanvas.width = width;
            drawingCanvas.height = height;
            if (gpuRenderer && gpuRenderer.backend === "webgpu") {
              configureWebGpuCanvas(gpuRenderer);
            }
          }
        }

        function getDeviceScale() {
          return Math.max(1, window.devicePixelRatio || 1);
        }

        function getVisibleWorldBounds(padding) {
          const viewportRect = getViewportRect();
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          const worldPadding = Number.isFinite(padding) ? padding : 0;

          return {
            bottom: (viewportRect.height - state.viewport.panY) / zoom + worldPadding,
            left: -state.viewport.panX / zoom - worldPadding,
            right: (viewportRect.width - state.viewport.panX) / zoom + worldPadding,
            top: -state.viewport.panY / zoom - worldPadding,
          };
        }

        function rectIntersectsBounds(x, y, width, height, bounds, padding) {
          const extra = Number.isFinite(padding) ? padding : 0;
          return !(
            x + width < bounds.left - extra ||
            x > bounds.right + extra ||
            y + height < bounds.top - extra ||
            y > bounds.bottom + extra
          );
        }

        function segmentIntersectsBounds(x1, y1, x2, y2, bounds, padding) {
          const extra = Number.isFinite(padding) ? padding : 0;
          const minX = Math.min(x1, x2);
          const maxX = Math.max(x1, x2);
          const minY = Math.min(y1, y2);
          const maxY = Math.max(y1, y2);

          return !(
            maxX < bounds.left - extra ||
            minX > bounds.right + extra ||
            maxY < bounds.top - extra ||
            minY > bounds.bottom + extra
          );
        }

        function webGpuCommonShaderSource() {
          return [
            "struct ErdCommonUniforms {",
            "  canvas: vec2<f32>,",
            "  pan: vec2<f32>,",
            "  zoom: f32,",
            "  deviceScale: f32,",
            "  pad0: vec2<f32>,",
            "};",
            "@group(0) @binding(0) var<uniform> erdUniforms: ErdCommonUniforms;",
            "fn world_to_clip(world: vec2<f32>) -> vec4<f32> {",
            "  let screen = (world * erdUniforms.zoom + erdUniforms.pan) * erdUniforms.deviceScale;",
            "  let clip = screen / erdUniforms.canvas * 2.0 - vec2<f32>(1.0, 1.0);",
            "  return vec4<f32>(clip.x, -clip.y, 0.0, 1.0);",
            "}",
          ].join("\\n");
        }

        function webGpuTableShaderSource() {
          return webGpuCommonShaderSource() + "\\n" + [
            "struct VertexIn {",
            "  @location(0) corner: vec2<f32>,",
            "  @location(1) bounds: vec4<f32>,",
            "  @location(2) fill: vec4<f32>,",
            "  @location(3) stroke: vec4<f32>,",
            "  @location(4) style: vec2<f32>,",
            "};",
            "struct VertexOut {",
            "  @builtin(position) position: vec4<f32>,",
            "  @location(0) local: vec2<f32>,",
            "  @location(1) size: vec2<f32>,",
            "  @location(2) fill: vec4<f32>,",
            "  @location(3) stroke: vec4<f32>,",
            "  @location(4) style: vec2<f32>,",
            "};",
            "@vertex fn vs(input: VertexIn) -> VertexOut {",
            "  var out: VertexOut;",
            "  let world = input.bounds.xy + input.corner * input.bounds.zw;",
            "  out.position = world_to_clip(world);",
            "  out.local = input.corner * input.bounds.zw;",
            "  out.size = input.bounds.zw;",
            "  out.fill = input.fill;",
            "  out.stroke = input.stroke;",
            "  out.style = input.style;",
            "  return out;",
            "}",
            "fn rounded_box_sdf(p: vec2<f32>, b: vec2<f32>, r: f32) -> f32 {",
            "  let q = abs(p - b * 0.5) - (b * 0.5 - vec2<f32>(r, r));",
            "  return length(max(q, vec2<f32>(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - r;",
            "}",
            "@fragment fn fs(input: VertexOut) -> @location(0) vec4<f32> {",
            "  let radius = min(input.style.x, min(input.size.x, input.size.y) * 0.5);",
            "  let dist = rounded_box_sdf(input.local, input.size, radius);",
            "  let aa = max(fwidth(dist), 0.75);",
            "  let alpha = 1.0 - smoothstep(0.0, aa, dist);",
            "  let inner = 1.0 - smoothstep(-input.style.y - aa, -input.style.y + aa, dist);",
            "  let color = mix(input.stroke, input.fill, inner);",
            "  return vec4<f32>(color.rgb, color.a * alpha);",
            "}",
          ].join("\\n");
        }

        function webGpuSegmentShaderSource() {
          return webGpuCommonShaderSource() + "\\n" + [
            "struct VertexIn {",
            "  @location(0) corner: vec2<f32>,",
            "  @location(1) segment: vec4<f32>,",
            "  @location(2) halfWidth: f32,",
            "  @location(3) color: vec4<f32>,",
            "};",
            "struct VertexOut {",
            "  @builtin(position) position: vec4<f32>,",
            "  @location(0) color: vec4<f32>,",
            "};",
            "@vertex fn vs(input: VertexIn) -> VertexOut {",
            "  var out: VertexOut;",
            "  let start = input.segment.xy;",
            "  let end = input.segment.zw;",
            "  let delta = end - start;",
            "  let len = max(length(delta), 0.0001);",
            "  let tangent = delta / len;",
            "  let normal = vec2<f32>(-tangent.y, tangent.x);",
            "  let base = mix(start, end, input.corner.x);",
            "  let world = base + normal * input.corner.y * input.halfWidth;",
            "  out.position = world_to_clip(world);",
            "  out.color = input.color;",
            "  return out;",
            "}",
            "@fragment fn fs(input: VertexOut) -> @location(0) vec4<f32> {",
            "  return input.color;",
            "}",
          ].join("\\n");
        }

        function webGpuSpriteShaderSource() {
          return webGpuCommonShaderSource() + "\\n" + [
            "@group(1) @binding(0) var spriteSampler: sampler;",
            "@group(1) @binding(1) var spriteTexture: texture_2d<f32>;",
            "struct VertexIn {",
            "  @location(0) corner: vec2<f32>,",
            "  @location(1) bounds: vec4<f32>,",
            "  @location(2) uvBounds: vec4<f32>,",
            "  @location(3) alpha: f32,",
            "};",
            "struct VertexOut {",
            "  @builtin(position) position: vec4<f32>,",
            "  @location(0) uv: vec2<f32>,",
            "  @location(1) alpha: f32,",
            "};",
            "@vertex fn vs(input: VertexIn) -> VertexOut {",
            "  var out: VertexOut;",
            "  let world = input.bounds.xy + input.corner * input.bounds.zw;",
            "  out.position = world_to_clip(world);",
            "  out.uv = mix(input.uvBounds.xy, input.uvBounds.zw, input.corner);",
            "  out.alpha = input.alpha;",
            "  return out;",
            "}",
            "@fragment fn fs(input: VertexOut) -> @location(0) vec4<f32> {",
            "  let color = textureSample(spriteTexture, spriteSampler, input.uv);",
            "  return vec4<f32>(color.rgb, color.a * input.alpha);",
            "}",
          ].join("\\n");
        }

        function tableVertexShaderSource() {
          return "#version 300 es\\n" +
            "in vec2 a_corner; in vec4 a_bounds; in vec4 a_fill; in vec4 a_stroke; in vec2 a_style;\\n" +
            "uniform vec2 u_canvas; uniform vec2 u_pan; uniform float u_zoom; uniform float u_deviceScale;\\n" +
            "out vec2 v_local; out vec2 v_size; out vec4 v_fill; out vec4 v_stroke; out vec2 v_style;\\n" +
            "void main() { vec2 world = a_bounds.xy + a_corner * a_bounds.zw; vec2 screen = (world * u_zoom + u_pan) * u_deviceScale; vec2 clip = screen / u_canvas * 2.0 - 1.0; gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0); v_local = a_corner * a_bounds.zw; v_size = a_bounds.zw; v_fill = a_fill; v_stroke = a_stroke; v_style = a_style; }";
        }

        function tableFragmentShaderSource() {
          return "#version 300 es\\nprecision mediump float;\\n" +
            "in vec2 v_local; in vec2 v_size; in vec4 v_fill; in vec4 v_stroke; in vec2 v_style; out vec4 outColor;\\n" +
            "float roundedBoxSDF(vec2 p, vec2 b, float r) { vec2 q = abs(p - b * 0.5) - (b * 0.5 - vec2(r)); return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\\n" +
            "void main() { float radius = min(v_style.x, min(v_size.x, v_size.y) * 0.5); float dist = roundedBoxSDF(v_local, v_size, radius); float aa = max(fwidth(dist), 0.75); float alpha = 1.0 - smoothstep(0.0, aa, dist); float inner = 1.0 - smoothstep(-v_style.y - aa, -v_style.y + aa, dist); vec4 color = mix(v_stroke, v_fill, inner); outColor = vec4(color.rgb, color.a * alpha); if (outColor.a <= 0.01) { discard; } }";
        }

        function segmentVertexShaderSource() {
          return "#version 300 es\\n" +
            "in vec2 a_corner; in vec4 a_segment; in float a_halfWidth; in vec4 a_color;\\n" +
            "uniform vec2 u_canvas; uniform vec2 u_pan; uniform float u_zoom; uniform float u_deviceScale;\\n" +
            "out vec4 v_color;\\n" +
            "void main() { vec2 start = a_segment.xy; vec2 end = a_segment.zw; vec2 delta = end - start; float len = max(length(delta), 0.0001); vec2 tangent = delta / len; vec2 normal = vec2(-tangent.y, tangent.x); vec2 base = mix(start, end, a_corner.x); vec2 world = base + normal * a_corner.y * a_halfWidth; vec2 screen = (world * u_zoom + u_pan) * u_deviceScale; vec2 clip = screen / u_canvas * 2.0 - 1.0; gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0); v_color = a_color; }";
        }

        function segmentFragmentShaderSource() {
          return "#version 300 es\\nprecision mediump float; in vec4 v_color; out vec4 outColor; void main() { outColor = v_color; }";
        }

        function spriteVertexShaderSource() {
          return "#version 300 es\\n" +
            "in vec2 a_corner; in vec4 a_bounds; in vec4 a_uvBounds; in float a_alpha;\\n" +
            "uniform vec2 u_canvas; uniform vec2 u_pan; uniform float u_zoom; uniform float u_deviceScale;\\n" +
            "out vec2 v_uv; out float v_alpha;\\n" +
            "void main() { vec2 world = a_bounds.xy + a_corner * a_bounds.zw; vec2 screen = (world * u_zoom + u_pan) * u_deviceScale; vec2 clip = screen / u_canvas * 2.0 - 1.0; gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0); v_uv = mix(a_uvBounds.xy, a_uvBounds.zw, a_corner); v_alpha = a_alpha; }";
        }

        function spriteFragmentShaderSource() {
          return "#version 300 es\\nprecision mediump float; in vec2 v_uv; in float v_alpha; uniform sampler2D u_texture; out vec4 outColor; void main() { vec4 color = texture(u_texture, v_uv); outColor = vec4(color.rgb, color.a * v_alpha); if (outColor.a <= 0.01) { discard; } }";
        }
  `;
}
