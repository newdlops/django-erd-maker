import { readdir } from "node:fs/promises";
import path from "node:path";

const IGNORED_DIRECTORY_NAMES = new Set([
  ".git",
  ".hg",
  ".mypy_cache",
  ".nox",
  ".pytest_cache",
  ".ruff_cache",
  ".tox",
  ".venv",
  "__pycache__",
  "dist",
  "node_modules",
  "site-packages",
  "target",
  "venv",
]);

export interface ScanDirectoryResult {
  directories: string[];
  files: string[];
}

export async function collectPythonFiles(directoryPath: string): Promise<string[]> {
  const results: string[] = [];
  const pendingDirectories = [directoryPath];

  while (pendingDirectories.length > 0) {
    const currentDirectory = pendingDirectories.pop();

    if (!currentDirectory) {
      continue;
    }

    const entries = await readdir(currentDirectory, { withFileTypes: true });

    for (const entry of entries) {
      const entryPath = path.join(currentDirectory, entry.name);

      if (entry.isDirectory()) {
        if (!isIgnoredDirectory(entry.name)) {
          pendingDirectories.push(entryPath);
        }

        continue;
      }

      if (entry.isFile() && entry.name.endsWith(".py")) {
        results.push(entryPath);
      }
    }
  }

  results.sort();

  return results;
}

export async function findFilesNamed(
  rootPath: string,
  fileName: string,
): Promise<string[]> {
  const matches: string[] = [];
  const pendingDirectories = [rootPath];

  while (pendingDirectories.length > 0) {
    const currentDirectory = pendingDirectories.pop();

    if (!currentDirectory) {
      continue;
    }

    const entries = await readdir(currentDirectory, { withFileTypes: true });

    for (const entry of entries) {
      const entryPath = path.join(currentDirectory, entry.name);

      if (entry.isDirectory()) {
        if (!isIgnoredDirectory(entry.name)) {
          pendingDirectories.push(entryPath);
        }

        continue;
      }

      if (entry.isFile() && entry.name === fileName) {
        matches.push(entryPath);
      }
    }
  }

  matches.sort();

  return matches;
}

export async function scanDirectories(rootPath: string): Promise<string[]> {
  const directories: string[] = [];
  const pendingDirectories = [rootPath];

  while (pendingDirectories.length > 0) {
    const currentDirectory = pendingDirectories.pop();

    if (!currentDirectory) {
      continue;
    }

    directories.push(currentDirectory);

    const entries = await readdir(currentDirectory, { withFileTypes: true });

    for (const entry of entries) {
      if (!entry.isDirectory() || isIgnoredDirectory(entry.name)) {
        continue;
      }

      pendingDirectories.push(path.join(currentDirectory, entry.name));
    }
  }

  directories.sort();

  return directories;
}

export async function scanImmediateChildren(
  directoryPath: string,
): Promise<ScanDirectoryResult> {
  const directories: string[] = [];
  const files: string[] = [];
  const entries = await readdir(directoryPath, { withFileTypes: true });

  for (const entry of entries) {
    const entryPath = path.join(directoryPath, entry.name);

    if (entry.isDirectory()) {
      directories.push(entryPath);
    }

    if (entry.isFile()) {
      files.push(entryPath);
    }
  }

  directories.sort();
  files.sort();

  return {
    directories,
    files,
  };
}

function isIgnoredDirectory(name: string): boolean {
  return IGNORED_DIRECTORY_NAMES.has(name);
}
