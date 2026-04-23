import * as vscode from "vscode";

import { registerCommands } from "./activation/registerCommands";
import { primeGraphvizRuntime } from "./services/graphviz/graphvizRuntime";
import { registerExtensionLogger } from "./services/logging/extensionLogger";
import { getExtensionLogger } from "./services/logging/extensionLogger";

export function activate(context: vscode.ExtensionContext): void {
  registerExtensionLogger(context);
  registerCommands(context);
  primeGraphvizRuntime(context, getExtensionLogger());
}

export function deactivate(): void {
  // Bundled Graphviz ships inside the extension package, so extension removal
  // removes the runtime at the same time.
}
