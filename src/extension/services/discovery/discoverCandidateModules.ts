import path from "node:path";

import type {
  DiscoveredCandidateModule,
  DiscoveredDjangoApp,
} from "./discoveryTypes";
import { collectPythonFiles } from "./pathScanner";

export async function discoverCandidateModules(
  selectedRoot: string,
  apps: DiscoveredDjangoApp[],
): Promise<DiscoveredCandidateModule[]> {
  const pythonFiles = await collectPythonFiles(selectedRoot);
  const appRoots = apps
    .map((app) => ({
      appLabel: app.appLabel,
      rootPath: path.join(selectedRoot, fromPosixPath(app.appPath)),
    }))
    .sort((left, right) => right.rootPath.length - left.rootPath.length);
  const modules = pythonFiles
    .filter((filePath) => path.basename(filePath) !== "manage.py")
    .map((filePath) => {
      const relativePath = toPosixPath(path.relative(selectedRoot, filePath));
      return {
        appLabel: inferAppLabel(selectedRoot, filePath, relativePath, appRoots),
        filePath: relativePath,
      } satisfies DiscoveredCandidateModule;
    });

  return dedupeAndSort(modules);
}

function dedupeAndSort(
  modules: DiscoveredCandidateModule[],
): DiscoveredCandidateModule[] {
  const seen = new Set<string>();
  const deduped = modules.filter((module) => {
    const key = `${module.appLabel}:${module.filePath}`;
    if (seen.has(key)) {
      return false;
    }

    seen.add(key);
    return true;
  });

  deduped.sort((left, right) => {
    const leftKey = `${left.appLabel}:${left.filePath}`;
    const rightKey = `${right.appLabel}:${right.filePath}`;
    return leftKey.localeCompare(rightKey);
  });

  return deduped;
}

function inferAppLabel(
  selectedRoot: string,
  absoluteFilePath: string,
  relativePath: string,
  appRoots: Array<{ appLabel: string; rootPath: string }>,
): string {
  for (const appRoot of appRoots) {
    if (
      absoluteFilePath === appRoot.rootPath ||
      absoluteFilePath.startsWith(`${appRoot.rootPath}${path.sep}`)
    ) {
      return appRoot.appLabel;
    }
  }

  const segments = relativePath.split("/").filter(Boolean);
  if (segments.length >= 2) {
    return segments[0];
  }

  return path.basename(selectedRoot);
}

function fromPosixPath(filePath: string): string {
  return filePath.split("/").join(path.sep);
}

function toPosixPath(filePath: string): string {
  return filePath.split(path.sep).join("/");
}
