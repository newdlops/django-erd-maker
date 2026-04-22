import path from "node:path";

import type {
  DiscoveryDiagnostic,
  WorkspaceRootSelection,
} from "./discoveryTypes";
import { findFilesNamed } from "./pathScanner";

export async function selectWorkspaceRoot(
  workspacePath: string,
): Promise<WorkspaceRootSelection> {
  const diagnostics: DiscoveryDiagnostic[] = [];
  const managePyFiles = await findFilesNamed(workspacePath, "manage.py");

  if (managePyFiles.length === 0) {
    diagnostics.push({
      code: "no_manage_py_found",
      message:
        "No manage.py file was found. Falling back to the opened workspace root for discovery.",
      severity: "warning",
    });

    return {
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
