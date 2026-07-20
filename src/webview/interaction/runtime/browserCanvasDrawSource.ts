export function getBrowserCanvasDrawSource(): string {
  return `
        const GPU_TILE_SIZE = 960;
        const GPU_TABLE_LABEL_ZOOM = 0.16;
        const GPU_TABLE_SUBTITLE_ZOOM = 0.24;
        const GPU_TABLE_DETAIL_ZOOM = 0.58;
        const GPU_TABLE_DETAIL_LIMIT = 56;
        const GPU_LABEL_ATLAS_SIZE = 2048;
        const GPU_MAX_LABELS_PER_FRAME = 2200;
        const GPU_DENSE_LABEL_ZOOM = 0.42;
        const GPU_DENSE_LABEL_TABLE_LIMIT = 520;
        const GPU_EDGE_TABLE_CLEARANCE = 10;
        const GPU_EDGE_DETOUR_GAP = 4;
        const GPU_EDGE_DETOUR_MAX_STEPS = 96;
        const GPU_EDGE_VISIBILITY_MAX_EXPANSIONS = 2048;
        const GPU_EDGE_VISIBILITY_MAX_DISCOVERY_STEPS = 512;
        const GPU_CLUSTER_OUTLINE_MAX_AREA_RATIO = 14;
        const GPU_MIN_LABEL_FONT_SIZE = 9;
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
          const tablePipeline = createWebGpuTablePipeline(device, format, commonBindGroupLayout);
          const renderer = {
            atlas: createWebGpuLabelAtlas(device),
            backend: "webgpu",
            commonBindGroupLayout,
            context: null,
            device,
            format,
            leafBundle: {
              corners: tablePipeline.corners,
              instanceBuffer: null,
              instanceBytes: 0,
              pipeline: tablePipeline.pipeline,
            },
            leafTile: {
              corners: tablePipeline.corners,
              instanceBuffer: null,
              instanceBytes: 0,
              pipeline: tablePipeline.pipeline,
            },
            segment: createWebGpuSegmentPipeline(device, format, commonBindGroupLayout),
            sprite: createWebGpuSpritePipeline(
              device,
              format,
              commonBindGroupLayout,
              spriteBindGroupLayout,
            ),
            spriteBindGroupLayout,
            table: tablePipeline,
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

          const tableCorners = createStaticBuffer(gl, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]));
          const renderer = {
            atlas: createLabelAtlas(gl),
            backend: "webgl2",
            gl,
            leafBundle: {
              buffers: {
                corners: tableCorners,
                instances: gl.createBuffer(),
              },
              program: tableProgram,
            },
            leafTile: {
              buffers: {
                corners: tableCorners,
                instances: gl.createBuffer(),
              },
              program: tableProgram,
            },
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
                corners: tableCorners,
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
            avoidedBundlePenetrations: 0,
            avoidedNodePenetrations: 0,
            avoidedTablePenetrations: 0,
            detouredEdges: 0,
            edgeBuckets: new Map(),
            edgeSegments: [],
            leafBundleBuckets: new Map(),
            leafBundles: [],
            leafTileBuckets: new Map(),
            leafTiles: [],
            tableBuckets: new Map(),
            tables: [],
            tablesById: new Map(),
            unresolvedRouteEdges: [],
            unresolvedRoutePenetrations: 0,
            visibilityFallbackEdges: 0,
          };

          const leafTilesRaw = (renderModel.bundleLeafTiles || []);
          for (const tile of leafTilesRaw) {
            if (!tile || !tile.position || !tile.size) {
              continue;
            }
            if (!isVisibleModel(tile.modelId)) {
              continue;
            }
            const x = Number(tile.position.x || 0);
            const y = Number(tile.position.y || 0);
            const width = Number(tile.size.width || 0);
            const height = Number(tile.size.height || 0);
            if (width <= 0 || height <= 0) {
              continue;
            }
            const record = {
              appLabel: tile.appLabel || "",
              bundleIndex: Number(tile.bundleIndex || 0),
              height,
              modelId: tile.modelId,
              modelName: tile.modelName || "",
              width,
              x,
              y,
            };
            const recordIndex = nextScene.leafTiles.length;
            nextScene.leafTiles.push(record);
            addToBuckets(
              nextScene.leafTileBuckets,
              { bottom: y + height, left: x, right: x + width, top: y },
              recordIndex,
            );
          }

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
          for (const meta of getActiveEdgeMeta()) {
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

          const collapseEnabled = Boolean(state.collapseClusters) && !renderModel.modelCatalogMode;
          let collapsedAggregates = null;
          let collapsedClusterKeyByModelId = null;
          if (collapseEnabled) {
            const result = applyClusterCollapse(nextScene, visibleEdgeEntries);
            if (result) {
              collapsedAggregates = result.aggregates;
              collapsedClusterKeyByModelId = result.clusterKeyByModelId;
            }
          }
          buildClusterOutlineRecords(nextScene);

          const bundlingEnabled = Boolean(state.edgeBundling) && !renderModel.modelCatalogMode && !collapseEnabled;
          let bundleAppCenters = null;
          let bundleSpatial = null;
          if (bundlingEnabled) {
            bundleAppCenters = computeAppClusterCenters(visibleEdgeEntries);
            const distinctApps = new Set();
            for (const entry of visibleEdgeEntries) {
              if (entry.sourceTable && entry.sourceTable.appLabel) {
                distinctApps.add(entry.sourceTable.appLabel);
              }
            }
            if (distinctApps.size < 3) {
              bundleSpatial = computeSpatialClusterContext(visibleEdgeEntries);
            }
          }

          function buildEdgePathWithBundle(entry) {
            const fallbackPath = getStaticOrLiveEdgePath(entry);
            if (!bundlingEnabled || !entry.sourceTable || !entry.targetTable) {
              return fallbackPath;
            }
            const sourceLabel = entry.sourceTable.appLabel || "";
            const targetLabel = entry.targetTable.appLabel || "";
            if (bundleAppCenters && sourceLabel && targetLabel && sourceLabel !== targetLabel) {
              const sourceCluster = bundleAppCenters.get(sourceLabel);
              const targetCluster = bundleAppCenters.get(targetLabel);
              if (sourceCluster && targetCluster) {
                return buildBundledPath(
                  entry.sourcePosition,
                  entry.sourceTable,
                  entry.targetPosition,
                  entry.targetTable,
                  sourceCluster,
                  targetCluster,
                  0.85,
                );
              }
            }
            if (bundleSpatial) {
              const sourceCellKey = bundleSpatial.cellKey(entry.sourcePosition, entry.sourceTable);
              const targetCellKey = bundleSpatial.cellKey(entry.targetPosition, entry.targetTable);
              if (sourceCellKey !== targetCellKey) {
                return buildBundledPath(
                  entry.sourcePosition,
                  entry.sourceTable,
                  entry.targetPosition,
                  entry.targetTable,
                  bundleSpatial.cellCenter(sourceCellKey),
                  bundleSpatial.cellCenter(targetCellKey),
                  0.7,
                );
              }
            }
            return fallbackPath;
          }

          if (collapseEnabled && collapsedAggregates) {
            renderedEdges = collapsedAggregates.superEdges.map((edge) => ({
              edgeId: edge.edgeId,
              meta: edge.meta,
              points: edge.points,
            }));
          } else {
            renderedEdges = renderModel.modelCatalogMode
              ? getStaticOrCatalogEdgePaths(visibleEdgeEntries)
              : visibleEdgeEntries.map((entry) => ({
                  edgeId: entry.meta.edgeId,
                  meta: entry.meta,
                  points: buildEdgePathWithBundle(entry),
                }));
          }

          for (const edge of renderedEdges) {
            const routed = routeEdgePathAroundTables(
              edge.points,
              edge.meta,
              nextScene,
            );
            nextScene.avoidedTablePenetrations += routed.initialCollisions.length;
            nextScene.avoidedBundlePenetrations += routed.initialCollisions.filter(
              (collision) => isSyntheticBundleModelId(collision.table.modelId),
            ).length;
            nextScene.avoidedNodePenetrations += routed.initialCollisions.filter(
              (collision) => !isSyntheticBundleModelId(collision.table.modelId),
            ).length;
            nextScene.unresolvedRoutePenetrations += routed.unresolvedCollisions.length;
            if (routed.usedVisibilityFallback) {
              nextScene.visibilityFallbackEdges += 1;
            }
            if (
              routed.unresolvedCollisions.length > 0
              && nextScene.unresolvedRouteEdges.length < 24
            ) {
              nextScene.unresolvedRouteEdges.push({
                edgeId: edge.edgeId,
                endPoint: edge.points[edge.points.length - 1],
                sourceModelId: edge.meta && edge.meta.sourceModelId,
                startPoint: edge.points[0],
                targetModelId: edge.meta && edge.meta.targetModelId,
                tableModelIds: [...new Set(routed.unresolvedCollisions.map(
                  (collision) => collision.table.modelId,
                ))],
                collisions: routed.unresolvedCollisions.slice(0, 12).map((collision) => {
                  const segmentStart = routed.points[collision.segmentIndex];
                  const segmentEnd = routed.points[collision.segmentIndex + 1];
                  return {
                    intervalEnd: round2(collision.interval.end),
                    intervalStart: round2(collision.interval.start),
                    modelId: collision.table.modelId,
                    segmentEnd,
                    segmentEndInside: isPointInsideRoutingTable(
                      segmentEnd,
                      collision.table,
                    ),
                    segmentIndex: collision.segmentIndex,
                    segmentStart,
                    segmentStartInside: isPointInsideRoutingTable(
                      segmentStart,
                      collision.table,
                    ),
                  };
                }),
              });
            }
            if (routed.initialCollisions.length > 0) {
              nextScene.detouredEdges += 1;
            }
            edge.points = routed.points;

            for (const visibleSegment of findSegments(edge.points)) {
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

          renderedCrossings = [];
          sceneGraph = nextScene;
          logErdDuration("info", "scene.graph.built", startedAt, {
            avoidedBundlePenetrations: nextScene.avoidedBundlePenetrations,
            avoidedNodePenetrations: nextScene.avoidedNodePenetrations,
            avoidedTablePenetrations: nextScene.avoidedTablePenetrations,
            clusterOutlineRecords: nextScene.leafBundles.length,
            detouredEdges: nextScene.detouredEdges,
            edgeSegments: nextScene.edgeSegments.length,
            leafBundleRecords: nextScene.leafBundles.length,
            leafBundlesInPayload: (renderModel.leafBundles || []).length,
            renderer: gpuRenderer ? gpuRenderer.backend : "unknown",
            tables: nextScene.tables.length,
            unresolvedRouteEdges: nextScene.unresolvedRouteEdges,
            unresolvedRoutePenetrations: nextScene.unresolvedRoutePenetrations,
            visibilityFallbackEdges: nextScene.visibilityFallbackEdges,
          });
          return sceneGraph;
        }

        function buildClusterOutlineRecords(scene) {
          scene.leafBundles = [];
          scene.leafBundleBuckets = new Map();
          if (state.collapseClusters) {
            return;
          }

          const selectedTable = scene.tablesById.get(state.selectedModelId);
          const selectedClusterId =
            selectedTable && selectedTable.meta && selectedTable.meta.clusterId;
          if (!selectedClusterId) {
            return;
          }

          const outlineMetaById = new Map(
            (renderModel.clusterOutlines || []).map((outline) => [outline.clusterId, outline]),
          );
          const members = scene.tables.filter((table) =>
            String(table.modelId).indexOf("__leafbundle.") !== 0
            && table.meta
            && table.meta.clusterId === selectedClusterId
          );
          if (members.length < 2) {
            return;
          }

          const padding = 32;
          const left = Math.min(...members.map((table) => table.x)) - padding;
          const top = Math.min(...members.map((table) => table.y)) - padding;
          const right = Math.max(...members.map((table) => table.x + table.width)) + padding;
          const bottom = Math.max(...members.map((table) => table.y + table.height)) + padding;
          const memberArea = members.reduce(
            (sum, table) => sum + table.width * table.height,
            0,
          );
          const outlineArea = (right - left) * (bottom - top);
          // A single rectangle around a dispersed cluster becomes visual noise.
          // In that case the member cards and their edges still carry the focus,
          // while the oversized background outline is deliberately omitted.
          if (outlineArea > memberArea * GPU_CLUSTER_OUTLINE_MAX_AREA_RATIO) {
            return;
          }
          const outline = outlineMetaById.get(selectedClusterId) || {};
          const record = {
            appLabel: outline.colorKey || selectedClusterId,
            clusterId: selectedClusterId,
            height: bottom - top,
            kind: "cluster-outline",
            leafCount: members.length,
            parentName: outline.label || selectedClusterId,
            width: right - left,
            x: left,
            y: top,
          };
          scene.leafBundles.push(record);
          addToBuckets(
            scene.leafBundleBuckets,
            { bottom, left, right, top },
            0,
          );
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

        function isSyntheticBundleModelId(modelId) {
          return String(modelId || "").indexOf("__leafbundle.") === 0;
        }

        function isEdgeEndpointTable(meta, modelId) {
          const endpointIds = Array.isArray(meta.logicalEndpointModelIds)
            && meta.logicalEndpointModelIds.length > 0
            ? meta.logicalEndpointModelIds
            : [meta.sourceModelId, meta.targetModelId];
          if (
            modelId === meta.sourceModelId
            || modelId === meta.targetModelId
            || endpointIds.includes(modelId)
          ) {
            return true;
          }
          if (!isSyntheticBundleModelId(modelId)) {
            return false;
          }

          const leaves = typeof bundleLeavesByFakeIdRaw === "object"
            ? bundleLeavesByFakeIdRaw[modelId] || []
            : [];
          if (!leaves.length) {
            return false;
          }
          return endpointIds.some((endpointId) => leaves.includes(endpointId));
        }

        function collectRoutingCandidateTables(scene, bounds, overrideById) {
          const recordsById = new Map();
          for (const table of collectStaticRoutingCandidateTables(scene, bounds)) {
            if (overrideById && overrideById.has(table.modelId)) {
              continue;
            }
            recordsById.set(table.modelId, table);
          }
          if (overrideById) {
            for (const table of overrideById.values()) {
              if (
                rectIntersectsBounds(
                  table.x,
                  table.y,
                  table.width,
                  table.height,
                  bounds,
                  GPU_EDGE_TABLE_CLEARANCE + GPU_EDGE_DETOUR_GAP,
                )
              ) {
                recordsById.set(table.modelId, table);
              }
            }
          }
          return [...recordsById.values()];
        }

        function collectStaticRoutingCandidateTables(scene, bounds) {
          return collectBucketValues(scene.tableBuckets, bounds)
            .map((modelId) => scene.tablesById.get(modelId))
            .filter(Boolean);
        }

        function collectEdgePathCollisions(points, meta, scene, overrideById) {
          const collisions = [];
          const padding = GPU_EDGE_TABLE_CLEARANCE;
          for (let segmentIndex = 0; segmentIndex + 1 < points.length; segmentIndex += 1) {
            const segment = {
              end: points[segmentIndex + 1],
              start: points[segmentIndex],
            };
            const tables = collectRoutingCandidateTables(scene, {
              bottom: Math.max(segment.start.y, segment.end.y) + padding,
              left: Math.min(segment.start.x, segment.end.x) - padding,
              right: Math.max(segment.start.x, segment.end.x) + padding,
              top: Math.min(segment.start.y, segment.end.y) - padding,
            }, overrideById);
            for (const table of tables) {
              if (isEdgeEndpointTable(meta, table.modelId)) {
                continue;
              }
              const interval = segmentRectangleInteriorInterval(
                segment,
                table.x - padding,
                table.x + table.width + padding,
                table.y - padding,
                table.y + table.height + padding,
              );
              if (interval) {
                collisions.push({ interval, segmentIndex, table });
              }
            }
          }
          return collisions.sort((left, right) =>
            left.segmentIndex - right.segmentIndex
            || left.interval.start - right.interval.start
            || String(left.table.modelId).localeCompare(String(right.table.modelId))
          );
        }

        function routeEdgePathAroundTables(rawPoints, meta, scene, overrideById) {
          let points = normalizePoints(rawPoints || []);
          const initialCollisions = collectEdgePathCollisions(
            points,
            meta,
            scene,
            overrideById,
          );
          let unresolvedCollisions = initialCollisions;
          if (initialCollisions.length === 0) {
            return {
              initialCollisions,
              points,
              unresolvedCollisions: [],
              usedVisibilityFallback: false,
            };
          }

          // A native endpoint can be valid on its own table boundary while
          // still landing inside a tightly adjacent table's clearance box.
          // Such a fixed endpoint makes a collision-free path mathematically
          // impossible. Move only that endpoint to a free port on the same
          // rendered table and add a short outward escape segment before the
          // general obstacle search.
          points = repairCollidingEndpointPorts(
            points,
            meta,
            scene,
            overrideById,
          );
          const seenRoutes = new Set([edgeRouteSignature(points)]);
          unresolvedCollisions = collectEdgePathCollisions(
            points,
            meta,
            scene,
            overrideById,
          );
          for (
            let step = 0;
            step < GPU_EDGE_DETOUR_MAX_STEPS && unresolvedCollisions.length > 0;
            step += 1
          ) {
            const collision = unresolvedCollisions[0];
            const start = points[collision.segmentIndex];
            const end = points[collision.segmentIndex + 1];
            let best = null;

            for (const detour of createTableDetourCandidates(collision.table)) {
              const replacement = normalizePoints([start, ...detour, end]);
              if (pathIntersectsRoutingTable(replacement, collision.table)) {
                continue;
              }
              const candidatePoints = normalizePoints([
                ...points.slice(0, collision.segmentIndex),
                ...replacement,
                ...points.slice(collision.segmentIndex + 2),
              ]);
              const signature = edgeRouteSignature(candidatePoints);
              if (seenRoutes.has(signature)) {
                continue;
              }
              const candidateCollisions = collectEdgePathCollisions(
                candidatePoints,
                meta,
                scene,
                overrideById,
              );
              const score = candidateCollisions.length * 1_000_000_000
                + edgeRouteLength(candidatePoints)
                + candidatePoints.length * 12;
              if (!best || score < best.score) {
                best = {
                  collisions: candidateCollisions,
                  points: candidatePoints,
                  score,
                  signature,
                };
              }
            }

            if (!best) {
              break;
            }
            points = best.points;
            unresolvedCollisions = best.collisions;
            seenRoutes.add(best.signature);
          }

          if (unresolvedCollisions.length === 0) {
            points = simplifyCollisionFreeRoute(points, meta, scene, overrideById);
            return {
              initialCollisions,
              points,
              unresolvedCollisions,
              usedVisibilityFallback: false,
            };
          }

          const visibilityRoute = routeCollisionFreeVisibilityPath(
            points,
            meta,
            scene,
            overrideById,
          );
          if (visibilityRoute) {
            points = simplifyCollisionFreeRoute(
              visibilityRoute,
              meta,
              scene,
              overrideById,
            );
            unresolvedCollisions = collectEdgePathCollisions(
              points,
              meta,
              scene,
              overrideById,
            );
          }
          return {
            initialCollisions,
            points,
            unresolvedCollisions,
            usedVisibilityFallback: Boolean(visibilityRoute)
              && unresolvedCollisions.length === 0,
          };
        }

        function repairCollidingEndpointPorts(
          rawPoints,
          meta,
          scene,
          overrideById,
        ) {
          let points = normalizePoints(rawPoints);
          points = repairCollidingEndpointPort(
            points,
            "source",
            meta,
            scene,
            overrideById,
          );
          points = repairCollidingEndpointPort(
            points,
            "target",
            meta,
            scene,
            overrideById,
          );
          return points;
        }

        function repairCollidingEndpointPort(
          points,
          endpoint,
          meta,
          scene,
          overrideById,
        ) {
          if (points.length < 2) {
            return points;
          }
          const endpointTableId = endpoint === "source"
            ? meta.sourceModelId
            : meta.targetModelId;
          const endpointTable = overrideById && overrideById.get(endpointTableId)
            || scene.tablesById.get(endpointTableId);
          if (!endpointTable) {
            return points;
          }

          const collisions = collectEdgePathCollisions(
            points,
            meta,
            scene,
            overrideById,
          );
          if (countEndpointCollisions(collisions, points.length, endpoint) === 0) {
            return points;
          }

          let best = null;
          for (const portCandidate of createEndpointPortCandidates(endpointTable)) {
            const candidatePoints = endpoint === "source"
              ? normalizePoints([
                  portCandidate.port,
                  portCandidate.escape,
                  ...points.slice(1),
                ])
              : normalizePoints([
                  ...points.slice(0, -1),
                  portCandidate.escape,
                  portCandidate.port,
                ]);
            const candidateCollisions = collectEdgePathCollisions(
              candidatePoints,
              meta,
              scene,
              overrideById,
            );
            const endpointCollisionCount = countEndpointCollisions(
              candidateCollisions,
              candidatePoints.length,
              endpoint,
            );
            const score = endpointCollisionCount * 1_000_000_000_000
              + candidateCollisions.length * 1_000_000_000
              + edgeRouteLength(candidatePoints)
              + edgePointDistance(
                endpoint === "source" ? points[0] : points[points.length - 1],
                portCandidate.port,
              );
            if (!best || score < best.score) {
              best = {
                collisions: candidateCollisions,
                endpointCollisionCount,
                points: candidatePoints,
                score,
              };
            }
          }

          const currentEndpointCollisionCount = countEndpointCollisions(
            collisions,
            points.length,
            endpoint,
          );
          return best && (
            best.endpointCollisionCount < currentEndpointCollisionCount
            || (
              best.endpointCollisionCount === currentEndpointCollisionCount
              && best.collisions.length < collisions.length
            )
          )
            ? best.points
            : points;
        }

        function countEndpointCollisions(collisions, pointCount, endpoint) {
          const endpointSegmentIndex = endpoint === "source"
            ? 0
            : pointCount - 2;
          return collisions.filter((collision) =>
            collision.segmentIndex === endpointSegmentIndex
            && (
              endpoint === "source"
                ? collision.interval.start <= 0.000001
                : collision.interval.end >= 0.999999
            )
          ).length;
        }

        function createEndpointPortCandidates(table) {
          const escapeDistance = GPU_EDGE_TABLE_CLEARANCE + GPU_EDGE_DETOUR_GAP;
          const sampleCount = 16;
          const candidatesByKey = new Map();
          function add(port, normal) {
            const roundedPort = { x: round2(port.x), y: round2(port.y) };
            const escape = {
              x: round2(roundedPort.x + normal.x * escapeDistance),
              y: round2(roundedPort.y + normal.y * escapeDistance),
            };
            candidatesByKey.set(
              edgeRoutePointKey(roundedPort) + ":" + edgeRoutePointKey(escape),
              { escape, port: roundedPort },
            );
          }
          for (let index = 0; index <= sampleCount; index += 1) {
            const fraction = index / sampleCount;
            const x = table.x + table.width * fraction;
            const y = table.y + table.height * fraction;
            add({ x, y: table.y }, { x: 0, y: -1 });
            add({ x, y: table.y + table.height }, { x: 0, y: 1 });
            add({ x: table.x, y }, { x: -1, y: 0 });
            add({ x: table.x + table.width, y }, { x: 1, y: 0 });
          }
          return [...candidatesByKey.values()];
        }

        function routeCollisionFreeVisibilityPath(
          rawPoints,
          meta,
          scene,
          overrideById,
        ) {
          const originalPoints = normalizePoints(rawPoints || []);
          if (originalPoints.length < 2) {
            return null;
          }
          const start = originalPoints[0];
          const goal = originalPoints[originalPoints.length - 1];
          const startKey = edgeRoutePointKey(start);
          const goalKey = edgeRoutePointKey(goal);
          if (startKey === goalKey) {
            return originalPoints;
          }

          const bestCostByKey = new Map([[startKey, 0]]);
          const parentByKey = new Map();
          const pointByKey = new Map([[startKey, start], [goalKey, goal]]);
          const frontier = [{
            cost: 0,
            estimate: edgePointDistance(start, goal),
            key: startKey,
            point: start,
          }];
          let expansions = 0;

          while (
            frontier.length > 0
            && expansions < GPU_EDGE_VISIBILITY_MAX_EXPANSIONS
          ) {
            let bestIndex = 0;
            for (let index = 1; index < frontier.length; index += 1) {
              if (
                frontier[index].estimate < frontier[bestIndex].estimate
                || (
                  frontier[index].estimate === frontier[bestIndex].estimate
                  && frontier[index].cost < frontier[bestIndex].cost
                )
              ) {
                bestIndex = index;
              }
            }
            const current = frontier.splice(bestIndex, 1)[0];
            if (current.cost > (bestCostByKey.get(current.key) ?? Infinity) + 0.001) {
              continue;
            }
            expansions += 1;

            if (isCollisionFreeRoutingSegment(
              current.point,
              goal,
              meta,
              scene,
              overrideById,
            )) {
              parentByKey.set(goalKey, current.key);
              return reconstructVisibilityRoute(
                goalKey,
                parentByKey,
                pointByKey,
              );
            }

            const neighbors = discoverVisibleDetourPoints(
              current.point,
              goal,
              meta,
              scene,
              overrideById,
            );
            for (const neighbor of neighbors) {
              const neighborKey = edgeRoutePointKey(neighbor);
              if (neighborKey === current.key || neighborKey === goalKey) {
                continue;
              }
              const nextCost = current.cost + edgePointDistance(current.point, neighbor);
              if (nextCost + 0.001 >= (bestCostByKey.get(neighborKey) ?? Infinity)) {
                continue;
              }
              bestCostByKey.set(neighborKey, nextCost);
              parentByKey.set(neighborKey, current.key);
              pointByKey.set(neighborKey, neighbor);
              frontier.push({
                cost: nextCost,
                estimate: nextCost + edgePointDistance(neighbor, goal),
                key: neighborKey,
                point: neighbor,
              });
            }
          }
          return null;
        }

        function discoverVisibleDetourPoints(
          origin,
          goal,
          meta,
          scene,
          overrideById,
        ) {
          const originKey = edgeRoutePointKey(origin);
          const pending = [goal];
          const pendingKeys = new Set([edgeRoutePointKey(goal)]);
          const inspectedKeys = new Set();
          const visibleByKey = new Map();
          let discoverySteps = 0;

          while (
            pending.length > 0
            && discoverySteps < GPU_EDGE_VISIBILITY_MAX_DISCOVERY_STEPS
          ) {
            const target = pending.shift();
            const targetKey = edgeRoutePointKey(target);
            pendingKeys.delete(targetKey);
            if (targetKey === originKey || inspectedKeys.has(targetKey)) {
              continue;
            }
            inspectedKeys.add(targetKey);
            discoverySteps += 1;

            const collisions = collectEdgePathCollisions(
              [origin, target],
              meta,
              scene,
              overrideById,
            );
            if (collisions.length === 0) {
              visibleByKey.set(targetKey, target);
              continue;
            }

            for (const corner of createTableDetourCornerPoints(collisions[0].table)) {
              const cornerKey = edgeRoutePointKey(corner);
              if (
                cornerKey === originKey
                || inspectedKeys.has(cornerKey)
                || pendingKeys.has(cornerKey)
              ) {
                continue;
              }
              pending.push(corner);
              pendingKeys.add(cornerKey);
            }
          }
          return [...visibleByKey.values()];
        }

        function isCollisionFreeRoutingSegment(start, end, meta, scene, overrideById) {
          return collectEdgePathCollisions(
            [start, end],
            meta,
            scene,
            overrideById,
          ).length === 0;
        }

        function reconstructVisibilityRoute(goalKey, parentByKey, pointByKey) {
          const reversed = [];
          const seen = new Set();
          let key = goalKey;
          while (key && !seen.has(key)) {
            seen.add(key);
            const point = pointByKey.get(key);
            if (!point) {
              return null;
            }
            reversed.push(point);
            key = parentByKey.get(key);
          }
          return normalizePoints(reversed.reverse());
        }

        function edgePointDistance(start, end) {
          return Math.hypot(end.x - start.x, end.y - start.y);
        }

        function edgeRoutePointKey(point) {
          return round2(point.x) + "," + round2(point.y);
        }

        function createTableDetourCandidates(table) {
          const [topLeft, topRight, bottomRight, bottomLeft] =
            createTableDetourCornerPoints(table);
          return [
            [topLeft],
            [topRight],
            [bottomRight],
            [bottomLeft],
            [topLeft, topRight],
            [topRight, topLeft],
            [topRight, bottomRight],
            [bottomRight, topRight],
            [bottomRight, bottomLeft],
            [bottomLeft, bottomRight],
            [bottomLeft, topLeft],
            [topLeft, bottomLeft],
          ];
        }

        function createTableDetourCornerPoints(table) {
          const gap = GPU_EDGE_TABLE_CLEARANCE + GPU_EDGE_DETOUR_GAP;
          const left = round2(table.x - gap);
          const right = round2(table.x + table.width + gap);
          const top = round2(table.y - gap);
          const bottom = round2(table.y + table.height + gap);
          return [
            { x: left, y: top },
            { x: right, y: top },
            { x: right, y: bottom },
            { x: left, y: bottom },
          ];
        }

        function pathIntersectsRoutingTable(points, table) {
          const padding = GPU_EDGE_TABLE_CLEARANCE;
          return findSegments(points).some((segment) =>
            Boolean(segmentRectangleInteriorInterval(
              segment,
              table.x - padding,
              table.x + table.width + padding,
              table.y - padding,
              table.y + table.height + padding,
            ))
          );
        }

        function isPointInsideRoutingTable(point, table) {
          const padding = GPU_EDGE_TABLE_CLEARANCE;
          return point.x > table.x - padding
            && point.x < table.x + table.width + padding
            && point.y > table.y - padding
            && point.y < table.y + table.height + padding;
        }

        function simplifyCollisionFreeRoute(rawPoints, meta, scene, overrideById) {
          const points = normalizePoints(rawPoints);
          let index = 1;
          while (index + 1 < points.length) {
            const direct = [points[index - 1], points[index + 1]];
            if (collectEdgePathCollisions(direct, meta, scene, overrideById).length === 0) {
              points.splice(index, 1);
            } else {
              index += 1;
            }
          }
          return points;
        }

        function edgeRouteLength(points) {
          return findSegments(points).reduce((sum, segment) =>
            sum + Math.hypot(
              segment.end.x - segment.start.x,
              segment.end.y - segment.start.y,
            ), 0);
        }

        function edgeRouteSignature(points) {
          return points.map((point) => round2(point.x) + "," + round2(point.y)).join(" ");
        }

        function segmentRectangleInteriorInterval(segment, left, right, top, bottom) {
          const dx = segment.end.x - segment.start.x;
          const dy = segment.end.y - segment.start.y;
          let enter = 0;
          let exit = 1;
          const axes = [
            [segment.start.x, dx, left, right],
            [segment.start.y, dy, top, bottom],
          ];

          for (const [origin, delta, minimum, maximum] of axes) {
            if (Math.abs(delta) < 0.000001) {
              if (origin <= minimum || origin >= maximum) {
                return null;
              }
              continue;
            }
            const first = (minimum - origin) / delta;
            const second = (maximum - origin) / delta;
            enter = Math.max(enter, Math.min(first, second));
            exit = Math.min(exit, Math.max(first, second));
            if (exit - enter <= 0.000001) {
              return null;
            }
          }

          return exit > 0.000001 && enter < 0.999999
            ? { end: Math.min(1, exit), start: Math.max(0, enter) }
            : null;
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
          const visibleLeafBundles = collectVisibleLeafBundles(scene, visibleBounds);
          const visibleLeafTiles = collectVisibleLeafTiles(scene, visibleBounds);
          const cullMs = performance.now() - cullStartedAt;
          const labelStartedAt = performance.now();
          const labels = collectVisibleLabels(visibleTables);
          appendLeafBundleLabels(labels, visibleLeafBundles);
          appendLeafTileLabels(labels, visibleLeafTiles);
          const labelMs = performance.now() - labelStartedAt;
          const drawStartedAt = performance.now();

          if (gpuRenderer.backend === "webgpu") {
            drawWebGpuScene(
              gpuRenderer,
              visibleSegments,
              visibleOverlays,
              visibleTables,
              labels,
              visibleLeafBundles,
              visibleLeafTiles,
            );
          } else {
            clearGpuScene(gpuRenderer);
            drawLeafBundleBatch(gpuRenderer, visibleLeafBundles);
            drawSegmentBatch(gpuRenderer, visibleSegments, false);
            drawSegmentBatch(gpuRenderer, visibleOverlays, true);
            drawLeafTileBatch(gpuRenderer, visibleLeafTiles);
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

        function collectVisibleLeafTiles(scene, bounds) {
          if (!scene.leafTiles.length) {
            return [];
          }
          const indices = collectBucketValues(scene.leafTileBuckets, bounds);
          const records = [];
          const seen = new Set();
          for (const recordIndex of indices) {
            if (seen.has(recordIndex)) {
              continue;
            }
            seen.add(recordIndex);
            const record = scene.leafTiles[recordIndex];
            if (!record) {
              continue;
            }
            if (!rectIntersectsBounds(record.x, record.y, record.width, record.height, bounds, 0)) {
              continue;
            }
            records.push(record);
          }
          return records;
        }

        function collectVisibleLeafBundles(scene, bounds) {
          if (!scene.leafBundles.length) {
            return [];
          }
          const indices = collectBucketValues(scene.leafBundleBuckets, bounds);
          const records = [];
          const seen = new Set();
          for (const recordIndex of indices) {
            if (seen.has(recordIndex)) {
              continue;
            }
            seen.add(recordIndex);
            const record = scene.leafBundles[recordIndex];
            if (!record) {
              continue;
            }
            if (!rectIntersectsBounds(record.x, record.y, record.width, record.height, bounds, 0)) {
              continue;
            }
            records.push(record);
          }
          return records;
        }

        function collectVisibleSegments(scene, bounds) {
          const records = collectBucketValues(scene.edgeBuckets, bounds)
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

          latestEdgeLodStats = {
            applied: false,
            limit: records.length,
            skippedSegments: 0,
            sourceSegments: records.length,
          };
          return applyLiveDragEdgeSegments(records, bounds);
        }

        function createEmptyEdgeLodStats() {
          return {
            applied: false,
            limit: 0,
            skippedSegments: 0,
            sourceSegments: 0,
          };
        }

        function getActiveTableDrag() {
          return drag && drag.kind === "table" && drag.currentPosition ? drag : null;
        }

        function createLiveDragTableOverrides(scene, activeDrag) {
          const overrideById = new Map();
          const baseRecord = scene.tablesById.get(activeDrag.modelId);
          if (!baseRecord) {
            return overrideById;
          }

          const position = activeDrag.currentPosition;
          const startPos = activeDrag.startPosition;
          const dx = position.x - (startPos ? startPos.x : baseRecord.x);
          const dy = position.y - (startPos ? startPos.y : baseRecord.y);
          overrideById.set(activeDrag.modelId, {
            ...baseRecord,
            maxX: position.x + baseRecord.width,
            maxY: position.y + baseRecord.height,
            options: getTableOptions(state, activeDrag.modelId),
            x: position.x,
            y: position.y,
          });

          const groupLeaves = (typeof bundleLeavesByFakeIdRaw === "object"
            && bundleLeavesByFakeIdRaw[activeDrag.modelId]) || [];
          for (const leafId of groupLeaves) {
            const leafBase = scene.tablesById.get(leafId);
            if (!leafBase) continue;
            const newX = leafBase.x + dx;
            const newY = leafBase.y + dy;
            overrideById.set(leafId, {
              ...leafBase,
              maxX: newX + leafBase.width,
              maxY: newY + leafBase.height,
              x: newX,
              y: newY,
            });
          }
          return overrideById;
        }

        function applyLiveDragTableRecord(scene, records, bounds) {
          const activeDrag = getActiveTableDrag();
          if (!activeDrag) {
            return records;
          }

          const overrideById = createLiveDragTableOverrides(scene, activeDrag);
          if (overrideById.size === 0) {
            return records;
          }

          const nextRecords = records.filter((record) => !overrideById.has(record.modelId));
          for (const override of overrideById.values()) {
            if (
              rectIntersectsBounds(
                override.x,
                override.y,
                override.width,
                override.height,
                bounds,
                0,
              )
            ) {
              nextRecords.push(override);
            }
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

          const scene = ensureSceneGraph();
          const overrideById = createLiveDragTableOverrides(scene, activeDrag);
          const movedModelIds = new Set(overrideById.keys());

          const filteredRecords = records.filter(
            (record) =>
              !movedModelIds.has(record.meta.sourceModelId) &&
              !movedModelIds.has(record.meta.targetModelId),
          );

          const liveRecords = collectLiveDragEdgeSegments(
            activeDrag,
            movedModelIds,
            bounds,
            overrideById,
          );
          latestLiveDragSegmentCount = liveRecords.length;
          return routeEdgeRecordsAroundLiveDragTables(
            filteredRecords,
            scene,
            overrideById,
            bounds,
          ).concat(liveRecords);
        }

        function routeEdgeRecordsAroundLiveDragTables(
          records,
          scene,
          overrideById,
          bounds,
        ) {
          const routedRecords = [];
          for (const record of records) {
            const routed = routeEdgePathAroundTables(
              [record.segment.start, record.segment.end],
              record.meta,
              scene,
              overrideById,
            );
            for (const segment of findSegments(routed.points)) {
              if (!segmentIntersectsBounds(
                segment.start.x,
                segment.start.y,
                segment.end.x,
                segment.end.y,
                bounds,
                80,
              )) {
                continue;
              }
              routedRecords.push({
                ...record,
                bounds: {
                  bottom: Math.max(segment.start.y, segment.end.y),
                  left: Math.min(segment.start.x, segment.end.x),
                  right: Math.max(segment.start.x, segment.end.x),
                  top: Math.min(segment.start.y, segment.end.y),
                },
                points: routed.points,
                segment,
              });
            }
          }
          return routedRecords;
        }

        function collectLiveDragEdgeSegments(
          activeDrag,
          movedModelIds,
          bounds,
          overrideById,
        ) {
          const visibleEdgeEntries = [];
          for (const meta of getActiveEdgeMeta()) {
            if (
              !movedModelIds.has(meta.sourceModelId) &&
              !movedModelIds.has(meta.targetModelId)
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

          const liveBundlingEnabled = Boolean(state.edgeBundling) && !renderModel.modelCatalogMode;
          let liveBundleAppCenters = null;
          let liveBundleSpatial = null;
          if (liveBundlingEnabled) {
            liveBundleAppCenters = computeAppClusterCenters(visibleEdgeEntries);
            const distinctApps = new Set();
            for (const entry of visibleEdgeEntries) {
              if (entry.sourceTable && entry.sourceTable.appLabel) {
                distinctApps.add(entry.sourceTable.appLabel);
              }
            }
            if (distinctApps.size < 3) {
              liveBundleSpatial = computeSpatialClusterContext(visibleEdgeEntries);
            }
          }

          function buildLiveEdgePath(entry) {
            if (liveBundlingEnabled && entry.sourceTable && entry.targetTable) {
              const sourceLabel = entry.sourceTable.appLabel || "";
              const targetLabel = entry.targetTable.appLabel || "";
              if (liveBundleAppCenters && sourceLabel && targetLabel && sourceLabel !== targetLabel) {
                const sourceCluster = liveBundleAppCenters.get(sourceLabel);
                const targetCluster = liveBundleAppCenters.get(targetLabel);
                if (sourceCluster && targetCluster) {
                  return buildBundledPath(
                    entry.sourcePosition,
                    entry.sourceTable,
                    entry.targetPosition,
                    entry.targetTable,
                    sourceCluster,
                    targetCluster,
                    0.85,
                  );
                }
              }
              if (liveBundleSpatial) {
                const srcKey = liveBundleSpatial.cellKey(entry.sourcePosition, entry.sourceTable);
                const tgtKey = liveBundleSpatial.cellKey(entry.targetPosition, entry.targetTable);
                if (srcKey !== tgtKey) {
                  return buildBundledPath(
                    entry.sourcePosition,
                    entry.sourceTable,
                    entry.targetPosition,
                    entry.targetTable,
                    liveBundleSpatial.cellCenter(srcKey),
                    liveBundleSpatial.cellCenter(tgtKey),
                    0.7,
                  );
                }
              }
            }
            return buildStraightPath(
              entry.sourcePosition,
              entry.sourceTable,
              entry.targetPosition,
              entry.targetTable,
            );
          }

          const routedEdges = renderModel.modelCatalogMode
            ? routeCatalogEdgesWithPorts(visibleEdgeEntries).map((routed) => ({
                edgeId: routed.entry.meta.edgeId,
                meta: routed.entry.meta,
                points: routed.points,
              }))
            : visibleEdgeEntries.map((entry) => ({
                edgeId: entry.meta.edgeId,
                meta: entry.meta,
                points: buildLiveEdgePath(entry),
              }));
          const records = [];

          for (const edge of routedEdges) {
            const routed = routeEdgePathAroundTables(
              edge.points,
              edge.meta,
              ensureSceneGraph(),
              overrideById,
            );
            edge.points = routed.points;
            for (const visibleSegment of findSegments(edge.points)) {
              if (
                !segmentIntersectsBounds(
                  visibleSegment.start.x,
                  visibleSegment.start.y,
                  visibleSegment.end.x,
                  visibleSegment.end.y,
                  bounds,
                  80,
                )
              ) {
                continue;
              }

              records.push({
                bounds: {
                  bottom: Math.max(visibleSegment.start.y, visibleSegment.end.y),
                  left: Math.min(visibleSegment.start.x, visibleSegment.end.x),
                  right: Math.max(visibleSegment.start.x, visibleSegment.end.x),
                  top: Math.min(visibleSegment.start.y, visibleSegment.end.y),
                },
                edgeId: edge.edgeId,
                meta: edge.meta,
                points: edge.points,
                segment: visibleSegment,
              });
            }
          }

          return records;
        }

        function collectVisibleOverlaySegments(bounds) {
          return renderedOverlays
            .filter((overlay) => overlay.active)
            .flatMap((overlay) => {
              const meta = {
                cssKind: "method-overlay",
                provenance: "overlay",
                sourceModelId: overlay.sourceModelId,
                targetModelId: overlay.targetModelId,
              };
              const routed = routeEdgePathAroundTables(
                [
                  { x: overlay.x1, y: overlay.y1 },
                  { x: overlay.x2, y: overlay.y2 },
                ],
                meta,
                ensureSceneGraph(),
              );
              return findSegments(routed.points).map((segment) => ({ meta, segment }));
            })
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
            const selectedClusterId = getSelectedClusterId();
            const clusterMember = isModelInSelectedCluster(record.modelId);
            const dimmed = Boolean(selectedClusterId && !clusterMember);
            const titleColor = dimmed ? "#708087" : "#f4f7f1";
            const subtitleColor = dimmed ? "#5f7076" : "#9fb7b0";

            labels.push(createLabelDescriptor(table.modelName, "700 14px Georgia, serif", titleColor, record.x + 14, record.y + 14, Math.max(40, record.width - 28)));
            if (zoom >= GPU_TABLE_SUBTITLE_ZOOM) {
              labels.push(createLabelDescriptor(table.databaseTableName, "500 12px Georgia, serif", subtitleColor, record.x + 14, record.y + 34, Math.max(40, record.width - 28)));
            }

            if (!allowDetails) {
              continue;
            }

            let cursorY = record.y + 56;
            for (const row of record.meta.fieldRows) {
              if (cursorY + 16 > record.y + record.height - 12) {
                break;
              }
              labels.push(createLabelDescriptor(row.text, "500 12px Georgia, serif", dimmed ? "#596a70" : row.tone === "enum-option" ? "#e4d3a7" : "#c7d7d4", record.x + 14, cursorY, Math.max(40, record.width - 28)));
              cursorY += 16;
            }

            if (record.options.showProperties) {
              for (const property of record.meta.properties) {
                if (cursorY + 16 > record.y + record.height - 12) {
                  break;
                }
                labels.push(createLabelDescriptor("@ " + property, "500 12px Georgia, serif", dimmed ? "#596a70" : "#a8d8ff", record.x + 14, cursorY, Math.max(40, record.width - 28)));
                cursorY += 16;
              }
            }

            if (record.options.showMethods) {
              for (const method of record.meta.methods) {
                if (cursorY + 16 > record.y + record.height - 12) {
                  break;
                }
                labels.push(createLabelDescriptor("fn " + method.name, "500 12px Georgia, serif", dimmed ? "#596a70" : "#ffcf8a", record.x + 14, cursorY, Math.max(40, record.width - 28)));
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
              isModelInSelectedCluster(record.modelId) ||
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

        function drawWebGpuScene(renderer, segments, overlays, tables, labels, leafBundles, leafTiles) {
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

            drawWebGpuLeafBundleBatch(renderer, pass, leafBundles || []);
            drawWebGpuSegmentBatch(renderer, pass, segments, false);
            drawWebGpuSegmentBatch(renderer, pass, overlays, true);
            drawWebGpuLeafTileBatch(renderer, pass, leafTiles || []);
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

        function appendLeafTileLabels(labels, tiles) {
          if (!tiles.length) {
            return;
          }
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          if (zoom < GPU_TABLE_LABEL_ZOOM) {
            return;
          }
          for (const tile of tiles) {
            if (!tile.modelName) {
              continue;
            }
            const maxWidth = Math.max(40, tile.width - 20);
            labels.push(createLabelDescriptor(
              tile.modelName,
              "600 13px Georgia, serif",
              "#f4f7f1",
              tile.x + 10,
              tile.y + Math.max(8, (tile.height - 14) / 2),
              maxWidth,
            ));
          }
        }

        function appendLeafBundleLabels(labels, bundles) {
          if (!bundles.length) {
            return;
          }
          const zoom = Math.max(state.viewport.zoom, MIN_VIEWPORT_ZOOM);
          if (zoom < GPU_TABLE_LABEL_ZOOM) {
            return;
          }
          for (const record of bundles) {
            const titleSource = record.parentName || record.parentModelId || "";
            if (!titleSource) {
              continue;
            }
            const isClusterOutline = record.kind === "cluster-outline";
            const title = isClusterOutline
              ? titleSource + " · " + record.leafCount + " members"
              : titleSource + " · " + record.leafCount + " leaves";
            const titleMaxWidth = Math.max(40, record.width - 28);
            labels.push(createLabelDescriptor(
              title,
              isClusterOutline ? "700 16px Georgia, serif" : "700 14px Georgia, serif",
              isClusterOutline ? "#b9d6e8" : "#f4f7f1",
              record.x + 14,
              isClusterOutline
                ? record.y + 10
                : record.y + Math.max(12, (record.height - 16) / 2),
              titleMaxWidth,
            ));
          }
        }

        function leafBundleColors(record) {
          const stroke = record.appLabel ? appStrokeColor(record.appLabel) : [0.66, 0.85, 1.0, 0.7];
          if (record.kind === "cluster-outline") {
            return {
              borderWidth: 3.0,
              cornerRadius: 24,
              fill: [stroke[0], stroke[1], stroke[2], 0.055],
              stroke: [stroke[0], stroke[1], stroke[2], 0.42],
            };
          }
          return {
            borderWidth: 2.0,
            cornerRadius: 16,
            fill: [0.06, 0.12, 0.18, 0.96],
            stroke: [stroke[0], stroke[1], stroke[2], 0.85],
          };
        }

        function leafTileColors(record) {
          const stroke = record.appLabel ? appStrokeColor(record.appLabel) : [0.66, 0.85, 1.0, 0.7];
          return {
            borderWidth: 1.4,
            cornerRadius: 12,
            fill: [0.10, 0.16, 0.22, 0.96],
            stroke: [stroke[0], stroke[1], stroke[2], 0.85],
          };
        }

        function fillLeafTileInstanceData(data, tiles) {
          for (let index = 0; index < tiles.length; index += 1) {
            const record = tiles[index];
            const colors = leafTileColors(record);
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
            data[offset + 12] = colors.cornerRadius;
            data[offset + 13] = colors.borderWidth;
          }
        }

        function drawLeafTileBatch(renderer, tiles) {
          if (!tiles.length) {
            return;
          }
          const gl = renderer.gl;
          const data = new Float32Array(tiles.length * 14);
          fillLeafTileInstanceData(data, tiles);

          const target = renderer.leafTile;
          gl.useProgram(target.program);
          bindCommonUniforms(gl, target.program);
          bindBufferData(gl, target.buffers.instances, data);
          bindCornerAttribute(gl, target.program, target.buffers.corners, "a_corner");
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_bounds", 4, 14, 0);
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_fill", 4, 14, 4);
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_stroke", 4, 14, 8);
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_style", 2, 14, 12);
          gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, tiles.length);
        }

        function drawWebGpuLeafTileBatch(renderer, pass, tiles) {
          if (!tiles.length) {
            return;
          }
          const data = new Float32Array(tiles.length * 14);
          fillLeafTileInstanceData(data, tiles);

          const target = renderer.leafTile;
          const buffer = ensureWebGpuInstanceBuffer(renderer.device, target, data.byteLength);
          renderer.device.queue.writeBuffer(buffer, 0, data);
          pass.setPipeline(target.pipeline);
          pass.setBindGroup(0, renderer.commonBindGroup);
          pass.setVertexBuffer(0, target.corners);
          pass.setVertexBuffer(1, buffer);
          pass.draw(4, tiles.length);
        }

        function fillLeafBundleInstanceData(data, bundles) {
          for (let index = 0; index < bundles.length; index += 1) {
            const record = bundles[index];
            const colors = leafBundleColors(record);
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
            data[offset + 12] = colors.cornerRadius;
            data[offset + 13] = colors.borderWidth;
          }
        }

        function drawLeafBundleBatch(renderer, bundles) {
          if (!bundles.length) {
            return;
          }

          const gl = renderer.gl;
          const data = new Float32Array(bundles.length * 14);
          fillLeafBundleInstanceData(data, bundles);

          const target = renderer.leafBundle;
          gl.useProgram(target.program);
          bindCommonUniforms(gl, target.program);
          bindBufferData(gl, target.buffers.instances, data);
          bindCornerAttribute(gl, target.program, target.buffers.corners, "a_corner");
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_bounds", 4, 14, 0);
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_fill", 4, 14, 4);
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_stroke", 4, 14, 8);
          bindInstancedFloat(gl, target.program, target.buffers.instances, "a_style", 2, 14, 12);
          gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, bundles.length);
        }

        function drawWebGpuLeafBundleBatch(renderer, pass, bundles) {
          if (!bundles.length) {
            return;
          }

          const data = new Float32Array(bundles.length * 14);
          fillLeafBundleInstanceData(data, bundles);

          const target = renderer.leafBundle;
          const buffer = ensureWebGpuInstanceBuffer(renderer.device, target, data.byteLength);
          renderer.device.queue.writeBuffer(buffer, 0, data);
          pass.setPipeline(target.pipeline);
          pass.setBindGroup(0, renderer.commonBindGroup);
          pass.setVertexBuffer(0, target.corners);
          pass.setVertexBuffer(1, buffer);
          pass.draw(4, bundles.length);
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
          const fittedFont = fitFontToWidth(
            atlas.context,
            label.font,
            label.text,
            label.maxWidth,
          );
          atlas.context.font = fittedFont;
          const measuredWidth = Math.ceil(
            atlas.context.measureText(label.text).width,
          );
          const textWidth = Math.max(
            2,
            Math.min(Math.floor(label.maxWidth), measuredWidth),
          );
          const key = fittedFont + "|" + label.color + "|" + textWidth + "|" + label.text;
          if (atlas.map.has(key)) {
            return atlas.map.get(key);
          }

          const fontSize = Math.max(
            GPU_MIN_LABEL_FONT_SIZE,
            Number.parseFloat(fittedFont.match(/([0-9.]+)px/)?.[1] || "12"),
          );
          const width = textWidth;
          const height = Math.max(14, Math.ceil(fontSize * 1.5));
          const slot = allocateAtlasSlot(atlas, width + 8, height + 8);
          if (!slot) {
            return null;
          }

          atlas.context.clearRect(slot.x, slot.y, slot.width, slot.height);
          atlas.context.font = fittedFont;
          atlas.context.fillStyle = label.color;
          atlas.context.textBaseline = "top";
          // Canvas maxWidth compresses only the rare label that is still too
          // wide at the readable minimum font. The complete text remains in
          // the card; it is never replaced with an ellipsis or painted past
          // the card boundary.
          atlas.context.fillText(
            label.text,
            slot.x + 4,
            slot.y + 4,
            width,
          );

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
            width,
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

        function fitFontToWidth(context, font, text, maxWidth) {
          context.font = font;
          const measuredWidth = context.measureText(text).width;
          if (measuredWidth <= maxWidth || measuredWidth <= 0) {
            return font;
          }
          const match = font.match(/([0-9.]+)px/);
          const baseSize = match ? Number.parseFloat(match[1]) : 12;
          const fittedSize = Math.max(
            GPU_MIN_LABEL_FONT_SIZE,
            Math.floor(baseSize * maxWidth / measuredWidth * 10) / 10,
          );
          return font.replace(/([0-9.]+)px/, fittedSize + "px");
        }

        function hashAppToHue(label) {
          if (!label) return 158;
          let hash = 0;
          for (let i = 0; i < label.length; i++) {
            hash = (hash * 31 + label.charCodeAt(i)) | 0;
          }
          return ((hash % 360) + 360) % 360;
        }

        function hslToRgb(hue, sat, light) {
          if (sat === 0) {
            return [light, light, light];
          }
          const q = light < 0.5 ? light * (1 + sat) : light + sat - light * sat;
          const p = 2 * light - q;
          const hue2rgb = (t) => {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1/6) return p + (q - p) * 6 * t;
            if (t < 1/2) return q;
            if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
            return p;
          };
          const h = hue / 360;
          return [hue2rgb(h + 1/3), hue2rgb(h), hue2rgb(h - 1/3)];
        }

        function appStrokeColor(label) {
          const rgb = hslToRgb(hashAppToHue(label), 0.6, 0.62);
          return [rgb[0], rgb[1], rgb[2], 0.78];
        }

        function appHeaderColor(label) {
          const rgb = hslToRgb(hashAppToHue(label), 0.55, 0.42);
          return [rgb[0], rgb[1], rgb[2], 0.92];
        }

        function getSelectedClusterId() {
          if (!state.selectedModelId) {
            return "";
          }
          const selectedTable = tableMetaById.get(state.selectedModelId);
          return selectedTable && selectedTable.clusterId
            ? selectedTable.clusterId
            : "";
        }

        function isModelInSelectedCluster(modelId) {
          const selectedClusterId = getSelectedClusterId();
          if (!selectedClusterId) {
            return false;
          }
          const table = tableMetaById.get(modelId);
          return Boolean(table && table.clusterId === selectedClusterId);
        }

        function edgeLogicalEndpointIds(meta) {
          return Array.isArray(meta.logicalEndpointModelIds)
            && meta.logicalEndpointModelIds.length > 0
            ? [...new Set(meta.logicalEndpointModelIds)]
            : [...new Set([meta.sourceModelId, meta.targetModelId])];
        }

        function edgeSelectedClusterRelation(meta) {
          if (!state.selectedModelId) {
            return "unfocused";
          }
          const selectedClusterId = getSelectedClusterId();
          if (!selectedClusterId) {
            return edgeLogicalEndpointIds(meta).includes(state.selectedModelId)
              ? "boundary"
              : "unrelated";
          }
          const endpointIds = edgeLogicalEndpointIds(meta);
          let knownEndpoints = 0;
          let selectedEndpoints = 0;
          for (const modelId of new Set(endpointIds)) {
            const table = tableMetaById.get(modelId);
            if (!table) {
              continue;
            }
            knownEndpoints += 1;
            if (table.clusterId === selectedClusterId) {
              selectedEndpoints += 1;
            }
          }
          if (selectedEndpoints === 0) {
            return "unrelated";
          }
          return knownEndpoints > 0 && selectedEndpoints === knownEndpoints
            ? "internal"
            : "boundary";
        }

        function tableColors(record) {
          const selected = state.selectedModelId === record.modelId;
          const methodTarget = isMethodTarget(record.modelId);
          const dragging = drag && drag.kind === "table" && drag.modelId === record.modelId;
          const appLabel = record.meta && record.meta.appLabel;
          const clusterId = record.meta && record.meta.clusterId;
          const selectedClusterId = getSelectedClusterId();
          const clusterMember = Boolean(
            selectedClusterId && clusterId === selectedClusterId,
          );
          const unrelated = Boolean(selectedClusterId && !clusterMember);
          const clusterStroke = appStrokeColor(selectedClusterId || appLabel);
          const defaultStroke = appStrokeColor(appLabel);

          return {
            borderWidth: selected || dragging ? 3.4 : clusterMember ? 2.8 : 2.0,
            fill: selected
              ? [0.15, 0.24, 0.22, 0.99]
              : clusterMember
                ? [0.075, 0.17, 0.19, 0.98]
                : unrelated
                  ? [0.035, 0.065, 0.09, 0.72]
                  : [0.06, 0.12, 0.18, 0.96],
            stroke: dragging
              ? [0.66, 0.85, 1.0, 0.9]
              : selected
                ? [1.0, 0.75, 0.41, 0.92]
                : clusterMember
                  ? [clusterStroke[0], clusterStroke[1], clusterStroke[2], 0.94]
                : methodTarget
                  ? [0.66, 0.85, 1.0, 0.74]
                  : unrelated
                    ? [defaultStroke[0], defaultStroke[1], defaultStroke[2], 0.14]
                    : defaultStroke,
          };
        }

        function edgeColor(meta) {
          const clusterRelation = edgeSelectedClusterRelation(meta);
          if (clusterRelation === "internal") {
            const color = appStrokeColor(getSelectedClusterId());
            return [color[0], color[1], color[2], 0.96];
          }
          if (clusterRelation === "boundary") {
            return [1.0, 0.75, 0.41, 0.86];
          }
          if (clusterRelation === "unrelated") {
            return [0.46, 0.58, 0.61, 0.10];
          }
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
          const clusterRelation = edgeSelectedClusterRelation(meta);
          if (clusterRelation === "internal") {
            return 5.4;
          }
          if (clusterRelation === "boundary") {
            return 4.4;
          }
          if (clusterRelation === "unrelated") {
            return 2.2;
          }
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
            "  let cap = (input.corner.x * 2.0 - 1.0) * input.halfWidth;",
            "  let world = base + tangent * cap + normal * input.corner.y * input.halfWidth;",
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
            "void main() { vec2 start = a_segment.xy; vec2 end = a_segment.zw; vec2 delta = end - start; float len = max(length(delta), 0.0001); vec2 tangent = delta / len; vec2 normal = vec2(-tangent.y, tangent.x); vec2 base = mix(start, end, a_corner.x); float cap = (a_corner.x * 2.0 - 1.0) * a_halfWidth; vec2 world = base + tangent * cap + normal * a_corner.y * a_halfWidth; vec2 screen = (world * u_zoom + u_pan) * u_deviceScale; vec2 clip = screen / u_canvas * 2.0 - 1.0; gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0); v_color = a_color; }";
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
