import * as vscode from "vscode";

import { registerCommands } from "./activation/registerCommands";
import { primeOgdfBinaryInstalled } from "./services/layout/ensureOgdfBinaryInstalled";
import { registerExtensionLogger } from "./services/logging/extensionLogger";
import { getExtensionLogger } from "./services/logging/extensionLogger";

export function activate(context: vscode.ExtensionContext): void {
  registerExtensionLogger(context);
  primeOgdfBinaryInstalled(context, getExtensionLogger());
  registerCommands(context);
}

export function deactivate(): void {
  // No-op for the Phase 0 scaffold.
}
