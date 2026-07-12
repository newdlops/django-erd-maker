import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const sourceModulePath = path.resolve(
  __dirname,
  "../../out/webview/interaction/runtime/browserEventSource.js",
);

test("enabling optimized layout also enables the routed bend geometry it scores", () => {
  const { getBrowserEventSource } = require(sourceModulePath);
  const source = getBrowserEventSource();

  const optimizedHandler = source.slice(
    source.indexOf('for (const button of document.querySelectorAll("[data-optimized-toggle]"))'),
  );
  assert.match(
    optimizedHandler,
    /if \(state\.optimizedLayout\) \{[\s\S]*state\.useEdgeBends = true;/,
  );
  assert.match(
    optimizedHandler,
    /viewState: createRefreshViewStateSnapshot\(state\)/,
  );
});
