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

test("enabling optimized layout preserves the user's edge-bend preference", () => {
  const { getBrowserEventSource } = require(sourceModulePath);
  const source = getBrowserEventSource();

  const optimizedHandlerStart = source.indexOf(
    'for (const button of document.querySelectorAll("[data-optimized-toggle]"))',
  );
  const optimizedHandler = source.slice(
    optimizedHandlerStart,
    source.indexOf('root.addEventListener("click"', optimizedHandlerStart),
  );
  assert.doesNotMatch(
    optimizedHandler,
    /state\.useEdgeBends\s*=/,
  );
  assert.match(
    optimizedHandler,
    /viewState: createRefreshViewStateSnapshot\(state\)/,
  );
});
