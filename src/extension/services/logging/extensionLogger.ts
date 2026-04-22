import * as vscode from "vscode";

import type { Logger } from "./logger";

const outputChannelName = "Django ERD Maker";

let outputChannel: vscode.OutputChannel | undefined;

export function registerExtensionLogger(context: vscode.ExtensionContext): void {
  context.subscriptions.push(getOutputChannel());
}

export function showExtensionLog(): void {
  getOutputChannel().show(true);
}

export function getExtensionLogger(): Logger {
  return {
    error(message, error) {
      appendLine("error", message);

      if (error instanceof Error) {
        if (error.stack) {
          appendRaw(error.stack);
        } else {
          appendRaw(error.message);
        }
        return;
      }

      if (error !== undefined) {
        appendRaw(String(error));
      }
    },
    info(message) {
      appendLine("info", message);
    },
    warn(message) {
      appendLine("warn", message);
    },
  };
}

function getOutputChannel(): vscode.OutputChannel {
  outputChannel ??= vscode.window.createOutputChannel(outputChannelName);
  return outputChannel;
}

function appendLine(level: "error" | "info" | "warn", message: string): void {
  getOutputChannel().appendLine(
    `[${new Date().toISOString()}] [${level}] ${message}`,
  );
}

function appendRaw(message: string): void {
  for (const line of message.split(/\r?\n/)) {
    getOutputChannel().appendLine(`  ${line}`);
  }
}
