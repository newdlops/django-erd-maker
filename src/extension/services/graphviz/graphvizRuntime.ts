import * as vscode from "vscode";

import { execFile } from "node:child_process";
import { access } from "node:fs/promises";
import type { Logger } from "../logging/logger";
import {
  createBundledGraphvizEnvironment,
  expectedBundledGraphvizRuntimeRoot,
  getBundledGraphvizRuntimeCandidates,
  type BundledGraphvizRuntime,
} from "./bundledGraphvizRuntime";

export interface GraphvizRuntimeResolution {
  dotPath: string;
  kind: "bundled" | "path";
  runtime?: BundledGraphvizRuntime;
}

let cachedRuntime: GraphvizRuntimeResolution | undefined;
let resolvePromise: Promise<GraphvizRuntimeResolution | undefined> | undefined;

export function primeGraphvizRuntime(
  context: vscode.ExtensionContext,
  logger?: Logger,
): void {
  void ensureGraphvizRuntime(context, logger);
}

export async function ensureGraphvizRuntime(
  context: vscode.ExtensionContext,
  logger?: Logger,
): Promise<GraphvizRuntimeResolution | undefined> {
  if (cachedRuntime && await canExecuteRuntime(cachedRuntime)) {
    return cachedRuntime;
  }

  if (resolvePromise) {
    return resolvePromise;
  }

  resolvePromise = resolveGraphvizRuntime(context, logger).finally(() => {
    resolvePromise = undefined;
  });

  const runtime = await resolvePromise;
  if (runtime) {
    cachedRuntime = runtime;
  }
  return runtime;
}

export function createGraphvizEnvironment(
  baseEnvironment: Record<string, string | undefined>,
  runtime: GraphvizRuntimeResolution | undefined,
): Record<string, string | undefined> {
  const environment = { ...baseEnvironment };
  if (!runtime) {
    return environment;
  }

  environment.DJANGO_ERD_GRAPHVIZ_DOT = runtime.dotPath;

  if (runtime.kind === "bundled" && runtime.runtime) {
    return createBundledGraphvizEnvironment(environment, runtime.runtime);
  }

  return environment;
}

async function resolveGraphvizRuntime(
  context: vscode.ExtensionContext,
  logger?: Logger,
): Promise<GraphvizRuntimeResolution | undefined> {
  const bundledRuntime = await resolveBundledGraphvizRuntime(
    context.extensionUri.fsPath,
  );
  if (bundledRuntime) {
    logger?.info(`Using bundled Graphviz runtime: ${bundledRuntime.dotPath}`);
    return {
      dotPath: bundledRuntime.dotPath,
      kind: "bundled",
      runtime: bundledRuntime,
    };
  }

  if (await canExecuteCommand("dot", { ...process.env })) {
    logger?.warn("Bundled Graphviz runtime was not found; falling back to dot from PATH.");
    return {
      dotPath: "dot",
      kind: "path",
    };
  }

  logger?.warn(
    [
      "Graphviz runtime is unavailable.",
      `Expected bundled runtime under ${expectedBundledGraphvizRuntimeRoot(context.extensionUri.fsPath)}.`,
      "Falling back to the legacy analyzer layout path.",
    ].join(" "),
  );
  return undefined;
}

async function resolveBundledGraphvizRuntime(
  extensionPath: string,
): Promise<BundledGraphvizRuntime | undefined> {
  for (const candidate of getBundledGraphvizRuntimeCandidates(extensionPath)) {
    if (!await isExecutableFile(candidate.dotPath)) {
      continue;
    }

    const environment = createBundledGraphvizEnvironment(process.env, candidate);
    if (await canExecuteCommand(candidate.dotPath, environment)) {
      return candidate;
    }
  }

  return undefined;
}

async function canExecuteRuntime(
  runtime: GraphvizRuntimeResolution,
): Promise<boolean> {
  return canExecuteCommand(
    runtime.dotPath,
    createGraphvizEnvironment(process.env, runtime),
  );
}

async function isExecutableFile(filePath: string): Promise<boolean> {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}

function canExecuteCommand(
  filePath: string,
  environment: Record<string, string | undefined>,
): Promise<boolean> {
  return new Promise((resolve) => {
    execFile(
      filePath,
      ["-V"],
      {
        env: environment,
        maxBuffer: 1024 * 1024,
        windowsHide: true,
      },
      (error) => {
        resolve(!error);
      },
    );
  });
}
