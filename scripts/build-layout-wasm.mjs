import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..");
const crateManifest = path.join(repoRoot, "layout-wasm", "Cargo.toml");
const wasmTarget = "wasm32-unknown-unknown";
const artifactPath = path.join(
  repoRoot,
  "layout-wasm",
  "target",
  wasmTarget,
  "release",
  "django_erd_layout_wasm.wasm",
);
const assetPath = path.join(
  repoRoot,
  "src",
  "webview",
  "interaction",
  "runtime",
  "layoutWasmAsset.ts",
);

const build = spawnSync(
  "cargo",
  ["build", "--manifest-path", crateManifest, "--target", wasmTarget, "--release"],
  {
    cwd: repoRoot,
    encoding: "utf8",
    stdio: "pipe",
  },
);

if (build.status !== 0) {
  process.stderr.write(build.stdout);
  process.stderr.write(build.stderr);
  process.stderr.write(
    "\nFailed to build layout WASM. Install the Rust target with: rustup target add wasm32-unknown-unknown\n",
  );
  process.exit(build.status ?? 1);
}

const wasmBase64 = readFileSync(artifactPath).toString("base64");
mkdirSync(path.dirname(assetPath), { recursive: true });
writeFileSync(
  assetPath,
  `export const LAYOUT_WASM_BASE64 = ${JSON.stringify(wasmBase64)};\n`,
);
