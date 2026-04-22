import type * as vscode from "vscode";

import { ErdPanel } from "../panels/erdPanel";
import { openDiagram } from "./openDiagram";

export async function refreshDiagram(
  context: vscode.ExtensionContext,
): Promise<void> {
  if (await ErdPanel.refreshCurrent()) {
    return;
  }

  await openDiagram(context);
}
