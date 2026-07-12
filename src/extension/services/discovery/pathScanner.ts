import { readdir } from "node:fs/promises";
import path from "node:path";

const DIRECTORY_SCAN_CONCURRENCY = 32;

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

interface DirectoryEntry {
  isDirectory(): boolean;
  isFile(): boolean;
  name: string;
}

export async function collectPythonFiles(directoryPath: string): Promise<string[]> {
  const results: string[] = [];
  await walkDirectoryTree(directoryPath, (currentDirectory, entries) => {
    for (const entry of entries) {
      const entryPath = path.join(currentDirectory, entry.name);

      if (entry.isFile() && entry.name.endsWith(".py")) {
        results.push(entryPath);
      }
    }
  });

  results.sort();

  return results;
}

export async function findFilesNamed(
  rootPath: string,
  fileName: string,
): Promise<string[]> {
  const matches: string[] = [];
  await walkDirectoryTree(rootPath, (currentDirectory, entries) => {
    for (const entry of entries) {
      const entryPath = path.join(currentDirectory, entry.name);

      if (entry.isFile() && entry.name === fileName) {
        matches.push(entryPath);
      }
    }
  });

  matches.sort();

  return matches;
}

export async function scanDirectories(rootPath: string): Promise<string[]> {
  const directories: string[] = [];
  await walkDirectoryTree(rootPath, (currentDirectory) => {
    directories.push(currentDirectory);
  });

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
  // Editor metadata, local-history snapshots, and tool caches commonly
  // contain copied Django trees. Treating those copies as live source both
  // multiplies discovery time and creates duplicate apps/models (for example
  // `.vscode` language-server stubs and `.lh` history snapshots).
  return name.startsWith(".") || IGNORED_DIRECTORY_NAMES.has(name);
}

async function walkDirectoryTree(
  rootPath: string,
  visit: (directoryPath: string, entries: DirectoryEntry[]) => void,
): Promise<void> {
  const pendingDirectories = [rootPath];
  let nextDirectoryIndex = 0;

  while (nextDirectoryIndex < pendingDirectories.length) {
    const batch = pendingDirectories.slice(
      nextDirectoryIndex,
      nextDirectoryIndex + DIRECTORY_SCAN_CONCURRENCY,
    );
    nextDirectoryIndex += batch.length;

    const scannedDirectories = await Promise.all(
      batch.map(async (directoryPath) => ({
        directoryPath,
        entries: await readdir(directoryPath, { withFileTypes: true }),
      })),
    );

    // Consume a batch in its original order so callers retain deterministic
    // traversal semantics even though the filesystem reads happen in parallel.
    for (const { directoryPath, entries } of scannedDirectories) {
      visit(directoryPath, entries);

      for (const entry of entries) {
        if (!entry.isDirectory() || isIgnoredDirectory(entry.name)) {
          continue;
        }

        pendingDirectories.push(path.join(directoryPath, entry.name));
      }
    }
  }
}
