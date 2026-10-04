import fs from "node:fs/promises";
import { createReadStream } from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

export const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
export const target = "darwin-arm64";
export const minimumMacOS = "26.0";
export const upstreamArchive = "ogdf-foxglove-202510.tar.gz";
export const binaryPaths = [
  `bin/analyzer/${target}/django-erd-maker-analyzer`,
  `bin/ogdf/${target}/django-erd-ogdf-layout`,
];
export const sourceArchives = ["project-source.tar.gz", "rust-dependencies.tar.gz", upstreamArchive];
export const requiredDocs = [
  "README.md", "README.ko.md", "CHANGELOG.md", "LICENSE", "NOTICE.md",
  "THIRD_PARTY_NOTICES.md", "PRIVACY.md", "SECURITY.md", "SUPPORT.md",
];

export async function readJson(file) {
  return JSON.parse(await fs.readFile(file, "utf8"));
}

export async function sha256(file) {
  const hash = createHash("sha256");
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest("hex");
}

export async function collectFiles(root, relative = "") {
  const result = [];
  for (const entry of await fs.readdir(path.join(root, relative), { withFileTypes: true })) {
    const name = path.posix.join(relative, entry.name);
    if (entry.isSymbolicLink()) throw new Error(`Release inputs must not be symlinks: ${name}`);
    if (entry.isDirectory()) result.push(...await collectFiles(root, name));
    else if (entry.isFile()) result.push(name);
  }
  return result.sort();
}

export async function digestFiles(root, files) {
  const result = {};
  for (const file of files) result[file] = await sha256(path.join(root, file));
  return result;
}

export async function copyFile(root, destination, relative) {
  const output = path.join(destination, relative);
  await fs.mkdir(path.dirname(output), { recursive: true });
  await fs.copyFile(path.join(root, relative), output);
}

export function requireReleaseHost() {
  if (`${process.platform}-${process.arch}` !== target) {
    throw new Error(`This release currently supports only ${target}; use a matching macOS host.`);
  }
}

export function cleanRuntimeEnv() {
  return Object.fromEntries(Object.entries(process.env).filter(([key]) =>
    !/^(DJANGO_ERD_|DJERD_)/.test(key)));
}

export function run(command, args, options = {}) {
  const { capture = false, ...spawnOptions } = options;
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: repoRoot,
      stdio: capture ? ["ignore", "pipe", "inherit"] : "inherit",
      ...spawnOptions,
    });
    let stdout = "";
    if (capture) child.stdout.setEncoding("utf8").on("data", (chunk) => { stdout += chunk; });
    child.on("error", reject);
    child.on("close", (code, signal) => {
      if (code === 0) resolve(stdout.trim());
      else reject(new Error(`${command} failed (${signal ?? code})`));
    });
  });
}

export async function archiveDirectory(directory, output, names = ["."]) {
  await run("tar", ["-czf", output, "-C", directory, ...names], {
    env: { ...process.env, COPYFILE_DISABLE: "1" },
  });
}
