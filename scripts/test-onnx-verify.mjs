// Load Python-prepared input tensors, run ONNX in Node, compare with PyTorch.
import * as ort from "onnxruntime-node";
import { readFileSync } from "node:fs";

// Minimal .npy loader (float32 / int64 only)
function loadNpy(path) {
  const buf = readFileSync(path);
  // npy magic + version
  if (buf[0] !== 0x93 || buf.slice(1, 6).toString() !== "NUMPY") {
    throw new Error("not npy");
  }
  const headerLen = buf.readUInt16LE(8);
  const header = buf.slice(10, 10 + headerLen).toString().trim();
  // Header is python dict literal: {'descr': '<f4', 'fortran_order': False, 'shape': (1250, 10), }
  const descrMatch = header.match(/'descr':\s*'([^']+)'/);
  const shapeMatch = header.match(/'shape':\s*\(([^)]+)\)/);
  const descr = descrMatch[1];
  const shape = shapeMatch[1].split(",").map(s => s.trim()).filter(s => s.length)
                              .map(Number);
  const dataStart = 10 + headerLen;
  const dataBuf = buf.slice(dataStart);
  if (descr === "<f4") {
    return { dtype: "float32", shape,
             data: new Float32Array(dataBuf.buffer,
                                    dataBuf.byteOffset,
                                    dataBuf.byteLength / 4) };
  } else if (descr === "<i8") {
    return { dtype: "int64", shape,
             data: new BigInt64Array(dataBuf.buffer,
                                     dataBuf.byteOffset,
                                     dataBuf.byteLength / 8) };
  }
  throw new Error(`unsupported dtype ${descr}`);
}

async function main() {
  const inputs = {};
  for (const name of ["x", "app_idx", "baseline", "edge_index", "edge_attr"]) {
    const npy = loadNpy(`tmp/${name}.npy`);
    inputs[name] = new ort.Tensor(npy.dtype, npy.data, npy.shape);
    console.log(`  ${name}: ${npy.dtype} shape=[${npy.shape.join(",")}]`);
  }

  const session = await ort.InferenceSession.create(
    "data/erd-poc/checkpoints/v12-fnv.onnx",
    { intraOpNumThreads: 4 },
  );
  const out = await session.run(inputs);
  const pos = out.positions.data;
  console.log(`output: [${out.positions.dims.join(",")}]`);
  for (let i = 0; i < 3; i++) {
    console.log(`  node[${i}]: norm=(${pos[i*2].toFixed(4)},`
                + `${pos[i*2+1].toFixed(4)})`);
  }
  console.log(`\nExpected (PyTorch v12-fnv):`);
  console.log(`  node[0]: norm=(-0.4293,0.2394)`);
  console.log(`  node[1]: norm=(-0.5369,0.4834)`);
  console.log(`  node[2]: norm=(-0.1273,0.3028)`);
}
main().catch(e => { console.error(e); process.exit(1); });
