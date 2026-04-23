declare module "vscode" {
  export interface Disposable {
    dispose(): unknown;
  }

  export interface OutputChannel extends Disposable {
    append(value: string): void;
    appendLine(value: string): void;
    clear(): void;
    show(preserveFocus?: boolean): void;
  }

  export class Uri {
    private constructor();
    readonly fsPath: string;
    toString(): string;
  }

  export enum ViewColumn {
    One = 1,
  }

  export interface ExtensionContext {
    extensionUri: Uri;
    globalStorageUri: Uri;
    subscriptions: Disposable[];
  }

  export interface WorkspaceFolder {
    index: number;
    name: string;
    uri: Uri;
  }

  export interface Webview {
    html: string;
    onDidReceiveMessage(listener: (message: unknown) => void): Disposable;
    postMessage(message: unknown): Thenable<boolean>;
  }

  export interface WebviewPanel extends Disposable {
    readonly webview: Webview;
    onDidDispose(listener: () => void): Disposable;
    reveal(viewColumn?: ViewColumn): void;
  }

  export namespace commands {
    function registerCommand(
      command: string,
      callback: (...args: unknown[]) => unknown,
    ): Disposable;
    function executeCommand<T = unknown>(command: string, ...rest: unknown[]): Thenable<T>;
  }

  export enum ProgressLocation {
    Notification = 15,
  }

  export namespace window {
    function createOutputChannel(name: string): OutputChannel;
    function createWebviewPanel(
      viewType: string,
      title: string,
      showOptions: ViewColumn,
      options: {
        enableScripts?: boolean;
        retainContextWhenHidden?: boolean;
      },
    ): WebviewPanel;
    function showErrorMessage(message: string): Thenable<string | undefined>;
    function withProgress<T>(
      options: { location: ProgressLocation; title: string },
      task: () => Thenable<T> | Promise<T>,
    ): Thenable<T>;
  }

  export namespace workspace {
    const workspaceFolders: readonly WorkspaceFolder[] | undefined;
  }
}
