import { access } from "node:fs/promises";
import path from "node:path";

import type {
  DiscoveredDjangoApp,
  DiscoveryDiagnostic,
} from "./discoveryTypes";
import { collectPythonFiles, scanDirectories, scanImmediateChildren } from "./pathScanner";

export interface DiscoverAppsResult {
  apps: DiscoveredDjangoApp[];
  diagnostics: DiscoveryDiagnostic[];
}

export async function discoverApps(
  selectedRoot: string,
): Promise<DiscoverAppsResult> {
  const diagnostics: DiscoveryDiagnostic[] = [];
  const apps: DiscoveredDjangoApp[] = [];
  const directories = await scanDirectories(selectedRoot);

  for (const directoryPath of directories) {
    const baseName = path.basename(directoryPath);

    if (baseName === "models") {
      continue;
    }

    const app = await maybeDiscoverApp(directoryPath, selectedRoot, diagnostics);

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

async function maybeDiscoverApp(
  directoryPath: string,
  selectedRoot: string,
  diagnostics: DiscoveryDiagnostic[],
): Promise<DiscoveredDjangoApp | undefined> {
  const scanResult = await scanImmediateChildren(directoryPath);
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

  const candidateModelFiles = await collectCandidateModelFiles(
    directoryPath,
    modelsPackagePath,
    selectedRoot,
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

async function collectCandidateModelFiles(
  appDirectoryPath: string,
  modelsPackagePath: string | undefined,
  selectedRoot: string,
  diagnostics: DiscoveryDiagnostic[],
): Promise<string[]> {
  const candidateFiles: string[] = [];
  const modelsPyPath = path.join(appDirectoryPath, "models.py");

  if (await pathExists(modelsPyPath)) {
    candidateFiles.push(toRelativePosixPath(selectedRoot, modelsPyPath));
  }

  if (!modelsPackagePath) {
    return candidateFiles.sort();
  }

  const modelsInitPath = path.join(modelsPackagePath, "__init__.py");

  if (!(await pathExists(modelsInitPath))) {
    diagnostics.push({
      code: "models_package_missing_init",
      message: `Models package ${toRelativePosixPath(selectedRoot, modelsPackagePath)} is missing __init__.py.`,
      severity: "warning",
    });

    return candidateFiles.sort();
  }

  const modelModuleFiles = await collectPythonFiles(modelsPackagePath);

  for (const filePath of modelModuleFiles) {
    if (!isCandidateModelModuleFile(modelsPackagePath, filePath)) {
      continue;
    }

    candidateFiles.push(toRelativePosixPath(selectedRoot, filePath));
  }

  candidateFiles.sort();

  return candidateFiles;
}

async function pathExists(filePath: string): Promise<boolean> {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
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
