import * as vscode from "vscode";

import { ErdPanel } from "../panels/erdPanel";
import { discoverDjangoWorkspace } from "../services/discovery/discoverDjangoWorkspace";
import type { DjangoWorkspaceDiscoveryResult } from "../services/discovery/discoveryTypes";
import { loadLiveDiagram } from "../services/diagram/loadLiveDiagram";
import type { LiveDiagramResult } from "../services/diagram/loadLiveDiagram";
import { ensureOgdfBinaryInstalled } from "../services/layout/ensureOgdfBinaryInstalled";
import { getExtensionLogger, showExtensionLog } from "../services/logging/extensionLogger";
import type { Logger } from "../services/logging/logger";
import { timeAsync } from "../services/metrics/timeAsync";

export async function openDiagram(
  context: vscode.ExtensionContext,
): Promise<void> {
  const logger = getExtensionLogger();
  const workspacePath = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;

  if (!workspacePath) {
    throw new Error("Open a Django workspace folder before opening the ERD.");
  }

  logger.info(`Open diagram requested for workspace: ${workspacePath}`);

  const refreshLoader = () =>
    vscode.window.withProgress(
      {
        location: vscode.ProgressLocation.Notification,
        title: "Loading Django ERD",
      },
      async () => {
        await ensureOgdfBinaryInstalled(context, logger);
        const timedDiscovery = await timeAsync(() =>
          discoverDjangoWorkspace(workspacePath),
        );
        logDiscoveryResult(timedDiscovery.result, timedDiscovery.durationMs, logger);
        const liveDiagram = await loadLiveDiagram(
          context.extensionUri.fsPath,
          timedDiscovery.result,
          "hierarchical",
          timedDiscovery.durationMs,
          logger,
        );
        logLiveDiagramResult(liveDiagram, logger);
        return liveDiagram;
      },
    );

  try {
    const liveDiagram = await refreshLoader();

    ErdPanel.render(context.extensionUri, liveDiagram, refreshLoader);
  } catch (error) {
    const reason = error instanceof Error ? error.message : String(error);
    logger.error(`Django ERD failed to load: ${reason}`, error);
    showExtensionLog();
    void vscode.window.showErrorMessage(`Django ERD failed to load: ${reason}`);
    throw error;
  }
}

function logDiscoveryResult(
  discovery: DjangoWorkspaceDiscoveryResult,
  durationMs: number,
  logger: Logger,
): void {
  logger.info(
    [
      `Discovery completed in ${durationMs.toFixed(1)}ms`,
      `strategy=${discovery.strategy}`,
      `selectedRoot=${discovery.selectedRoot}`,
      `apps=${discovery.apps.length}`,
      `candidateModelFiles=${discovery.candidateModelFiles.length}`,
      `candidateModules=${discovery.candidateModules.length}`,
      `diagnostics=${discovery.diagnostics.length}`,
    ].join(" · "),
  );

  logPreview(
    logger,
    "Discovered apps",
    discovery.apps.map((app) => `${app.appLabel} (${app.appPath})`),
  );
  logPreview(
    logger,
    "Candidate modules",
    discovery.candidateModules.map(
      (module) => `${module.appLabel}:${module.filePath}`,
    ),
  );

  for (const diagnostic of discovery.diagnostics) {
    logger.warn(
      `Discovery diagnostic [${diagnostic.code}] ${diagnostic.message}`,
    );
  }
}

function logLiveDiagramResult(
  result: LiveDiagramResult,
  logger: Logger,
): void {
  const { payload } = result;
  logger.info(
    [
      `Diagram payload ready`,
      `models=${payload.analyzer.models.length}`,
      `structuralEdges=${payload.graph.structuralEdges.length}`,
      `methodAssociations=${payload.graph.methodAssociations.length}`,
      `layout=${payload.layout.mode}`,
      `analyzerDiagnostics=${payload.analyzer.diagnostics.length}`,
    ].join(" · "),
  );

  logPreview(
    logger,
    "Extracted models",
    payload.analyzer.models.map(
      (model) =>
        `${model.identity.id} bases=[${model.declaredBaseClasses.join(", ")}] fields=${model.fields.length}`,
    ),
  );

  for (const diagnostic of payload.analyzer.diagnostics) {
    logger.warn(`Analyzer diagnostic [${diagnostic.code}] ${diagnostic.message}`);
  }
}

function logPreview(logger: Logger, label: string, values: string[]): void {
  if (values.length === 0) {
    logger.info(`${label}: none`);
    return;
  }

  const previewLimit = 200;
  for (const value of values.slice(0, previewLimit)) {
    logger.info(`${label}: ${value}`);
  }

  if (values.length > previewLimit) {
    logger.info(`${label}: ... ${values.length - previewLimit} more`);
  }
}
