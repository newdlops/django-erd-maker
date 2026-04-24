import * as vscode from "vscode";

import { DEFAULT_LAYOUT_MODE, type LayoutMode } from "../../shared/graph/layoutContract";
import type { RefreshViewStateSnapshot } from "../../shared/protocol/webviewContract";
import { ErdPanel } from "../panels/erdPanel";
import { discoverDjangoWorkspace } from "../services/discovery/discoverDjangoWorkspace";
import type { DjangoWorkspaceDiscoveryResult } from "../services/discovery/discoveryTypes";
import { loadLiveDiagram } from "../services/diagram/loadLiveDiagram";
import { relayoutLiveDiagram } from "../services/diagram/loadLiveDiagram";
import type { LiveDiagramResult } from "../services/diagram/loadLiveDiagram";
import { restoreRefreshViewState } from "../services/diagram/restoreRefreshViewState";
import { ensureOgdfBinaryInstalled } from "../services/layout/ensureOgdfBinaryInstalled";
import { getExtensionLogger, showExtensionLog } from "../services/logging/extensionLogger";
import type { Logger } from "../services/logging/logger";
import { timeAsync } from "../services/metrics/timeAsync";

export async function openDiagram(
  context: vscode.ExtensionContext,
): Promise<void> {
  const logger = getExtensionLogger();
  const workspacePath = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  let cachedDiagram: LiveDiagramResult | undefined;
  let latestRefreshRunId = 0;

  if (!workspacePath) {
    throw new Error("Open a Django workspace folder before opening the ERD.");
  }

  logger.info(`Open diagram requested for workspace: ${workspacePath}`);

  const refreshLoader = ({
    layoutMode = DEFAULT_LAYOUT_MODE,
    requestId,
    refreshKind = "full",
    viewState,
  }: {
    layoutMode?: LayoutMode;
    requestId?: number;
    refreshKind?: "full" | "layout";
    viewState?: RefreshViewStateSnapshot;
  } = {}) => {
    const refreshRunId = requestId ?? ++latestRefreshRunId;
    latestRefreshRunId = Math.max(latestRefreshRunId, refreshRunId);

    return vscode.window.withProgress(
      {
        location: vscode.ProgressLocation.Notification,
        title: "Loading Django ERD",
      },
      async () => {
        await ensureOgdfBinaryInstalled(context, logger);
        const previousDiagram = cachedDiagram;
        let liveDiagram: LiveDiagramResult;

        if (refreshKind === "layout" && previousDiagram) {
          logger.info(
            `Layout refresh reusing cached analyzer payload · requestId=${refreshRunId} · layout=${layoutMode}`,
          );
          liveDiagram = await relayoutLiveDiagram(
            context.extensionUri.fsPath,
            previousDiagram,
            layoutMode,
            logger,
            refreshRunId,
          );
        } else {
          const timedDiscovery = await timeAsync(() =>
            discoverDjangoWorkspace(workspacePath),
          );
          logDiscoveryResult(timedDiscovery.result, timedDiscovery.durationMs, logger);
          liveDiagram = await loadLiveDiagram(
            context.extensionUri.fsPath,
            timedDiscovery.result,
            layoutMode,
            timedDiscovery.durationMs,
            logger,
            refreshRunId,
          );
        }

        if (viewState) {
          liveDiagram = restoreRefreshViewState(
            liveDiagram,
            previousDiagram,
            viewState,
            refreshKind,
          );
        }

        if (refreshRunId !== latestRefreshRunId) {
          logger.info(
            [
              "Stale diagram refresh result skipped",
              `requestId=${refreshRunId}`,
              `latestRequestId=${latestRefreshRunId}`,
              `requestedLayout=${liveDiagram.payload.layoutExecution?.requestedMode ?? liveDiagram.payload.view.layoutMode}`,
              `appliedLayout=${liveDiagram.payload.layoutExecution?.appliedMode ?? liveDiagram.payload.layout.mode}`,
              `layoutStatus=${liveDiagram.payload.layoutExecution?.status ?? "applied"}`,
            ].join(" · "),
          );
          return cachedDiagram ?? liveDiagram;
        }

        logLiveDiagramResult(liveDiagram, logger, refreshRunId);
        cachedDiagram = liveDiagram;
        return liveDiagram;
      },
    );
  };

  try {
    const liveDiagram = await refreshLoader({
      layoutMode: DEFAULT_LAYOUT_MODE,
      refreshKind: "full",
    });

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
  requestId?: number,
): void {
  const { payload } = result;
  const execution = payload.layoutExecution;
  const metadata = execution?.engineMetadata ?? payload.layout.engineMetadata;
  logger.info(
    [
      `Diagram payload ready`,
      ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
      `models=${payload.analyzer.models.length}`,
      `structuralEdges=${payload.graph.structuralEdges.length}`,
      `methodAssociations=${payload.graph.methodAssociations.length}`,
      `requestedLayout=${execution?.requestedMode ?? payload.view.layoutMode}`,
      `appliedLayout=${execution?.appliedMode ?? payload.layout.mode}`,
      `layoutStatus=${execution?.status ?? "applied"}`,
      `layoutEngine=${execution?.engine ?? "analyzer"}`,
      ...(metadata?.actualMode ? [`actualLayout=${metadata.actualMode}`] : []),
      ...(metadata?.strategy ? [`layoutStrategy=${metadata.strategy}`] : []),
      ...(metadata?.actualAlgorithm ? [`actualAlgorithm=${metadata.actualAlgorithm}`] : []),
      ...(metadata?.strategyReason ? [`strategyReason=${metadata.strategyReason}`] : []),
      ...(metadata?.nodeOverlaps !== undefined ? [`nodeOverlaps=${metadata.nodeOverlaps}`] : []),
      ...(metadata?.edgeNodeIntersections !== undefined ? [`edgeNodeIntersections=${metadata.edgeNodeIntersections}`] : []),
      ...(metadata?.overlappingEdges !== undefined ? [`overlappingEdges=${metadata.overlappingEdges}`] : []),
      ...(execution?.reason ? [`layoutReason=${execution.reason}`] : []),
      `disabledLayouts=${Object.keys(payload.layoutFailures ?? {}).length}`,
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
