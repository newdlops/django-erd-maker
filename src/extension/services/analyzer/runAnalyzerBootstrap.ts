import { execFile } from "node:child_process";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import {
  getOgdfLayoutDefinition,
  normalizeLayoutMode,
  resolveAnalyzerLayoutMode,
  type LayoutMode,
} from "../../../shared/graph/layoutContract";
import { decodeDiagramBootstrapPayload } from "../../../shared/protocol/decodeDiagramBootstrap";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";
import type { DjangoWorkspaceDiscoveryResult } from "../discovery/discoveryTypes";
import type { Logger } from "../logging/logger";
import { resolveAnalyzerBinaryPath } from "./resolveAnalyzerBinaryPath";

export interface AnalyzerBootstrapResult {
  durationMs: number;
  payload: DiagramBootstrapPayload;
}

export async function runAnalyzerBootstrap(
  extensionRootPath: string,
  discovery: DjangoWorkspaceDiscoveryResult,
  layoutMode: LayoutMode,
  logger?: Logger,
): Promise<AnalyzerBootstrapResult> {
  const started = Date.now();
  const analyzerBinaryPath = await resolveAnalyzerBinaryPath(extensionRootPath);
  const requestedLayoutMode = normalizeLayoutMode(layoutMode);
  const requestedLayout = getOgdfLayoutDefinition(requestedLayoutMode);
  const analyzerLayoutMode = resolveAnalyzerLayoutMode(layoutMode);
  const modules = createRequestModules(discovery);

  if (modules.length === 0) {
    throw new Error("Analyzer bootstrap requires at least one discovered Python module.");
  }

  const requestDirectory = await mkdtemp(path.join(os.tmpdir(), "django-erd-analyzer-"));
  const requestPath = path.join(requestDirectory, "request.json");
  logger?.info(
    [
      `Analyzer bootstrap starting`,
      `binary=${analyzerBinaryPath}`,
      `requestedLayout=${requestedLayoutMode}`,
      `requestedLabel=${requestedLayout.label}`,
      `analyzerLayout=${analyzerLayoutMode}`,
      `workspaceRoot=${discovery.selectedRoot}`,
      `modules=${modules.length}`,
    ].join(" · "),
  );
  logModulePreview(modules, logger);

  await writeFile(
    requestPath,
    JSON.stringify({
      modules,
      workspaceRoot: discovery.selectedRoot,
    }),
    "utf8",
  );

  try {
    const { stderr, stdout } = await execFileAsync(
      analyzerBinaryPath,
      [
        "bootstrap",
        "--mode",
        analyzerLayoutMode,
        "--request-file",
        requestPath,
      ],
      {
        cwd: extensionRootPath,
        maxBuffer: 100 * 1024 * 1024,
      },
    );

    if (stderr.trim().length > 0) {
      logger?.error(`Analyzer stderr: ${stderr.trim()}`);
      throw new Error(stderr.trim());
    }

    const payload = decodeDiagramBootstrapPayload(JSON.parse(stdout));
    logger?.info(
      [
        `Analyzer bootstrap completed in ${Date.now() - started}ms`,
        `models=${payload.analyzer.models.length}`,
        `diagnostics=${payload.analyzer.diagnostics.length}`,
        `nodes=${payload.graph.nodes.length}`,
        `structuralEdges=${payload.graph.structuralEdges.length}`,
      ].join(" · "),
    );

    return {
      durationMs: Date.now() - started,
      payload,
    };
  } catch (error) {
    const reason = error instanceof Error ? error.message : String(error);
    logger?.error(`Analyzer bootstrap failed: ${reason}`, error);
    throw new Error(`Failed to run analyzer bootstrap: ${reason}`);
  } finally {
    await rm(requestDirectory, { force: true, recursive: true });
  }
}

function execFileAsync(
  filePath: string,
  args: string[],
  options: {
    cwd: string;
    maxBuffer: number;
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

function logModulePreview(
  modules: Array<{ appLabel: string; filePath: string }>,
  logger?: Logger,
): void {
  if (!logger) {
    return;
  }

  const previewLimit = 200;
  for (const module of modules.slice(0, previewLimit)) {
    logger.info(`Analyzer module: ${module.appLabel}:${module.filePath}`);
  }

  if (modules.length > previewLimit) {
    logger.info(`Analyzer module: ... ${modules.length - previewLimit} more`);
  }
}

function createRequestModules(discovery: DjangoWorkspaceDiscoveryResult): Array<{
  appLabel: string;
  filePath: string;
}> {
  return discovery.candidateModules.map((module) => ({
    appLabel: module.appLabel,
    filePath: path.join(discovery.selectedRoot, fromPosixPath(module.filePath)),
  }));
}

function fromPosixPath(filePath: string): string {
  return filePath.split("/").join(path.sep);
}
