#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";

const inputArg = process.argv[2] ?? "log.txt";
const outBaseArg = process.argv[3] ?? "codeidx-log-analysis";
const workspaceRoot = process.cwd();
const inputPath = path.resolve(workspaceRoot, inputArg);
const outBase = path.resolve(workspaceRoot, outBaseArg);

function fail(message) {
  process.stderr.write(`${message}\n`);
  process.exitCode = 1;
}

if (!fs.existsSync(inputPath)) {
  fail(`Input log does not exist: ${inputPath}`);
  process.exit(1);
}

const raw = fs.readFileSync(inputPath, "utf8");
const lines = raw.split(/\r?\n/).filter((line) => line.length > 0);
const stat = fs.statSync(inputPath);

function parseKeyValues(text) {
  const values = {};
  for (const match of text.matchAll(/([A-Za-z][A-Za-z0-9_-]*)=([^ ]+)/g)) {
    const key = match[1];
    const rawValue = match[2];
    const msValue = rawValue.match(/^(\d+)ms$/);
    if (msValue) {
      values[key] = Number(msValue[1]);
      continue;
    }
    const numeric = Number(rawValue);
    values[key] = Number.isFinite(numeric) && String(numeric) === rawValue ? numeric : rawValue;
  }
  return values;
}

function isGeneratedPath(file) {
  return /(^|\/)(graphql-codegen|__generated__|generated|dist|build|out|target)(\/|$)|\.generated\./i.test(file);
}

function countBy(items, getKey) {
  const counts = new Map();
  for (const item of items) {
    const key = getKey(item);
    counts.set(key, (counts.get(key) ?? 0) + 1);
  }
  return [...counts.entries()]
    .map(([key, count]) => ({ key, count }))
    .sort((a, b) => b.count - a.count || a.key.localeCompare(b.key));
}

function durationMs(startIso, endIso) {
  if (!startIso || !endIso) {
    return undefined;
  }
  const duration = Date.parse(endIso) - Date.parse(startIso);
  return Number.isFinite(duration) ? duration : undefined;
}

function formatMs(ms) {
  if (!Number.isFinite(ms)) {
    return "";
  }
  if (ms < 1000) {
    return `${ms}ms`;
  }
  return `${(ms / 1000).toFixed(1)}s`;
}

const counts = {
  totalLines: lines.length,
  parsedLines: 0,
  zoektUpdates: 0,
  mcpStarts: 0,
  cacheIgnored: 0,
  manifestRestored: 0,
  incrementalStarts: 0,
  incrementalUpdated: 0,
  incrementalWarnings: 0,
  documentSummaries: 0,
  missingFileErrors: 0,
};
const versions = new Set();
const events = [];
const zoekUpdates = [];
const manifestRestores = [];
const incrementalRuns = [];
const documentSummaries = [];
const missingFileErrors = [];
let currentRun = undefined;
let firstTimestamp = undefined;
let lastTimestamp = undefined;

lines.forEach((line, index) => {
  const lineNumber = index + 1;
  const parsed = line.match(/^\[([^\]]+)\]\s+\[([^\]]+)\]\s+(.*)$/);
  if (!parsed) {
    events.push({ line: lineNumber, kind: "unparsed", text: line });
    return;
  }
  counts.parsedLines += 1;
  const [, timestamp, version, message] = parsed;
  firstTimestamp ??= timestamp;
  lastTimestamp = timestamp;
  versions.add(version);

  if (message.includes("zoek-rs update:")) {
    counts.zoektUpdates += 1;
    const details = parseKeyValues(message);
    zoekUpdates.push({ line: lineNumber, timestamp, ...details });
    return;
  }

  if (message.includes("codeidx MCP") && message.includes("started")) {
    counts.mcpStarts += 1;
    events.push({ line: lineNumber, timestamp, kind: "mcp_start", message });
    return;
  }

  if (message.includes("call graph cache ignored:")) {
    counts.cacheIgnored += 1;
    events.push({ line: lineNumber, timestamp, kind: "cache_ignored", message });
    return;
  }

  if (message.includes("call graph rust-native serving manifest restored:")) {
    counts.manifestRestored += 1;
    const details = parseKeyValues(message);
    manifestRestores.push({ line: lineNumber, timestamp, ...details });
    return;
  }

  if (message.includes("call graph rust-native incremental start:")) {
    counts.incrementalStarts += 1;
    const details = parseKeyValues(message);
    currentRun = {
      index: incrementalRuns.length + 1,
      startLine: lineNumber,
      startTimestamp: timestamp,
      reason: details.reason,
      changed: details.changed ?? 0,
      deleted: details.deleted ?? 0,
      ignored: details.ignored ?? 0,
      warnings: [],
      summaries: [],
      missingFileErrors: [],
    };
    incrementalRuns.push(currentRun);
    return;
  }

  if (message.includes("call graph rust-native incremental warning:")) {
    counts.incrementalWarnings += 1;
    const warning = { line: lineNumber, timestamp, message };
    if (currentRun) {
      currentRun.warnings.push(warning);
    }
    events.push({ ...warning, kind: "incremental_warning" });
    return;
  }

  if (message.includes("call graph rust-native document summary loaded:")) {
    counts.documentSummaries += 1;
    const details = parseKeyValues(message);
    const file = typeof details.file === "string" ? details.file : "";
    const summary = {
      line: lineNumber,
      timestamp,
      file,
      symbols: typeof details.symbols === "number" ? details.symbols : 0,
      generated: isGeneratedPath(file),
      existsInCurrentWorkspace: file ? fs.existsSync(path.resolve(workspaceRoot, file)) : false,
    };
    documentSummaries.push(summary);
    if (currentRun) {
      currentRun.summaries.push(summary);
    }
    return;
  }

  if (message.includes("call graph rust-native incremental updated:")) {
    counts.incrementalUpdated += 1;
    const details = parseKeyValues(message);
    if (currentRun) {
      currentRun.endLine = lineNumber;
      currentRun.endTimestamp = timestamp;
      currentRun.elapsedMs = typeof details.elapsed === "number" ? details.elapsed : undefined;
      currentRun.files = typeof details.files === "number" ? details.files : undefined;
      currentRun.wallMs = durationMs(currentRun.startTimestamp, timestamp);
      currentRun = undefined;
    }
    return;
  }

  if (message.includes("No such file or directory")) {
    counts.missingFileErrors += 1;
    const error = { line: lineNumber, timestamp, message };
    missingFileErrors.push(error);
    if (currentRun) {
      currentRun.missingFileErrors.push(error);
    }
    return;
  }

  events.push({ line: lineNumber, timestamp, kind: "other", message });
});

const fileStats = new Map();
for (const summary of documentSummaries) {
  const current = fileStats.get(summary.file) ?? {
    file: summary.file,
    count: 0,
    totalSymbols: 0,
    maxSymbols: 0,
    firstLine: summary.line,
    lastLine: summary.line,
    generated: summary.generated,
    existsInCurrentWorkspace: summary.existsInCurrentWorkspace,
  };
  current.count += 1;
  current.totalSymbols += summary.symbols;
  current.maxSymbols = Math.max(current.maxSymbols, summary.symbols);
  current.firstLine = Math.min(current.firstLine, summary.line);
  current.lastLine = Math.max(current.lastLine, summary.line);
  current.generated ||= summary.generated;
  current.existsInCurrentWorkspace ||= summary.existsInCurrentWorkspace;
  fileStats.set(summary.file, current);
}

const topFiles = [...fileStats.values()]
  .sort((a, b) => b.totalSymbols - a.totalSymbols || b.count - a.count || a.file.localeCompare(b.file))
  .slice(0, 25);

const totalSummarySymbols = documentSummaries.reduce((sum, summary) => sum + summary.symbols, 0);
const generatedSummarySymbols = documentSummaries
  .filter((summary) => summary.generated)
  .reduce((sum, summary) => sum + summary.symbols, 0);
const generatedSummaryCount = documentSummaries.filter((summary) => summary.generated).length;
const existingSummaryCount = documentSummaries.filter((summary) => summary.existsInCurrentWorkspace).length;
const slowRuns = incrementalRuns.filter((run) => (run.elapsedMs ?? 0) >= 120_000);
const externalDeletedRuns = incrementalRuns.filter((run) => run.reason === "external-deleted");
const largeGeneratedFiles = topFiles.filter((file) => file.generated && file.maxSymbols >= 1_000);
const manifest = manifestRestores[manifestRestores.length - 1];
const issues = [];
const codeidxEventCount =
  counts.zoektUpdates +
  counts.mcpStarts +
  counts.cacheIgnored +
  counts.manifestRestored +
  counts.incrementalStarts +
  counts.incrementalUpdated +
  counts.documentSummaries +
  counts.missingFileErrors;
const apparentLogLevels = [...versions].filter((version) => ["info", "warn", "error"].includes(version));

if (codeidxEventCount === 0 && apparentLogLevels.length > 0) {
  issues.push({
    severity: "critical",
    code: "unsupported_log_format",
    evidence: `parsed bracket values look like log levels: ${apparentLogLevels.join(", ")}`,
    detail: "This analyzer expects Codeidx logs with a version token such as [v0.1.711]; use an ERD log analyzer for this file.",
  });
}

if (counts.cacheIgnored > 0) {
  issues.push({
    severity: "info",
    code: "call_graph_cache_ignored",
    evidence: `line ${events.find((event) => event.kind === "cache_ignored")?.line ?? "?"}`,
    detail: "Call graph cache metadata changed, so the persisted cache was not trusted for this activation.",
  });
}

if (manifest && typeof manifest.files === "number" && manifest.files >= 100_000) {
  issues.push({
    severity: "warning",
    code: "large_restored_manifest",
    evidence: `line ${manifest.line}`,
    detail: `Restored manifest contains ${manifest.files} files, ${manifest.symbols} symbols, and ${manifest.references} references.`,
  });
}

for (const run of slowRuns) {
  issues.push({
    severity: "critical",
    code: "slow_incremental_call_graph_update",
    evidence: `lines ${run.startLine}-${run.endLine ?? "?"}`,
    detail: `Incremental run ${run.index} processed ${run.files ?? "?"} files in ${formatMs(run.elapsedMs)}.`,
  });
}

if (externalDeletedRuns.length > 1) {
  issues.push({
    severity: "warning",
    code: "repeated_external_deleted_updates",
    evidence: externalDeletedRuns.map((run) => `line ${run.startLine}`).join(", "),
    detail: "`external-deleted` triggered multiple call graph updates in one short log window.",
  });
}

if (largeGeneratedFiles.length > 0) {
  issues.push({
    severity: "warning",
    code: "generated_file_symbol_hotspots",
    evidence: largeGeneratedFiles.slice(0, 3).map((file) => `${file.file} (${file.maxSymbols})`).join(", "),
    detail: "Generated files dominate document summary work and are candidates for search/call-graph exclusion.",
  });
}

if (counts.missingFileErrors > 0) {
  issues.push({
    severity: "warning",
    code: "missing_file_error_without_path",
    evidence: missingFileErrors.map((error) => `line ${error.line}`).join(", "),
    detail: "`os error 2` is logged without the missing path, so root cause cannot be isolated from the log alone.",
  });
}

if (documentSummaries.length > 0 && existingSummaryCount / documentSummaries.length < 0.25) {
  issues.push({
    severity: "info",
    code: "log_paths_do_not_match_current_workspace",
    evidence: `${existingSummaryCount}/${documentSummaries.length} summary paths exist below ${workspaceRoot}`,
    detail: "The analyzed log likely came from another workspace or was copied here for diagnosis.",
  });
}

const recommendedExcludes = [
  "**/graphql-codegen/**",
  "**/__generated__/**",
  "**/*.generated.ts",
  "**/*.generated.tsx",
  "**/*.generated.py",
  "log*.txt",
  "codeidx-log-analysis.*",
  "erd-layout-final.json",
  "erd-topology-diagnostics.json",
  ".codeidx/**",
];
const recommendedCallGraphExcludes = recommendedExcludes.filter(
  (glob) => !["log*.txt", "codeidx-log-analysis.*", "erd-layout-final.json", "erd-topology-diagnostics.json"].includes(glob),
);
const recommendedSettings = {
  "intellijStyledSearch.excludeGlobs": recommendedExcludes,
  "intellijStyledSearch.callGraphExcludeGlobs": recommendedCallGraphExcludes,
};

const report = {
  schemaVersion: "codeidx-log-analysis/v1",
  source: path.relative(workspaceRoot, inputPath) || inputPath,
  generatedAt: new Date().toISOString(),
  workspaceRoot,
  input: {
    bytes: stat.size,
    lines: lines.length,
  },
  timeRange: {
    firstTimestamp,
    lastTimestamp,
    wallMs: durationMs(firstTimestamp, lastTimestamp),
  },
  versions: [...versions].sort(),
  counts,
  manifestRestores,
  incrementalRuns: incrementalRuns.map((run) => ({
    index: run.index,
    reason: run.reason,
    startLine: run.startLine,
    endLine: run.endLine,
    changed: run.changed,
    deleted: run.deleted,
    ignored: run.ignored,
    files: run.files,
    elapsedMs: run.elapsedMs,
    wallMs: run.wallMs,
    summaries: run.summaries.length,
    summarySymbols: run.summaries.reduce((sum, summary) => sum + summary.symbols, 0),
    generatedSummaries: run.summaries.filter((summary) => summary.generated).length,
    generatedSymbols: run.summaries.filter((summary) => summary.generated).reduce((sum, summary) => sum + summary.symbols, 0),
    warnings: run.warnings,
    missingFileErrors: run.missingFileErrors,
  })),
  documentSummaryTotals: {
    count: documentSummaries.length,
    totalSymbols: totalSummarySymbols,
    generatedCount: generatedSummaryCount,
    generatedSymbols: generatedSummarySymbols,
    generatedSymbolRatio: totalSummarySymbols > 0 ? generatedSummarySymbols / totalSummarySymbols : 0,
    existingInCurrentWorkspace: existingSummaryCount,
  },
  topFiles,
  topPathPrefixes: countBy(documentSummaries, (summary) => summary.file.split("/").slice(0, 3).join("/")).slice(0, 15),
  zoekUpdates,
  missingFileErrors,
  issues,
  recommendedExcludes,
  recommendedSettings,
};

function mdEscape(value) {
  return String(value).replaceAll("|", "\\|").replaceAll("\n", " ");
}

function markdownTable(headers, rows) {
  const lines = [
    `| ${headers.map(mdEscape).join(" | ")} |`,
    `| ${headers.map(() => "---").join(" | ")} |`,
  ];
  for (const row of rows) {
    lines.push(`| ${row.map((value) => mdEscape(value ?? "")).join(" | ")} |`);
  }
  return lines.join("\n");
}

const md = [
  "# Codeidx Log Analysis",
  "",
  `Input: \`${report.source}\``,
  `Generated: \`${report.generatedAt}\``,
  `Range: \`${firstTimestamp ?? "unknown"}\` to \`${lastTimestamp ?? "unknown"}\` (${formatMs(report.timeRange.wallMs)})`,
  "",
  "## Summary",
  "",
  markdownTable(
    ["Metric", "Value"],
    [
      ["Lines", report.input.lines],
      ["Bytes", report.input.bytes],
      ["Versions", report.versions.join(", ")],
      ["Zoekt updates", counts.zoektUpdates],
      ["Incremental starts", counts.incrementalStarts],
      ["Incremental updates", counts.incrementalUpdated],
      ["Document summaries", counts.documentSummaries],
      ["Missing-file errors", counts.missingFileErrors],
      ["Generated symbol ratio", `${(report.documentSummaryTotals.generatedSymbolRatio * 100).toFixed(1)}%`],
      ["Summary paths in current workspace", `${existingSummaryCount}/${documentSummaries.length}`],
    ],
  ),
  "",
  "## Issues",
  "",
  issues.length > 0
    ? markdownTable(
        ["Severity", "Code", "Evidence", "Detail"],
        issues.map((issue) => [issue.severity, issue.code, issue.evidence, issue.detail]),
      )
    : "No issues detected.",
  "",
  "## Incremental Runs",
  "",
  incrementalRuns.length > 0
    ? markdownTable(
        ["#", "Reason", "Lines", "Changed", "Deleted", "Files", "Elapsed", "Summaries", "Generated Symbols"],
        report.incrementalRuns.map((run) => [
          run.index,
          run.reason,
          `${run.startLine}-${run.endLine ?? "?"}`,
          run.changed,
          run.deleted,
          run.files,
          formatMs(run.elapsedMs),
          run.summaries,
          run.generatedSymbols,
        ]),
      )
    : "No incremental runs detected.",
  "",
  "## Hot Files",
  "",
  topFiles.length > 0
    ? markdownTable(
        ["File", "Count", "Total Symbols", "Max Symbols", "Generated", "Exists Here"],
        topFiles.slice(0, 15).map((file) => [
          file.file,
          file.count,
          file.totalSymbols,
          file.maxSymbols,
          file.generated ? "yes" : "no",
          file.existsInCurrentWorkspace ? "yes" : "no",
        ]),
      )
    : "No document summary files detected.",
  "",
  "## Recommended Excludes",
  "",
  ...recommendedExcludes.map((glob) => `- \`${glob}\``),
  "",
  "## VS Code Settings Snippet",
  "",
  "Apply this in the workspace that produced the log.",
  "",
  "```json",
  JSON.stringify(recommendedSettings, null, 2),
  "```",
  "",
].join("\n");

fs.writeFileSync(`${outBase}.json`, `${JSON.stringify(report, null, 2)}\n`);
fs.writeFileSync(`${outBase}.md`, md);

process.stdout.write(`Wrote ${path.relative(workspaceRoot, `${outBase}.json`)}\n`);
process.stdout.write(`Wrote ${path.relative(workspaceRoot, `${outBase}.md`)}\n`);
