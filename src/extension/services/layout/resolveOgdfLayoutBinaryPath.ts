import { access } from "node:fs/promises";
import path from "node:path";

export async function resolveOgdfLayoutBinaryPath(
  extensionRootPath: string,
): Promise<string | undefined> {
  const envOverride = process.env.DJANGO_ERD_OGDF_LAYOUT_BIN;
  const platformKey = `${process.platform}-${process.arch}`;
  const candidatePaths = [
    envOverride,
    path.join(extensionRootPath, "bin", "ogdf", platformKey, ogdfLayoutBinaryName()),
    path.join(extensionRootPath, "native", "ogdf", platformKey, ogdfLayoutBinaryName()),
  ].filter((value): value is string => Boolean(value));

  for (const candidatePath of candidatePaths) {
    if (await pathExists(candidatePath)) {
      return candidatePath;
    }
  }

  return undefined;
}

function ogdfLayoutBinaryName(): string {
  return process.platform === "win32"
    ? "django-erd-ogdf-layout.exe"
    : "django-erd-ogdf-layout";
}

async function pathExists(filePath: string): Promise<boolean> {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}
