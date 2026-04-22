import fs from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, "../..");
const scenarioFilePath = path.join(__dirname, "scenarios.json");
const suitePath = path.join(__dirname, "suite", "index.cjs");
const fixtureRoot = path.join(repoRoot, "test", "fixtures", "django");

const rawScenarios = JSON.parse(await fs.readFile(scenarioFilePath, "utf8"));
const selectedScenarioIds = (process.env.DJANGO_ERD_E2E_SCENARIOS || "")
  .split(",")
  .map((value) => value.trim())
  .filter(Boolean);
const scenarios = selectedScenarioIds.length > 0
  ? rawScenarios.filter((scenario) => selectedScenarioIds.includes(scenario.id))
  : rawScenarios;

if (scenarios.length === 0) {
  throw new Error("No E2E scenarios were selected.");
}

for (const scenario of scenarios) {
  await runScenario(scenario);
}

async function runScenario(scenario) {
  const workspacePath = path.join(fixtureRoot, scenario.fixture);
  const sandboxRoot = path.join(repoRoot, ".tmp-e2e", scenario.id);
  const userDataDir = path.join(sandboxRoot, "user-data");
  const extensionsDir = path.join(sandboxRoot, "extensions");
  const logs = [];

  await fs.rm(sandboxRoot, { force: true, recursive: true });
  await fs.mkdir(userDataDir, { recursive: true });
  await fs.mkdir(extensionsDir, { recursive: true });

  const args = [
    "--new-window",
    "--disable-gpu",
    "--user-data-dir",
    userDataDir,
    "--extensions-dir",
    extensionsDir,
    "--extensionDevelopmentPath",
    repoRoot,
    "--extensionTestsPath",
    suitePath,
    workspacePath,
  ];

  await new Promise((resolve, reject) => {
    const child = spawn(process.env.DJANGO_ERD_CODE_BIN || "code", args, {
      cwd: repoRoot,
      env: {
        ...process.env,
        DJANGO_ERD_E2E_SCENARIO: scenario.id,
      },
      stdio: ["ignore", "pipe", "pipe"],
    });
    const timer = setTimeout(() => {
      child.kill("SIGTERM");
      reject(new Error(`Scenario ${scenario.id} timed out.\n${logs.join("")}`));
    }, 120_000);

    child.stdout.on("data", (chunk) => {
      logs.push(String(chunk));
    });
    child.stderr.on("data", (chunk) => {
      logs.push(String(chunk));
    });
    child.on("error", (error) => {
      clearTimeout(timer);
      reject(error);
    });
    child.on("exit", (code) => {
      clearTimeout(timer);
      if (code === 0) {
        process.stdout.write(`PASS ${scenario.id} ${scenario.fixture}\n`);
        resolve(undefined);
        return;
      }

      reject(
        new Error(
          `Scenario ${scenario.id} failed with exit code ${code}.\n${logs.join("")}`,
        ),
      );
    });
  });
}
