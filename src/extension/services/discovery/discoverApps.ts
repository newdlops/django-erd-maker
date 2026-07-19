import path from "node:path";

import type {
  DiscoveredDjangoApp,
  DiscoveryDiagnostic,
} from "./discoveryTypes";
import { scanDirectoryTree, type ScannedDirectory } from "./pathScanner";

export interface DiscoverAppsResult {
  apps: DiscoveredDjangoApp[];
  diagnostics: DiscoveryDiagnostic[];
}

export async function discoverApps(
  selectedRoot: string,
  existingSnapshot?: ScannedDirectory[],
): Promise<DiscoverAppsResult> {
  const diagnostics: DiscoveryDiagnostic[] = [];
  const apps: DiscoveredDjangoApp[] = [];
  const directorySnapshot = existingSnapshot
    ? existingSnapshot.filter((entry) => isPathWithin(selectedRoot, entry.directoryPath))
    : await scanDirectoryTree(selectedRoot);
  const snapshotByPath = new Map(
    directorySnapshot.map((entry) => [entry.directoryPath, entry] as const),
  );
  if (!existingSnapshot) {
    const managePyFiles = directorySnapshot.flatMap((entry) =>
      entry.files.filter((filePath) => path.basename(filePath) === "manage.py")
    );
    if (managePyFiles.length > 1) {
      diagnostics.push({
        code: "multiple_manage_py_roots",
        message: `Multiple manage.py roots were found. Using ${toPosixPath(selectedRoot)}.`,
        severity: "warning",
      });
    }
  }
  const candidateDirectories = directorySnapshot.filter(
    (entry) => path.basename(entry.directoryPath) !== "models",
  );

  for (const directory of candidateDirectories) {
    const appDiagnostics: DiscoveryDiagnostic[] = [];
    const app = maybeDiscoverApp(
      directory,
      selectedRoot,
      directorySnapshot,
      snapshotByPath,
      appDiagnostics,
    );
    diagnostics.push(...appDiagnostics);
    if (app) {
      apps.push(app);
    }
  }

  apps.sort((left, right) => left.appLabel.localeCompare(right.appLabel));

  if (apps.length === 0) {
    diagnostics.push({
      code: "no_django_apps_found",
      message:
        "No Django app directories with models were discovered under the selected workspace root.",
      severity: "warning",
    });
  }

  return {
    apps,
    diagnostics,
  };
}

function maybeDiscoverApp(
  scanResult: ScannedDirectory,
  selectedRoot: string,
  directorySnapshot: ScannedDirectory[],
  snapshotByPath: Map<string, ScannedDirectory>,
  diagnostics: DiscoveryDiagnostic[],
): DiscoveredDjangoApp | undefined {
  const directoryPath = scanResult.directoryPath;
  const hasAppConfig = scanResult.files.some(
    (filePath) => path.basename(filePath) === "apps.py",
  );
  const hasModelsPy = scanResult.files.some(
    (filePath) => path.basename(filePath) === "models.py",
  );
  const modelsPackagePath = scanResult.directories.find(
    (filePath) => path.basename(filePath) === "models",
  );
  const hasModelsPackage = modelsPackagePath !== undefined;

  if (!hasAppConfig && !hasModelsPy && !hasModelsPackage) {
    return undefined;
  }

  const candidateModelFiles = collectCandidateModelFiles(
    directoryPath,
    modelsPackagePath,
    selectedRoot,
    scanResult,
    directorySnapshot,
    snapshotByPath,
    diagnostics,
  );

  if (candidateModelFiles.length === 0) {
    diagnostics.push({
      code: "app_without_model_modules",
      message: `Discovered app ${path.basename(directoryPath)} but no model modules were found.`,
      severity: "info",
    });
  }

  return {
    appLabel: path.basename(directoryPath),
    appPath: toPosixPath(path.relative(selectedRoot, directoryPath) || "."),
    candidateModelFiles,
    hasAppConfig,
    hasModelsPackage,
    hasModelsPy,
  };
}

function collectCandidateModelFiles(
  appDirectoryPath: string,
  modelsPackagePath: string | undefined,
  selectedRoot: string,
  appScan: ScannedDirectory,
  directorySnapshot: ScannedDirectory[],
  snapshotByPath: Map<string, ScannedDirectory>,
  diagnostics: DiscoveryDiagnostic[],
): string[] {
  const candidateFiles: string[] = [];
  const modelsPyPath = path.join(appDirectoryPath, "models.py");

  if (appScan.files.includes(modelsPyPath)) {
    candidateFiles.push(toRelativePosixPath(selectedRoot, modelsPyPath));
  }

  if (!modelsPackagePath) {
    return candidateFiles.sort();
  }

  const modelsInitPath = path.join(modelsPackagePath, "__init__.py");
  const modelsPackageScan = snapshotByPath.get(modelsPackagePath);

  if (!modelsPackageScan?.files.includes(modelsInitPath)) {
    diagnostics.push({
      code: "models_package_missing_init",
      message: `Models package ${toRelativePosixPath(selectedRoot, modelsPackagePath)} is missing __init__.py.`,
      severity: "warning",
    });

    return candidateFiles.sort();
  }

  const modelModuleFiles = directorySnapshot.flatMap((entry) =>
    isPathWithin(modelsPackagePath, entry.directoryPath)
      ? entry.files.filter((filePath) => filePath.endsWith(".py"))
      : []
  );

  for (const filePath of modelModuleFiles) {
    if (!isCandidateModelModuleFile(modelsPackagePath, filePath)) {
      continue;
    }

    candidateFiles.push(toRelativePosixPath(selectedRoot, filePath));
  }

  candidateFiles.sort();

  return candidateFiles;
}

function isPathWithin(rootPath: string, candidatePath: string): boolean {
  const relativePath = path.relative(rootPath, candidatePath);
  return relativePath === ""
    || (!relativePath.startsWith(`..${path.sep}`)
      && relativePath !== "..");
}

function toPosixPath(filePath: string): string {
  return filePath.split(path.sep).join("/");
}

function toRelativePosixPath(rootPath: string, filePath: string): string {
  return toPosixPath(path.relative(rootPath, filePath));
}

function isCandidateModelModuleFile(modelsPackagePath: string, filePath: string): boolean {
  const relativePath = toPosixPath(path.relative(modelsPackagePath, filePath));
  if (!relativePath || relativePath.startsWith("../")) {
    return false;
  }

  const segments = relativePath.split("/");
  const baseName = segments[segments.length - 1] || "";
  if (!baseName.endsWith(".py")) {
    return false;
  }

  if (
    segments.some((segment) =>
      segment === "__pycache__" ||
      segment === ".pytest_cache" ||
      segment === "test" ||
      segment === "tests",
    )
  ) {
    return false;
  }

  if (
    baseName === "conftest.py" ||
    baseName.startsWith("test_") ||
    baseName.endsWith("_test.py")
  ) {
    return false;
  }

  return true;
}
