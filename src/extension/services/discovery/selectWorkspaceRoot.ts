import path from "node:path";

import type {
  DiscoveryDiagnostic,
  WorkspaceRootSelection,
} from "./discoveryTypes";
import {
  scanDirectoryTree,
  scanImmediateChildren,
  type ScannedDirectory,
} from "./pathScanner";

export interface WorkspaceRootSelectionResult extends WorkspaceRootSelection {
  directorySnapshot?: ScannedDirectory[];
}

export async function selectWorkspaceRoot(
  workspacePath: string,
): Promise<WorkspaceRootSelectionResult> {
  const diagnostics: DiscoveryDiagnostic[] = [];
  const rootManagePyPath = path.join(workspacePath, "manage.py");

  // A manage.py at the opened root is necessarily the shallowest candidate.
  // Avoid walking a large monorepo merely to rediscover that deterministic
  // answer.
  const rootEntries = await scanImmediateChildren(workspacePath);
  if (rootEntries.files.includes(rootManagePyPath)) {
    return {
      diagnostics,
      selectedRoot: workspacePath,
      strategy: "manage_py",
    };
  }

  const directorySnapshot = await scanDirectoryTree(workspacePath);
  const managePyFiles = directorySnapshot.flatMap((entry) =>
    entry.files.filter((filePath) => path.basename(filePath) === "manage.py")
  );

  if (managePyFiles.length === 0) {
    diagnostics.push({
      code: "no_manage_py_found",
      message:
        "No manage.py file was found. Falling back to the opened workspace root for discovery.",
      severity: "warning",
    });

    return {
      directorySnapshot,
      diagnostics,
      selectedRoot: workspacePath,
      strategy: "workspace_fallback",
    };
  }

  const candidateRoots = managePyFiles
    .map((filePath) => path.dirname(filePath))
    .sort((left, right) => {
      const depthDelta = getPathDepth(left) - getPathDepth(right);

      if (depthDelta !== 0) {
        return depthDelta;
      }

      return left.localeCompare(right);
    });

  if (candidateRoots.length > 1) {
    diagnostics.push({
      code: "multiple_manage_py_roots",
      message: `Multiple manage.py roots were found. Using ${toPosixPath(candidateRoots[0])}.`,
      severity: "warning",
    });
  }

  return {
    directorySnapshot,
    diagnostics,
    selectedRoot: candidateRoots[0],
    strategy: "manage_py",
  };
}

function getPathDepth(filePath: string): number {
  return filePath.split(path.sep).filter(Boolean).length;
}

function toPosixPath(filePath: string): string {
  return filePath.split(path.sep).join("/");
}
