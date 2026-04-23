declare const process: {
  arch: string;
  env: Record<string, string | undefined>;
  platform: string;
};

declare module "node:child_process" {
  export interface ExecFileResult {
    stderr: string;
    stdout: string;
  }

  export function execFile(
    file: string,
    args: string[],
    options: {
      cwd?: string;
      env?: Record<string, string | undefined>;
      maxBuffer?: number;
      windowsHide?: boolean;
    },
    callback: (error: Error | null, stdout: string, stderr: string) => void,
  ): void;
}
