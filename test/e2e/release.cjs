const fs = require("node:fs/promises");
const path = require("node:path");
const suite = require("./suite/index.cjs");

// VS Code's shell launcher can exit before its test host. Persist an explicit
// outcome so a successful launcher exit cannot be mistaken for a passing test.
exports.run = async function run() {
  const output = process.env.DJANGO_ERD_E2E_RESULT_FILE;
  if (!output) throw new Error("DJANGO_ERD_E2E_RESULT_FILE is required");
  await fs.mkdir(path.dirname(output), { recursive: true });
  try {
    await suite.run();
    await fs.writeFile(output, JSON.stringify({
      scenario: process.env.DJANGO_ERD_E2E_SCENARIO, passed: true,
      checkedAt: new Date().toISOString(),
    }, null, 2) + "\n");
  } catch (error) {
    await fs.writeFile(output, JSON.stringify({
      scenario: process.env.DJANGO_ERD_E2E_SCENARIO, passed: false,
      checkedAt: new Date().toISOString(), message: String(error),
    }, null, 2) + "\n");
    throw error;
  }
};
