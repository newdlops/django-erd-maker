#!/usr/bin/env node

import { spawn } from "node:child_process";
import { createWriteStream } from "node:fs";

const separator = process.argv.indexOf("--");
if (separator < 0 || separator < 3 || separator === process.argv.length - 1) {
  console.error("usage: capture_native_layout.mjs <output.json> -- <command> [args...]");
  process.exit(2);
}

const outputPath = process.argv[2];
const command = process.argv[separator + 1];
const args = process.argv.slice(separator + 2);
const output = createWriteStream(outputPath, { encoding: "utf8" });
const child = spawn(command, args, { stdio: ["ignore", "pipe", "inherit"] });
child.stdout.pipe(output);
child.on("error", (error) => {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
});
child.on("close", (code, signal) => {
  output.end(() => {
    if (signal) {
      console.error(`native layout terminated by ${signal}`);
      process.exitCode = 1;
    } else {
      process.exitCode = code ?? 1;
    }
  });
});
