import { LAYOUT_WASM_BASE64 } from "./layoutWasmAsset";

export function getBrowserLayoutWasmSource(): string {
  const wasmBase64Json = JSON.stringify(LAYOUT_WASM_BASE64);

  return `
        const layoutWasmBase64 = ${wasmBase64Json};
        const layoutWasmTextEncoder =
          typeof TextEncoder === "function" ? new TextEncoder() : undefined;
        const layoutWasmRuntime = createLayoutWasmRuntime(layoutWasmBase64);

        function createLayoutWasmRuntime(base64) {
          if (!base64 || typeof WebAssembly === "undefined" || typeof atob !== "function") {
            return undefined;
          }

          try {
            const binary = atob(base64);
            const bytes = new Uint8Array(binary.length);
            for (let index = 0; index < binary.length; index += 1) {
              bytes[index] = binary.charCodeAt(index);
            }

            const module = new WebAssembly.Module(bytes);
            const instance = new WebAssembly.Instance(module, {});
            const exports = instance.exports || {};

            if (
              !exports.memory ||
              typeof exports.layout_wasm_alloc !== "function" ||
              typeof exports.layout_wasm_dealloc !== "function" ||
              typeof exports.layout_wasm_optimize_locations !== "function" ||
              typeof exports.layout_wasm_compute_bounds !== "function" ||
              typeof exports.layout_wasm_output_ptr !== "function" ||
              typeof exports.layout_wasm_output_len !== "function" ||
              typeof exports.layout_wasm_free_output !== "function"
            ) {
              return undefined;
            }

            return { exports };
          } catch {
            return undefined;
          }
        }

        function tryOptimizeLayoutWithWasm(tableMetaList, positions, settingsOverride) {
          if (!layoutWasmRuntime || !positions || !Array.isArray(tableMetaList)) {
            return undefined;
          }

          const input = encodeLayoutWasmOptimizerInput(tableMetaList, positions, settingsOverride);
          if (!input) {
            return undefined;
          }

          const output = callLayoutWasm(
            layoutWasmRuntime,
            "layout_wasm_optimize_locations",
            input,
          );

          return output ? decodeLayoutWasmPositions(output) : undefined;
        }

        function tryComputeLayoutBoundsWithWasm(tables, optionsByModelId, layout) {
          if (!layoutWasmRuntime || !tables || !layout) {
            return undefined;
          }

          const input = encodeLayoutWasmBoundsInput(tables, optionsByModelId, layout);
          if (!input) {
            return undefined;
          }

          const output = callLayoutWasm(
            layoutWasmRuntime,
            "layout_wasm_compute_bounds",
            input,
          );

          return output ? decodeLayoutWasmBounds(output) : undefined;
        }

        function callLayoutWasm(runtime, exportName, input) {
          const exports = runtime.exports;
          const inputPointer = exports.layout_wasm_alloc(input.length);
          if (!Number.isFinite(inputPointer) || inputPointer <= 0) {
            return undefined;
          }

          try {
            new Uint8Array(exports.memory.buffer, inputPointer, input.length).set(input);
            const outputHandle = exports[exportName](inputPointer, input.length);
            if (!outputHandle) {
              return undefined;
            }

            const outputPointer = exports.layout_wasm_output_ptr(outputHandle);
            const outputLength = exports.layout_wasm_output_len(outputHandle);
            if (!Number.isFinite(outputPointer) || !Number.isFinite(outputLength) || outputLength <= 0) {
              exports.layout_wasm_free_output(outputHandle);
              return undefined;
            }

            const output = new Uint8Array(outputLength);
            output.set(new Uint8Array(exports.memory.buffer, outputPointer, outputLength));
            exports.layout_wasm_free_output(outputHandle);
            return output;
          } catch {
            return undefined;
          } finally {
            exports.layout_wasm_dealloc(inputPointer, input.length);
          }
        }

        function encodeLayoutWasmOptimizerInput(tableMetaList, positions, settingsOverride) {
          const nodeEntries = createLayoutWasmNodeEntries(tableMetaList, positions);
          if (nodeEntries.length === 0) {
            return undefined;
          }

          const settings = createLayoutWasmOptimizerSettings(settingsOverride);
          const visibleModelIds = new Set(nodeEntries.map((entry) => entry.modelId));
          const edgeEntries = edgeMeta
            .filter((edge) =>
              edge.provenance === "declared" &&
              visibleModelIds.has(edge.sourceModelId) &&
              visibleModelIds.has(edge.targetModelId)
            )
            .map((edge) => ({
              sourceBytes: encodeLayoutWasmString(edge.sourceModelId),
              targetBytes: encodeLayoutWasmString(edge.targetModelId),
            }));
          const byteLength =
            8 +
            16 +
            nodeEntries.reduce((sum, entry) => sum + 4 + entry.idBytes.length + 8 * 4, 0) +
            edgeEntries.reduce(
              (sum, entry) => sum + 4 + entry.sourceBytes.length + 4 + entry.targetBytes.length,
              0,
            );
          const writer = createLayoutWasmWriter(byteLength);

          writer.writeU32(nodeEntries.length);
          writer.writeU32(edgeEntries.length);
          writer.writeF64(settings.nodeSpacing);
          writer.writeF64(settings.edgeDetour);
          nodeEntries.forEach((entry) => writer.writeNode(entry));
          edgeEntries.forEach((entry) => {
            writer.writeBytesWithLength(entry.sourceBytes);
            writer.writeBytesWithLength(entry.targetBytes);
          });

          return writer.bytes;
        }

        function createLayoutWasmOptimizerSettings(settingsOverride) {
          const settings =
            settingsOverride ||
            (typeof getAppliedLayoutSettings === "function"
              ? getAppliedLayoutSettings()
              : (state && state.settings) || {});

          return {
            edgeDetour: Number.isFinite(settings.edgeDetour) ? settings.edgeDetour : 1.35,
            nodeSpacing: Number.isFinite(settings.nodeSpacing) ? settings.nodeSpacing : 1.4,
          };
        }

        function encodeLayoutWasmBoundsInput(tables, optionsByModelId, layout) {
          const nodeEntries = [];

          for (const table of tables) {
            const options = optionsByModelId && optionsByModelId.get(table.modelId);
            if (options && options.hidden) {
              continue;
            }

            const position =
              (options && options.manualPosition) ||
              layout[table.modelId] ||
              table.basePosition || { x: 0, y: 0 };
            const entry = createLayoutWasmNodeEntry(table, position);
            if (entry) {
              nodeEntries.push(entry);
            }
          }

          if (nodeEntries.length === 0) {
            return undefined;
          }

          const byteLength =
            4 + nodeEntries.reduce((sum, entry) => sum + 4 + entry.idBytes.length + 8 * 4, 0);
          const writer = createLayoutWasmWriter(byteLength);

          writer.writeU32(nodeEntries.length);
          nodeEntries.forEach((entry) => writer.writeNode(entry));

          return writer.bytes;
        }

        function createLayoutWasmNodeEntries(tableMetaList, positions) {
          return tableMetaList
            .map((table) => createLayoutWasmNodeEntry(table, positions[table.modelId] || table.basePosition))
            .filter(Boolean);
        }

        function createLayoutWasmNodeEntry(table, position) {
          if (
            !table ||
            !position ||
            !Number.isFinite(position.x) ||
            !Number.isFinite(position.y) ||
            !Number.isFinite(table.width) ||
            !Number.isFinite(table.height)
          ) {
            return undefined;
          }

          return {
            height: table.height,
            idBytes: encodeLayoutWasmString(table.modelId),
            modelId: table.modelId,
            width: table.width,
            x: position.x,
            y: position.y,
          };
        }

        function createLayoutWasmWriter(byteLength) {
          const bytes = new Uint8Array(byteLength);
          const view = new DataView(bytes.buffer);
          let offset = 0;

          return {
            bytes,
            writeBytes(value) {
              bytes.set(value, offset);
              offset += value.length;
            },
            writeBytesWithLength(value) {
              this.writeU32(value.length);
              this.writeBytes(value);
            },
            writeF64(value) {
              view.setFloat64(offset, value, true);
              offset += 8;
            },
            writeNode(entry) {
              this.writeBytesWithLength(entry.idBytes);
              this.writeF64(entry.x);
              this.writeF64(entry.y);
              this.writeF64(entry.width);
              this.writeF64(entry.height);
            },
            writeU32(value) {
              view.setUint32(offset, value, true);
              offset += 4;
            },
          };
        }

        function decodeLayoutWasmPositions(bytes) {
          const reader = createLayoutWasmReader(bytes);
          const count = reader.readU32();
          const positions = {};

          for (let index = 0; index < count; index += 1) {
            const modelId = reader.readString();
            positions[modelId] = {
              x: round2(reader.readF64()),
              y: round2(reader.readF64()),
            };
          }

          return positions;
        }

        function decodeLayoutWasmBounds(bytes) {
          const reader = createLayoutWasmReader(bytes);
          const visibleCount = reader.readU32();
          const minX = round2(reader.readF64());
          const minY = round2(reader.readF64());
          const maxX = round2(reader.readF64());
          const maxY = round2(reader.readF64());

          return {
            maxX,
            maxY,
            minX,
            minY,
            visibleCount,
          };
        }

        function createLayoutWasmReader(bytes) {
          const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
          let offset = 0;

          return {
            readBytes(length) {
              const value = bytes.slice(offset, offset + length);
              offset += length;
              return value;
            },
            readF64() {
              const value = view.getFloat64(offset, true);
              offset += 8;
              return value;
            },
            readString() {
              const length = this.readU32();
              return decodeLayoutWasmString(this.readBytes(length));
            },
            readU32() {
              const value = view.getUint32(offset, true);
              offset += 4;
              return value;
            },
          };
        }

        function encodeLayoutWasmString(value) {
          const source = String(value || "");
          if (layoutWasmTextEncoder) {
            return layoutWasmTextEncoder.encode(source);
          }

          const bytes = new Uint8Array(source.length);
          for (let index = 0; index < source.length; index += 1) {
            bytes[index] = source.charCodeAt(index) & 0xff;
          }
          return bytes;
        }

        function decodeLayoutWasmString(bytes) {
          if (typeof TextDecoder === "function") {
            return new TextDecoder().decode(bytes);
          }

          let value = "";
          for (let index = 0; index < bytes.length; index += 1) {
            value += String.fromCharCode(bytes[index]);
          }
          return value;
        }
  `;
}
