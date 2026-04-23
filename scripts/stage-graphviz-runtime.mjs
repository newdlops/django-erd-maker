import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "..");

const runtimeSource = process.env.GRAPHVIZ_RUNTIME_SOURCE;
const dotSource = process.env.GRAPHVIZ_DOT_SOURCE;
const platformKey = `${process.platform}-${process.arch}`;
const targetRoot = path.join(repoRoot, "resources", "graphviz", platformKey);
const dotFileName = process.platform === "win32" ? "dot.exe" : "dot";

await fs.mkdir(path.dirname(targetRoot), { recursive: true });
await fs.rm(targetRoot, { force: true, recursive: true });

let bundleMetadata = {
  arch: process.arch,
  dotPath: path.relative(repoRoot, path.join(targetRoot, "bin", dotFileName)),
  platform: process.platform,
  runtimeSource: runtimeSource || null,
  dotSource: dotSource || null,
  sourceKind: "custom",
  stagedAt: new Date().toISOString(),
};

if (runtimeSource) {
  await stageRuntimeDirectory(runtimeSource, targetRoot);
} else if (dotSource) {
  await stageDotBinary(dotSource, targetRoot);
} else if (process.platform === "darwin") {
  const graphvizPrefix = resolveBrewPrefix("graphviz");
  await stageDarwinHomebrewGraphviz(graphvizPrefix, targetRoot);
  bundleMetadata = {
    ...bundleMetadata,
    runtimeSource: graphvizPrefix,
    sourceKind: "homebrew-graphviz",
  };
} else {
  throw new Error(
    "Set GRAPHVIZ_RUNTIME_SOURCE or GRAPHVIZ_DOT_SOURCE before running bundle:graphviz.",
  );
}

const dotPath = path.join(targetRoot, "bin", dotFileName);
await fs.access(dotPath);

if (process.platform !== "win32") {
  await fs.chmod(dotPath, 0o755);
}

await fs.writeFile(
  path.join(targetRoot, "bundle.json"),
  JSON.stringify(bundleMetadata, null, 2),
  "utf8",
);

console.log(`Graphviz runtime staged at ${targetRoot}`);

async function stageRuntimeDirectory(sourceRoot, destinationRoot) {
  const sourceStats = await fs.stat(sourceRoot);
  if (!sourceStats.isDirectory()) {
    throw new Error(`GRAPHVIZ_RUNTIME_SOURCE must point to a directory: ${sourceRoot}`);
  }

  await fs.cp(sourceRoot, destinationRoot, {
    dereference: true,
    recursive: true,
  });
}

async function stageDotBinary(sourceDotPath, destinationRoot) {
  const sourceStats = await fs.stat(sourceDotPath);
  if (!sourceStats.isFile()) {
    throw new Error(`GRAPHVIZ_DOT_SOURCE must point to a file: ${sourceDotPath}`);
  }

  await fs.mkdir(path.join(destinationRoot, "bin"), { recursive: true });
  await fs.copyFile(sourceDotPath, path.join(destinationRoot, "bin", dotFileName));
}

async function stageDarwinHomebrewGraphviz(prefixPath, destinationRoot) {
  const graphvizRoot = await fs.realpath(prefixPath);
  const binRoot = path.join(destinationRoot, "bin");
  const libRoot = path.join(destinationRoot, "lib");
  const pluginRoot = path.join(libRoot, "graphviz");
  const licenseRoot = path.join(destinationRoot, "licenses");
  const machFiles = [];
  const queuedFiles = [];
  const copiedLibraries = new Map();
  const graphvizConfigPath = path.join(graphvizRoot, "lib", "graphviz", detectGraphvizConfigName(graphvizRoot));
  const seedFiles = [
    [path.join(graphvizRoot, "bin", "dot"), path.join(binRoot, "dot")],
    [path.join(graphvizRoot, "lib", "graphviz", "libgvplugin_core.8.dylib"), path.join(pluginRoot, "libgvplugin_core.8.dylib")],
    [path.join(graphvizRoot, "lib", "graphviz", "libgvplugin_dot_layout.8.dylib"), path.join(pluginRoot, "libgvplugin_dot_layout.8.dylib")],
    [path.join(graphvizRoot, "lib", "graphviz", "libgvplugin_neato_layout.8.dylib"), path.join(pluginRoot, "libgvplugin_neato_layout.8.dylib")],
  ];

  await fs.mkdir(binRoot, { recursive: true });
  await fs.mkdir(pluginRoot, { recursive: true });
  await fs.mkdir(licenseRoot, { recursive: true });

  for (const [sourcePath, targetPath] of seedFiles) {
    await copyBundledFile(sourcePath, targetPath);
    copiedLibraries.set(await fs.realpath(sourcePath), targetPath);
    machFiles.push(targetPath);
    queuedFiles.push(targetPath);
  }

  await copyBundledFile(graphvizConfigPath, path.join(pluginRoot, path.basename(graphvizConfigPath)));
  await copyLicenseIfPresent(
    path.join(graphvizRoot, "COPYING"),
    path.join(licenseRoot, "graphviz-COPYING"),
  );

  while (queuedFiles.length > 0) {
    const currentTargetPath = queuedFiles.pop();
    const dependencies = listMachODependencies(currentTargetPath);

    for (const dependencyPath of dependencies) {
      if (!dependencyPath.startsWith("/opt/homebrew/")) {
        continue;
      }

      const resolvedDependencyPath = await fs.realpath(dependencyPath);
      const targetDependencyPath = mapDependencyTargetPath(
        resolvedDependencyPath,
        destinationRoot,
      );
      if (targetDependencyPath === currentTargetPath) {
        copiedLibraries.set(resolvedDependencyPath, targetDependencyPath);
        continue;
      }

      if (!copiedLibraries.has(resolvedDependencyPath)) {
        await copyBundledFile(resolvedDependencyPath, targetDependencyPath);
        copiedLibraries.set(resolvedDependencyPath, targetDependencyPath);
        machFiles.push(targetDependencyPath);
        queuedFiles.push(targetDependencyPath);
      }
    }
  }

  for (const machFilePath of machFiles) {
    await rewriteMachOPaths(machFilePath, destinationRoot);
  }

  adhocSignMachFiles(machFiles);
  verifyNoHomebrewReferences(machFiles);
  verifyBundledDotCanRun(destinationRoot);
}

function resolveBrewPrefix(formula) {
  return execFileSync("brew", ["--prefix", formula], {
    encoding: "utf8",
  }).trim();
}

function detectGraphvizConfigName(graphvizRoot) {
  const pluginRoot = path.join(graphvizRoot, "lib", "graphviz");
  const entries = execFileSync("find", [pluginRoot, "-maxdepth", "1", "-name", "config*"], {
    encoding: "utf8",
  })
    .split("\n")
    .map((entry) => entry.trim())
    .filter((entry) => entry.length > 0)
    .sort();

  const configPath = entries.at(-1);
  if (!configPath) {
    throw new Error(`Graphviz plugin config was not found under ${pluginRoot}`);
  }

  return path.basename(configPath);
}

function listMachODependencies(filePath) {
  const output = execFileSync("otool", ["-L", filePath], {
    encoding: "utf8",
  });
  const dependencies = [];
  const lines = output.split("\n").slice(1);

  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    const dependencyPath = trimmed.split(" (compatibility version")[0];
    if (dependencyPath.length > 0) {
      dependencies.push(dependencyPath);
    }
  }

  return dependencies;
}

async function rewriteMachOPaths(filePath, destinationRoot) {
  const dependencies = listMachODependencies(filePath);
  for (const dependencyPath of dependencies) {
    if (!dependencyPath.startsWith("/opt/homebrew/")) {
      continue;
    }

    const rewrittenDependencyPath = bundledInstallNameForDependency(
      filePath,
      dependencyPath,
      destinationRoot,
    );
    execFileSync(
      "install_name_tool",
      ["-change", dependencyPath, rewrittenDependencyPath, filePath],
      { stdio: "inherit" },
    );
  }

  if (path.basename(filePath) === "dot") {
    return;
  }

  execFileSync(
    "install_name_tool",
    ["-id", bundledInstallId(filePath, destinationRoot), filePath],
    { stdio: "inherit" },
  );
}

function bundledInstallNameForDependency(filePath, dependencyPath, destinationRoot) {
  const resolvedDependencyPath = path.resolve(mapDependencyTargetPath(dependencyPath, destinationRoot));
  const relativePath = path.relative(path.dirname(filePath), resolvedDependencyPath);
  const posixRelativePath = relativePath.split(path.sep).join("/");
  return `@loader_path/${posixRelativePath}`;
}

function bundledInstallId(filePath, destinationRoot) {
  if (filePath.startsWith(path.join(destinationRoot, "lib", "graphviz"))) {
    return `@loader_path/${path.basename(filePath)}`;
  }

  if (filePath.startsWith(path.join(destinationRoot, "lib"))) {
    return `@loader_path/${path.basename(filePath)}`;
  }

  return `@loader_path/${path.basename(filePath)}`;
}

function mapDependencyTargetPath(dependencyPath, destinationRoot) {
  const normalizedDependencyPath = path.resolve(dependencyPath);
  if (normalizedDependencyPath.includes(`${path.sep}lib${path.sep}graphviz${path.sep}`)) {
    return path.join(destinationRoot, "lib", "graphviz", path.basename(normalizedDependencyPath));
  }

  return path.join(destinationRoot, "lib", path.basename(normalizedDependencyPath));
}

function verifyNoHomebrewReferences(machFiles) {
  for (const machFilePath of machFiles) {
    const output = execFileSync("otool", ["-L", machFilePath], {
      encoding: "utf8",
    });
    if (output.includes("/opt/homebrew/")) {
      throw new Error(`Homebrew reference remained in bundled file: ${machFilePath}`);
    }
  }
}

function adhocSignMachFiles(machFiles) {
  const signingOrder = [...machFiles].sort((left, right) => {
    const leftRank = left.includes(`${path.sep}bin${path.sep}`) ? 1 : 0;
    const rightRank = right.includes(`${path.sep}bin${path.sep}`) ? 1 : 0;
    return leftRank - rightRank || left.localeCompare(right);
  });

  for (const machFilePath of signingOrder) {
    execFileSync(
      "codesign",
      ["--force", "--sign", "-", machFilePath],
      { stdio: "inherit" },
    );
  }
}

function verifyBundledDotCanRun(destinationRoot) {
  const dotPath = path.join(destinationRoot, "bin", "dot");
  const bundledEnvironment = {
    ...process.env,
    GVBINDIR: path.join(destinationRoot, "lib", "graphviz"),
    PATH: [
      path.join(destinationRoot, "bin"),
      path.join(destinationRoot, "lib"),
      process.env.PATH || "",
    ].filter(Boolean).join(":"),
    DYLD_FALLBACK_LIBRARY_PATH: [
      path.join(destinationRoot, "lib"),
      process.env.DYLD_FALLBACK_LIBRARY_PATH || "",
    ].filter(Boolean).join(":"),
    DYLD_LIBRARY_PATH: [
      path.join(destinationRoot, "lib"),
      process.env.DYLD_LIBRARY_PATH || "",
    ].filter(Boolean).join(":"),
  };
  const versionCheck = spawnSync(dotPath, ["-V"], {
    encoding: "utf8",
    env: bundledEnvironment,
  });
  if (versionCheck.status !== 0) {
    throw new Error(
      [
        `Bundled dot failed to report its version.`,
        versionCheck.stderr?.trim(),
        versionCheck.stdout?.trim(),
      ].filter(Boolean).join("\n"),
    );
  }

  const plainCheck = spawnSync(dotPath, ["-Kdot", "-Tplain"], {
    encoding: "utf8",
    env: bundledEnvironment,
    input: "digraph { a -> b }\n",
  });
  if (plainCheck.status !== 0) {
    throw new Error(
      [
        `Bundled dot failed to produce plain output.`,
        plainCheck.stderr?.trim(),
        plainCheck.stdout?.trim(),
      ].filter(Boolean).join("\n"),
    );
  }
}

async function copyBundledFile(sourcePath, destinationPath) {
  await fs.mkdir(path.dirname(destinationPath), { recursive: true });
  await fs.copyFile(sourcePath, destinationPath);
  const sourceStats = await fs.stat(sourcePath);
  await fs.chmod(destinationPath, sourceStats.mode);
}

async function copyLicenseIfPresent(sourcePath, destinationPath) {
  try {
    await copyBundledFile(sourcePath, destinationPath);
  } catch {
    // License files are best-effort metadata and may not exist for every source.
  }
}
