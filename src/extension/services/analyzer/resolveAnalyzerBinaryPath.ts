import { access } from "node:fs/promises";
import path from "node:path";

export async function resolveAnalyzerBinaryPath(
  extensionRootPath: string,
): Promise<string> {
  const envOverride = process.env.DJANGO_ERD_ANALYZER_BIN;
  const candidatePaths = [
    envOverride,
    path.join(extensionRootPath, "bin", "analyzer", `${process.platform}-${process.arch}`, analyzerBinaryName()),
    path.join(extensionRootPath, "analyzer", "target", "release", analyzerBinaryName()),
    path.join(extensionRootPath, "analyzer", "target", "debug", analyzerBinaryName()),
  ].filter((value): value is string => Boolean(value));

  for (const candidatePath of candidatePaths) {
    if (await pathExists(candidatePath)) {
      return candidatePath;
    }
  }

  throw new Error(
    `The Django ERD analyzer for ${process.platform}-${process.arch} was not found. `
    + "Install the VSIX for this extension host's operating system and architecture. "
    + "For a source checkout, run npm run build or set DJANGO_ERD_ANALYZER_BIN.",
  );
}

function analyzerBinaryName(): string {
  return process.platform === "win32"
    ? "django-erd-maker-analyzer.exe"
    : "django-erd-maker-analyzer";
}

async function pathExists(filePath: string): Promise<boolean> {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}
