import { execFile } from "node:child_process";
import { access } from "node:fs/promises";
import path from "node:path";
import type * as vscode from "vscode";

import type { Logger } from "../logging/logger";

const OGDF_BUILD_TIMEOUT_MS = 15 * 60 * 1000;

let installPromise: Promise<string | undefined> | undefined;

export function primeOgdfBinaryInstalled(
  context: vscode.ExtensionContext,
  logger?: Logger,
): void {
  void ensureOgdfBinaryInstalled(context, logger).catch((error) => {
    const reason = error instanceof Error ? error.message : String(error);
    logger?.warn(`OGDF binary priming failed: ${reason}`);
  });
}

export async function ensureOgdfBinaryInstalled(
  context: vscode.ExtensionContext,
  logger?: Logger,
): Promise<string | undefined> {
  installPromise ??= installOgdfBinary(context, logger).finally(() => {
    installPromise = undefined;
  });

  const binaryPath = await installPromise;

  if (binaryPath) {
    process.env.DJANGO_ERD_OGDF_LAYOUT_BIN = binaryPath;
  }

  return binaryPath;
}

async function installOgdfBinary(
  context: vscode.ExtensionContext,
  logger?: Logger,
): Promise<string | undefined> {
  const envOverride = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN;

  if (envOverride && (await pathExists(envOverride))) {
    logger?.info(`OGDF binary override ready · path=${envOverride}`);
    return envOverride;
  }

  const extensionRootPath = context.extensionUri.fsPath;
  const bundledBinaryPath = path.join(
    extensionRootPath,
    "bin",
    "ogdf",
    platformKey(),
    ogdfBinaryName(),
  );

  if (await pathExists(bundledBinaryPath)) {
    logger?.info(`OGDF bundled binary ready · path=${bundledBinaryPath}`);
    return bundledBinaryPath;
  }

  const cachedInstallDirectory = path.join(
    context.globalStorageUri.fsPath,
    "native",
    "ogdf",
    platformKey(),
  );
  const cachedBinaryPath = path.join(cachedInstallDirectory, ogdfBinaryName());

  if (await pathExists(cachedBinaryPath)) {
    logger?.info(`OGDF cached binary ready · path=${cachedBinaryPath}`);
    return cachedBinaryPath;
  }

  const buildScriptPath = path.join(extensionRootPath, "scripts", "build-ogdf-binary.mjs");
  const sourceArchivePath = path.join(
    extensionRootPath,
    "vendor",
    "ogdf",
    "ogdf-foxglove-202510.tar.gz",
  );

  if (!(await pathExists(buildScriptPath)) || !(await pathExists(sourceArchivePath))) {
    logger?.warn(
      [
        "OGDF source build skipped because the bundled build assets are missing",
        `script=${buildScriptPath}`,
        `archive=${sourceArchivePath}`,
      ].join(" · "),
    );
    return undefined;
  }

  const started = Date.now();
  logger?.info(
    [
      "OGDF source build starting",
      `platform=${process.platform}`,
      `arch=${process.arch}`,
      `installDir=${cachedInstallDirectory}`,
    ].join(" · "),
  );

  try {
    const { stderr, stdout } = await execFileAsync(
      process.execPath,
      [
        buildScriptPath,
        "--extension-root",
        extensionRootPath,
        "--install-dir",
        cachedInstallDirectory,
        "--cache-root",
        path.join(context.globalStorageUri.fsPath, "native-build", "ogdf", platformKey()),
      ],
      {
        cwd: extensionRootPath,
        maxBuffer: 100 * 1024 * 1024,
        timeout: OGDF_BUILD_TIMEOUT_MS,
      },
    );

    if (stderr.trim().length > 0) {
      logger?.warn(`OGDF build stderr: ${stderr.trim()}`);
    }

    if (stdout.trim().length > 0) {
      logger?.info(`OGDF build output: ${stdout.trim()}`);
    }

    if (!(await pathExists(cachedBinaryPath))) {
      logger?.warn(`OGDF source build completed but binary is missing · path=${cachedBinaryPath}`);
      return undefined;
    }

    logger?.info(
      [
        `OGDF source build completed in ${Date.now() - started}ms`,
        `path=${cachedBinaryPath}`,
      ].join(" · "),
    );
    return cachedBinaryPath;
  } catch (error) {
    const reason = error instanceof Error ? error.message : String(error);
    logger?.warn(`OGDF source build failed: ${reason}`);
    return undefined;
  }
}

function execFileAsync(
  filePath: string,
  args: string[],
  options: {
    cwd: string;
    maxBuffer: number;
    timeout: number;
  },
): Promise<{ stderr: string; stdout: string }> {
  return new Promise((resolve, reject) => {
    execFile(filePath, args, options, (error, stdout, stderr) => {
      if (error) {
        const details = [error.message, stderr.trim()].filter(Boolean).join("\n");
        reject(new Error(details));
        return;
      }

      resolve({ stderr, stdout });
    });
  });
}

function ogdfBinaryName(): string {
  return process.platform === "win32"
    ? "django-erd-ogdf-layout.exe"
    : "django-erd-ogdf-layout";
}

function platformKey(): string {
  return `${process.platform}-${process.arch}`;
}

async function pathExists(filePath: string): Promise<boolean> {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}
