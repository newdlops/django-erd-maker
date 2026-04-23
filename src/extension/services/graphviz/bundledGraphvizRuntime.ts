import path from "node:path";

export interface BundledGraphvizRuntime {
  binDirectory: string;
  dotPath: string;
  executableName: string;
  pluginDirectory: string;
  platformKey: string;
  runtimeRoot: string;
}

export function graphvizExecutableName(
  platform: string = process.platform,
): string {
  return platform === "win32" ? "dot.exe" : "dot";
}

export function graphvizPlatformKey(
  platform: string = process.platform,
  arch: string = process.arch,
): string {
  return `${platform}-${arch}`;
}

export function getBundledGraphvizRuntimeCandidates(
  extensionPath: string,
  platform: string = process.platform,
  arch: string = process.arch,
): BundledGraphvizRuntime[] {
  const platformKey = graphvizPlatformKey(platform, arch);
  const executableName = graphvizExecutableName(platform);
  const runtimeRoots = [
    path.join(extensionPath, "resources", "graphviz", platformKey),
    path.join(extensionPath, "resources", "graphviz", platform),
    path.join(extensionPath, "resources", "graphviz", "universal"),
  ];

  return runtimeRoots.map((runtimeRoot) => ({
    binDirectory: path.join(runtimeRoot, "bin"),
    dotPath: path.join(runtimeRoot, "bin", executableName),
    executableName,
    pluginDirectory: path.join(runtimeRoot, "lib", "graphviz"),
    platformKey,
    runtimeRoot,
  }));
}

export function expectedBundledGraphvizRuntimeRoot(
  extensionPath: string,
  platform: string = process.platform,
  arch: string = process.arch,
): string {
  return getBundledGraphvizRuntimeCandidates(extensionPath, platform, arch)[0]
    .runtimeRoot;
}

export function createBundledGraphvizEnvironment(
  baseEnvironment: Record<string, string | undefined>,
  runtime: BundledGraphvizRuntime,
  platform: string = process.platform,
): Record<string, string | undefined> {
  const environment: Record<string, string | undefined> = {
    ...baseEnvironment,
    GVBINDIR: runtime.pluginDirectory,
  };
  const libraryDirectory = path.join(runtime.runtimeRoot, "lib");

  environment.PATH = prependPathEntries(
    [runtime.binDirectory, libraryDirectory],
    baseEnvironment.PATH,
    platform,
  );

  if (platform === "linux") {
    environment.LD_LIBRARY_PATH = prependPathEntries(
      [libraryDirectory],
      baseEnvironment.LD_LIBRARY_PATH,
      platform,
    );
  }

  if (platform === "darwin") {
    environment.DYLD_LIBRARY_PATH = prependPathEntries(
      [libraryDirectory],
      baseEnvironment.DYLD_LIBRARY_PATH,
      platform,
    );
    environment.DYLD_FALLBACK_LIBRARY_PATH = prependPathEntries(
      [libraryDirectory],
      baseEnvironment.DYLD_FALLBACK_LIBRARY_PATH,
      platform,
    );
  }

  return environment;
}

function prependPathEntries(
  entries: string[],
  existingValue: string | undefined,
  platform: string,
): string {
  const delimiter = platform === "win32" ? ";" : ":";
  const segments = [
    ...entries,
    ...(existingValue ? existingValue.split(delimiter) : []),
  ].filter((segment) => segment.length > 0);
  const uniqueSegments: string[] = [];
  const seen = new Set<string>();

  for (const segment of segments) {
    if (seen.has(segment)) {
      continue;
    }
    seen.add(segment);
    uniqueSegments.push(segment);
  }

  return uniqueSegments.join(delimiter);
}
