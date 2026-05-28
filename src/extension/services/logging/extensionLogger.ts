import { readFile } from "node:fs/promises";
import path from "node:path";

import * as vscode from "vscode";

import type { Logger } from "./logger";

const outputChannelName = "Django ERD Maker";
const STARTUP_TS = new Date().toISOString().slice(11, 19);  // HH:MM:SS UTC

let outputChannel: vscode.OutputChannel | undefined;
let cachedBuildTag = `unknown@${STARTUP_TS}`;

export function registerExtensionLogger(context: vscode.ExtensionContext): void {
  // Read package.json asynchronously; first few log lines may show
  // "unknown" until the version load completes, but typically
  // resolves within ~1ms.
  const pkgPath = path.join(context.extensionUri.fsPath, "package.json");
  void readFile(pkgPath, "utf8")
    .then((raw) => {
      try {
        const parsed = JSON.parse(raw);
        if (typeof parsed.version === "string" && parsed.version.length > 0) {
          cachedBuildTag = `${parsed.version}@${STARTUP_TS}`;
        }
      } catch {
        /* leave default */
      }
    })
    .catch(() => {
      /* leave default */
    });
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
    `[${new Date().toISOString()}] [${level}] [${cachedBuildTag}] ${message}`,
  );
}

function appendRaw(message: string): void {
  for (const line of message.split(/\r?\n/)) {
    getOutputChannel().appendLine(`  ${line}`);
  }
}
