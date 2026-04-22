import * as vscode from "vscode";

import { openDiagram } from "../commands/openDiagram";
import { refreshDiagram } from "../commands/refreshDiagram";
import { ErdPanel } from "../panels/erdPanel";
import { showExtensionLog } from "../services/logging/extensionLogger";
import type { DiagramTestAction } from "../../shared/protocol/webviewTestContract";

export function registerCommands(context: vscode.ExtensionContext): void {
  const openDiagramCommand = vscode.commands.registerCommand(
    "djangoErd.openDiagram",
    () => openDiagram(context),
  );
  const refreshDiagramCommand = vscode.commands.registerCommand(
    "djangoErd.refreshDiagram",
    () => refreshDiagram(context),
  );
  const showLogCommand = vscode.commands.registerCommand(
    "djangoErd.showLog",
    () => showExtensionLog(),
  );
  const panelStateCommand = vscode.commands.registerCommand(
    "djangoErd.__test.getPanelState",
    () => ErdPanel.getCurrentStateForTest(),
  );
  const panelActionCommand = vscode.commands.registerCommand(
    "djangoErd.__test.runWebviewAction",
    (action: unknown) => ErdPanel.runWebviewActionForTest(action as DiagramTestAction),
  );

  context.subscriptions.push(openDiagramCommand);
  context.subscriptions.push(refreshDiagramCommand);
  context.subscriptions.push(showLogCommand);
  context.subscriptions.push(panelStateCommand);
  context.subscriptions.push(panelActionCommand);
}
