declare const process: {
  arch: string;
  env: Record<string, string | undefined>;
  execPath: string;
  kill(pid: number, signal?: string): boolean;
  platform: string;
};

declare module "node:child_process" {
  export interface ExecFileResult {
    stderr: string;
    stdout: string;
  }

  export interface ChildProcess {
    pid?: number;
    kill(signal?: string): boolean;
  }

  export function execFile(
    file: string,
    args: string[],
    options: {
      cwd?: string;
      detached?: boolean;
      env?: Record<string, string | undefined>;
      killSignal?: string;
      maxBuffer?: number;
      timeout?: number;
    },
    callback: (error: Error | null, stdout: string, stderr: string) => void,
  ): ChildProcess;
}
