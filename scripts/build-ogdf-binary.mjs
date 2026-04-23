import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { execFile } from "node:child_process";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const defaultExtensionRoot = path.resolve(__dirname, "..");

const options = parseArgs(process.argv.slice(2));
const extensionRoot = path.resolve(options["extension-root"] ?? defaultExtensionRoot);
const installDir = path.resolve(options["install-dir"] ?? path.join(extensionRoot, "bin", "ogdf", platformKey()));
const cacheRoot = path.resolve(options["cache-root"] ?? path.join(os.tmpdir(), "django-erd-ogdf-build", platformKey()));
const sourceArchive = path.resolve(
  options["source-archive"] ?? path.join(extensionRoot, "vendor", "ogdf", "ogdf-foxglove-202510.tar.gz"),
);
const wrapperRoot = path.resolve(
  options["wrapper-root"] ?? path.join(extensionRoot, "native", "ogdf-layout"),
);

await ensureFile(sourceArchive, "OGDF source archive");
await ensureFile(path.join(wrapperRoot, "CMakeLists.txt"), "OGDF wrapper CMakeLists.txt");

const sourceParent = path.join(cacheRoot, "source");
const extractedSourceRoot = path.join(sourceParent, "ogdf-foxglove-202510");
const buildRoot = path.join(cacheRoot, "build");
const outputBinary = path.join(installDir, binaryName());

await fs.mkdir(installDir, { recursive: true });
await fs.mkdir(sourceParent, { recursive: true });

if (!(await pathExists(path.join(extractedSourceRoot, "CMakeLists.txt")))) {
  await execFileAsync("tar", ["-xzf", sourceArchive, "-C", sourceParent], {
    cwd: extensionRoot,
    maxBuffer: 100 * 1024 * 1024,
  });
}

await execFileAsync(
  "cmake",
  [
    "-S",
    wrapperRoot,
    "-B",
    buildRoot,
    `-DOGDF_SOURCE_DIR=${extractedSourceRoot}`,
    "-DCMAKE_BUILD_TYPE=Release",
  ],
  {
    cwd: extensionRoot,
    maxBuffer: 100 * 1024 * 1024,
  },
);

await execFileAsync(
  "cmake",
  [
    "--build",
    buildRoot,
    "--config",
    "Release",
    "--target",
    "django-erd-ogdf-layout",
    "--parallel",
    String(Math.max(1, Math.min(8, os.cpus().length || 1))),
  ],
  {
    cwd: extensionRoot,
    maxBuffer: 100 * 1024 * 1024,
  },
);

const builtBinary = await resolveBuiltBinary(buildRoot);
await fs.copyFile(builtBinary, outputBinary);

if (process.platform !== "win32") {
  await fs.chmod(outputBinary, 0o755);
}

process.stdout.write(
  JSON.stringify({
    binary: outputBinary,
    buildRoot,
    sourceRoot: extractedSourceRoot,
  }),
);

function binaryName() {
  return process.platform === "win32"
    ? "django-erd-ogdf-layout.exe"
    : "django-erd-ogdf-layout";
}

function platformKey() {
  return `${process.platform}-${process.arch}`;
}

function parseArgs(argv) {
  const parsed = {};

  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index];
    const value = argv[index + 1];

    if (!flag?.startsWith("--") || value === undefined) {
      throw new Error(`Invalid argument sequence near ${flag ?? "<eof>"}`);
    }

    parsed[flag.slice(2)] = value;
  }

  return parsed;
}

async function ensureFile(filePath, label) {
  if (!(await pathExists(filePath))) {
    throw new Error(`${label} is missing: ${filePath}`);
  }
}

async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

async function resolveBuiltBinary(buildRoot) {
  const candidates = [
    path.join(buildRoot, binaryName()),
    path.join(buildRoot, "Release", binaryName()),
  ];

  for (const candidate of candidates) {
    if (await pathExists(candidate)) {
      return candidate;
    }
  }

  throw new Error(`Built OGDF binary was not found under ${buildRoot}`);
}
