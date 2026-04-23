import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const runtimeModulePath = path.resolve(
  __dirname,
  "../../out/extension/services/graphviz/bundledGraphvizRuntime.js",
);
const {
  createBundledGraphvizEnvironment,
  getBundledGraphvizRuntimeCandidates,
  graphvizExecutableName,
  graphvizPlatformKey,
} = require(runtimeModulePath);

test("phase12 graphviz runtime resolves the platform bundle path under resources", () => {
  const extensionPath = path.join("/tmp", "django-erd-maker");
  const runtime = getBundledGraphvizRuntimeCandidates(extensionPath)[0];

  assert.equal(
    runtime.runtimeRoot,
    path.join(
      extensionPath,
      "resources",
      "graphviz",
      `${process.platform}-${process.arch}`,
    ),
  );
  assert.equal(runtime.platformKey, graphvizPlatformKey());
  assert.equal(
    runtime.dotPath,
    path.join(runtime.runtimeRoot, "bin", graphvizExecutableName()),
  );
});

test("phase12 graphviz runtime injects bundled environment variables", () => {
  const runtime = getBundledGraphvizRuntimeCandidates(
    path.join("/tmp", "django-erd-maker"),
  )[0];
  const environment = createBundledGraphvizEnvironment(
    {
      PATH: ["/usr/bin", "/bin"].join(path.delimiter),
    },
    runtime,
  );

  assert.equal(environment.GVBINDIR, runtime.pluginDirectory);
  assert.ok(environment.PATH.startsWith(runtime.binDirectory));

  const libraryDirectory = path.join(runtime.runtimeRoot, "lib");
  if (process.platform === "darwin") {
    assert.ok(environment.DYLD_LIBRARY_PATH.startsWith(libraryDirectory));
    assert.ok(environment.DYLD_FALLBACK_LIBRARY_PATH.startsWith(libraryDirectory));
  }

  if (process.platform === "linux") {
    assert.ok(environment.LD_LIBRARY_PATH.startsWith(libraryDirectory));
  }
});
