declare const process: {
  arch: string;
  env: Record<string, string | undefined>;
  execPath: string;
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
      maxBuffer?: number;
      timeout?: number;
    },
    callback: (error: Error | null, stdout: string, stderr: string) => void,
  ): void;
}
