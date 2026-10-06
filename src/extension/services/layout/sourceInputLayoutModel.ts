import { stat } from "node:fs/promises";
import path from "node:path";

// This is a shared numeric predictor, not a graph-signature layout snapshot.
// Native inference rebuilds all features from this request's original source.
export async function resolveSourceInputLayoutModelPath(
  extensionRootPath: string,
  enabled: boolean,
): Promise<string | undefined> {
  if (!enabled || process.env.DJERD_SOURCE_LAYOUT_MODEL === "0") return undefined;
  const override = process.env.DJERD_SOURCE_LAYOUT_MODEL_PATH;
  const candidate = override ?? path.join(extensionRootPath, "media", "source-layout", "model.bin");
  if (!candidate) return undefined;
  try {
    const info = await stat(candidate);
    return info.isFile() && info.size === 68_648 ? candidate : undefined;
  } catch {
    return undefined;
  }
}
