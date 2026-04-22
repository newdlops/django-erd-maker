declare module "node:fs/promises" {
  export function access(path: string): Promise<void>;
  export function mkdtemp(prefix: string): Promise<string>;
  export function readdir(
    path: string,
    options: { withFileTypes: true },
  ): Promise<Dirent[]>;
  export function rm(
    path: string,
    options?: { force?: boolean; recursive?: boolean },
  ): Promise<void>;
  export function writeFile(
    path: string,
    data: string,
    encoding: "utf8",
  ): Promise<void>;

  export interface Dirent {
    isDirectory(): boolean;
    isFile(): boolean;
    name: string;
  }
}

declare module "node:path" {
  interface PathModule {
    basename(path: string): string;
    dirname(path: string): string;
    join(...paths: string[]): string;
    relative(from: string, to: string): string;
    sep: string;
  }

  const path: PathModule;
  export default path;
}

declare module "node:os" {
  export function tmpdir(): string;
}
