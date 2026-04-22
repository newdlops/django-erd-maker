import * as vscode from "vscode";

import { registerCommands } from "./activation/registerCommands";
import { registerExtensionLogger } from "./services/logging/extensionLogger";

export function activate(context: vscode.ExtensionContext): void {
  registerExtensionLogger(context);
  registerCommands(context);
}

export function deactivate(): void {
  // No-op for the Phase 0 scaffold.
}
