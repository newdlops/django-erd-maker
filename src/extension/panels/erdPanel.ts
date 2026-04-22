import * as vscode from "vscode";

import type {
  DiagramInteractionSettingsSnapshot,
  WebviewToExtensionMessage,
} from "../../shared/protocol/webviewContract";
import type { LiveDiagramResult } from "../services/diagram/loadLiveDiagram";
import type { DiagramBootstrapPayload } from "../../shared/protocol/webviewContract";
import { mergePipelineTimings } from "../../shared/protocol/mergePipelineTimings";
import type { DjangoWorkspaceDiscoveryResult } from "../../shared/protocol/discoveryContract";
import type {
  DiagramTestAction,
  DiagramTestErrorMessage,
  DiagramTestSnapshot,
  DiagramTestSnapshotMessage,
  RunDiagramTestActionMessage,
} from "../../shared/protocol/webviewTestContract";
import { renderDiagramDocument } from "../../webview/app/renderDiagramDocument";

type RefreshLoader = () => Promise<LiveDiagramResult>;
type PanelMessage =
  | WebviewToExtensionMessage
  | DiagramTestSnapshotMessage
  | DiagramTestErrorMessage;

export interface ErdPanelTestState {
  discovery?: DjangoWorkspaceDiscoveryResult;
  html: string;
  payload: DiagramBootstrapPayload;
  webviewSnapshot?: DiagramTestSnapshot;
}

export class ErdPanel {
  private static currentPanel: ErdPanel | undefined;
  private currentState: ErdPanelTestState | undefined;
  private latestWebviewSnapshot: DiagramTestSnapshot | undefined;
  private readonly pendingTestRequests = new Map<
    string,
    {
      reject: (error: Error) => void;
      resolve: (snapshot: DiagramTestSnapshot) => void;
      timeout: ReturnType<typeof setTimeout>;
    }
  >();
  private readonly readyWaiters = new Map<
    string,
    {
      reject: (error: Error) => void;
      resolve: () => void;
      timeout: ReturnType<typeof setTimeout>;
    }
  >();
  private persistedSetupSettings: DiagramInteractionSettingsSnapshot | undefined;
  private refreshLoader: RefreshLoader | undefined;
  private webviewReady = false;

  private constructor(private readonly panel: vscode.WebviewPanel) {
    this.panel.onDidDispose(() => {
      this.rejectAllTestRequests("Webview panel was disposed.");
      this.rejectAllReadyWaiters("Webview panel was disposed.");
      ErdPanel.currentPanel = undefined;
    });
    this.panel.webview.onDidReceiveMessage((message) => {
      void this.handleWebviewMessage(message as PanelMessage);
    });
  }

  static render(
    _extensionUri: vscode.Uri,
    liveDiagram: LiveDiagramResult,
    refreshLoader?: RefreshLoader,
  ): void {
    const existingPanel = ErdPanel.currentPanel;

    if (existingPanel) {
      existingPanel.panel.reveal(vscode.ViewColumn.One);
      existingPanel.update(liveDiagram, refreshLoader);
      return;
    }

    const panel = vscode.window.createWebviewPanel(
      "djangoErd.diagram",
      "Django ERD",
      vscode.ViewColumn.One,
      {
        enableScripts: true,
        retainContextWhenHidden: true,
      },
    );

    const erdPanel = new ErdPanel(panel);
    erdPanel.update(liveDiagram, refreshLoader);
    ErdPanel.currentPanel = erdPanel;
  }

  static getCurrentStateForTest(): ErdPanelTestState | undefined {
    const currentPanel = ErdPanel.currentPanel;
    if (!currentPanel?.currentState) {
      return undefined;
    }

    return {
      ...currentPanel.currentState,
      webviewSnapshot: currentPanel.latestWebviewSnapshot,
    };
  }

  static async refreshCurrent(): Promise<boolean> {
    const currentPanel = ErdPanel.currentPanel;

    if (!currentPanel) {
      return false;
    }

    await currentPanel.refresh();
    return true;
  }

  static async runWebviewActionForTest(
    action: DiagramTestAction,
  ): Promise<DiagramTestSnapshot | undefined> {
    return ErdPanel.currentPanel?.runWebviewAction(action);
  }

  private async handleWebviewMessage(
    message: PanelMessage,
  ): Promise<void> {
    switch (message.type) {
      case "diagram.ready":
        this.webviewReady = true;
        this.resolveAllReadyWaiters();
        return;
      case "diagram.updateSetupSettings":
        this.persistedSetupSettings = {
          ...message.settings,
        };
        return;
      case "diagram.requestRefresh":
        if (message.settings) {
          this.persistedSetupSettings = {
            ...message.settings,
          };
        }
        await this.refresh();
        return;
      case "diagram.test.error":
        this.rejectTestRequest(message);
        return;
      case "diagram.test.snapshot":
        this.resolveTestRequest(message);
        return;
    }
  }

  private async refresh(): Promise<void> {
    if (!this.refreshLoader) {
      return;
    }

    const liveDiagram = await this.refreshLoader();
    this.update(liveDiagram, this.refreshLoader);
  }

  private update(
    liveDiagram: LiveDiagramResult,
    refreshLoader?: RefreshLoader,
  ): void {
    this.refreshLoader = refreshLoader;
    this.latestWebviewSnapshot = undefined;
    this.webviewReady = false;
    this.rejectAllReadyWaiters("Webview was reloaded before becoming ready.");
    const renderStarted = Date.now();
    renderDiagramDocument(
      liveDiagram.payload,
      liveDiagram.discovery,
      this.persistedSetupSettings,
    );
    liveDiagram.payload.timings = mergePipelineTimings(liveDiagram.payload.timings, {
      renderDocumentMs: Date.now() - renderStarted,
    });
    const html = renderDiagramDocument(
      liveDiagram.payload,
      liveDiagram.discovery,
      this.persistedSetupSettings,
    );
    this.currentState = {
      discovery: liveDiagram.discovery,
      html,
      payload: liveDiagram.payload,
    };
    this.panel.webview.html = this.currentState.html;
  }

  private async runWebviewAction(
    action: DiagramTestAction,
  ): Promise<DiagramTestSnapshot> {
    await this.waitForWebviewReady();
    const requestId = `test-${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;

    return new Promise<DiagramTestSnapshot>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingTestRequests.delete(requestId);
        reject(new Error(`Timed out waiting for webview test action ${action.type}.`));
      }, 5000);

      this.pendingTestRequests.set(requestId, {
        reject,
        resolve,
        timeout,
      });

      void this.panel.webview.postMessage({
        action,
        requestId,
        type: "diagram.test.run",
      } satisfies RunDiagramTestActionMessage).then((accepted: boolean) => {
        if (accepted) {
          return;
        }

        const pending = this.pendingTestRequests.get(requestId);
        if (!pending) {
          return;
        }

        clearTimeout(pending.timeout);
        this.pendingTestRequests.delete(requestId);
        reject(new Error("Webview did not accept the test message."));
      });
    });
  }

  private waitForWebviewReady(): Promise<void> {
    if (this.webviewReady) {
      return Promise.resolve();
    }

    const requestId = `ready-${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;
    return new Promise<void>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.readyWaiters.delete(requestId);
        reject(new Error("Timed out waiting for webview readiness."));
      }, 5000);

      this.readyWaiters.set(requestId, {
        reject,
        resolve,
        timeout,
      });
    });
  }

  private resolveAllReadyWaiters(): void {
    for (const [requestId, pending] of this.readyWaiters.entries()) {
      clearTimeout(pending.timeout);
      this.readyWaiters.delete(requestId);
      pending.resolve();
    }
  }

  private rejectAllReadyWaiters(reason: string): void {
    for (const [requestId, pending] of this.readyWaiters.entries()) {
      clearTimeout(pending.timeout);
      this.readyWaiters.delete(requestId);
      pending.reject(new Error(reason));
    }
  }

  private resolveTestRequest(message: DiagramTestSnapshotMessage): void {
    this.latestWebviewSnapshot = message.snapshot;
    if (this.currentState) {
      this.currentState.webviewSnapshot = message.snapshot;
    }

    const pending = this.pendingTestRequests.get(message.requestId);
    if (!pending) {
      return;
    }

    clearTimeout(pending.timeout);
    this.pendingTestRequests.delete(message.requestId);
    pending.resolve(message.snapshot);
  }

  private rejectTestRequest(message: DiagramTestErrorMessage): void {
    const pending = this.pendingTestRequests.get(message.requestId);
    if (!pending) {
      return;
    }

    clearTimeout(pending.timeout);
    this.pendingTestRequests.delete(message.requestId);
    pending.reject(new Error(message.message));
  }

  private rejectAllTestRequests(reason: string): void {
    for (const [requestId, pending] of this.pendingTestRequests.entries()) {
      clearTimeout(pending.timeout);
      this.pendingTestRequests.delete(requestId);
      pending.reject(new Error(reason));
    }
  }
}
