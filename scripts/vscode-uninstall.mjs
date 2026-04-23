import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const extensionRoot = path.resolve(__dirname, "..");
const manifest = JSON.parse(
  await fs.readFile(path.join(extensionRoot, "package.json"), "utf8"),
);

const extensionIds = [
  manifest.publisher ? `${manifest.publisher}.${manifest.name}` : undefined,
  manifest.name,
  `undefined.${manifest.name}`,
].filter(Boolean);

for (const root of globalStorageRoots()) {
  for (const extensionId of extensionIds) {
    await fs.rm(path.join(root, extensionId), { force: true, recursive: true });
  }
}

function globalStorageRoots() {
  const home = os.homedir();

  switch (process.platform) {
    case "darwin":
      return [
        path.join(home, "Library", "Application Support", "Code", "User", "globalStorage"),
        path.join(home, "Library", "Application Support", "Code - Insiders", "User", "globalStorage"),
        path.join(home, "Library", "Application Support", "VSCodium", "User", "globalStorage"),
      ];
    case "win32": {
      const appData = process.env.APPDATA ?? path.join(home, "AppData", "Roaming");
      return [
        path.join(appData, "Code", "User", "globalStorage"),
        path.join(appData, "Code - Insiders", "User", "globalStorage"),
        path.join(appData, "VSCodium", "User", "globalStorage"),
      ];
    }
    default:
      return [
        path.join(home, ".config", "Code", "User", "globalStorage"),
        path.join(home, ".config", "Code - Insiders", "User", "globalStorage"),
        path.join(home, ".config", "VSCodium", "User", "globalStorage"),
      ];
  }
}
