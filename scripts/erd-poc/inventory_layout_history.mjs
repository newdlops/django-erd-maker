#!/usr/bin/env node

import { createHash } from "node:crypto";
import { lstat, readFile, readdir } from "node:fs/promises";
import { extname, resolve } from "node:path";

const args = process.argv.slice(2);
const roots = args.filter((arg) => !arg.startsWith("--"));
const readNumberOption = (name, fallback) => {
  const prefix = `${name}=`;
  const raw = args.find((arg) => arg.startsWith(prefix));
  if (!raw) return fallback;
  const value = Number(raw.slice(prefix.length));
  return Number.isFinite(value) ? value : fallback;
};

const minVisual = readNumberOption("--min", Number.NEGATIVE_INFINITY);
const maxVisual = readNumberOption("--max", Number.POSITIVE_INFINITY);
const maxJsonBytes = readNumberOption("--max-json-mb", 80) * 1024 * 1024;
const jsonRecords = [];
const logRecords = [];

const numberOrNull = (value) =>
  typeof value === "number" && Number.isFinite(value) ? value : null;

const fieldFromLine = (line, name) => {
  const match = line.match(new RegExp(`${name}=(-?[0-9]+(?:\\.[0-9]+)?)`));
  return match ? Number(match[1]) : null;
};

const shortHash = (values) =>
  createHash("sha256").update(values.join("\n")).digest("hex").slice(0, 12);

const inspectLayoutJson = (path, value) => {
  if (!value || !Array.isArray(value.nodes) || !Array.isArray(value.routedEdges)) {
    return;
  }

  const metadata = value.engineMetadata ?? value.metadata ?? {};
  const visual = numberOrNull(metadata.visualCrossings ?? value.visualCrossings);
  if (visual !== null && (visual < minVisual || visual > maxVisual)) return;

  const nodeIds = new Set(
    value.nodes
      .map((node) => node?.modelId ?? node?.id)
      .filter((id) => typeof id === "string"),
  );
  const edgeKeys = value.routedEdges.map((edge) =>
    [
      edge?.edgeId ?? edge?.id ?? "",
      edge?.sourceModelId ?? edge?.source ?? "",
      edge?.targetModelId ?? edge?.target ?? "",
    ].join("\t"),
  );
  const missingEndpoints = value.routedEdges.filter(
    (edge) =>
      !nodeIds.has(edge?.sourceModelId ?? edge?.source) ||
      !nodeIds.has(edge?.targetModelId ?? edge?.target),
  ).length;
  const routeSegments = value.routedEdges.reduce(
    (total, edge) => total + Math.max(0, (edge?.points?.length ?? 0) - 1),
    0,
  );
  const straightRoutes = value.routedEdges.filter((edge) => edge?.points?.length === 2).length;
  const disconnectedRoutes = value.routedEdges.filter((edge) => (edge?.points?.length ?? 0) < 2).length;

  const carriers = Array.isArray(metadata.renderedCarrierRoutes)
    ? metadata.renderedCarrierRoutes
    : [];
  const carrierMembers = carriers.flatMap((carrier) =>
    Array.isArray(carrier?.memberEdgeIds) ? carrier.memberEdgeIds : [],
  );
  const carrierMemberSet = new Set(carrierMembers);
  const identityCarriers = carriers.filter((carrier) => {
    const members = Array.isArray(carrier?.memberEdgeIds) ? carrier.memberEdgeIds : [];
    return members.length === 1 && members[0] === carrier?.carrierId;
  }).length;
  const bundledCarriers = carriers.filter(
    (carrier) => (carrier?.memberEdgeIds?.length ?? 0) > 1,
  ).length;
  const carrierSegments = carriers.reduce(
    (total, carrier) => total + Math.max(0, (carrier?.points?.length ?? 0) - 1),
    0,
  );

  const allRoutesDirect =
    value.routedEdges.length > 0 &&
    missingEndpoints === 0 &&
    disconnectedRoutes === 0 &&
    straightRoutes === value.routedEdges.length &&
    carriers.length === value.routedEdges.length &&
    identityCarriers === carriers.length &&
    carrierMemberSet.size === value.routedEdges.length;

  jsonRecords.push({
    type: "json",
    path,
    line: null,
    visual,
    edgeCross: numberOrNull(metadata.edgeCrossings),
    edgeNode: numberOrNull(metadata.edgeNodeIntersections),
    nodeOverlap: numberOrNull(metadata.nodeOverlaps),
    bend: numberOrNull(metadata.edgeBendTotal),
    nodes: value.nodes.length,
    edges: value.routedEdges.length,
    visible: carriers.length || null,
    carrierMembers: carrierMemberSet.size || null,
    routeSegments,
    carrierSegments: carriers.length ? carrierSegments : null,
    straightRoutes,
    identityCarriers,
    bundledCarriers,
    missingEndpoints,
    disconnectedRoutes,
    leafBundles: Array.isArray(metadata.leafBundles) ? metadata.leafBundles.length : null,
    direct: allRoutesDirect,
    nodeSignature: shortHash([...nodeIds].sort()),
    graphSignature: shortHash(edgeKeys.sort()),
  });
};

const inspectLog = (path, text) => {
  const lines = text.split(/\r?\n/);
  let lastRendered = null;
  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];
    if (line.includes("[rendered-carrier-metrics-final]")) {
      lastRendered = {
        rawCross: fieldFromLine(line, "rawCross"),
        visible: fieldFromLine(line, "visibleEdges"),
        carrierSegments: fieldFromLine(line, "routeSegments"),
      };
      continue;
    }
    if (!line.includes("OGDF layout completed") || !line.includes("visualCrossings=")) {
      continue;
    }
    const visual = fieldFromLine(line, "visualCrossings");
    if (visual === null || visual < minVisual || visual > maxVisual) continue;
    const edges = fieldFromLine(line, "routedEdges");
    const routeSegments = fieldFromLine(line, "routeSegments");
    const leafBundles = fieldFromLine(line, "leafBundles");
    const visible = lastRendered?.visible ?? routeSegments;
    const direct =
      edges !== null &&
      routeSegments === edges &&
      visible === edges &&
      (leafBundles ?? 0) === 0 &&
      fieldFromLine(line, "edgeBend") === 0;
    logRecords.push({
      type: "log",
      path,
      line: index + 1,
      visual,
      edgeCross: fieldFromLine(line, "edgeCrossings"),
      edgeNode: fieldFromLine(line, "edgeNodeIntersections"),
      nodeOverlap: fieldFromLine(line, "nodeOverlaps"),
      bend: fieldFromLine(line, "edgeBend"),
      nodes: fieldFromLine(line, "nodes"),
      edges,
      visible,
      carrierMembers: null,
      routeSegments,
      carrierSegments: lastRendered?.carrierSegments ?? null,
      straightRoutes: null,
      identityCarriers: null,
      bundledCarriers: null,
      missingEndpoints: null,
      disconnectedRoutes: null,
      leafBundles,
      direct,
      rawCross: lastRendered?.rawCross ?? null,
      nodeSignature: null,
      graphSignature: null,
    });
  }
};

const inspectFile = async (path) => {
  const extension = extname(path).toLowerCase();
  if (![".json", ".log", ".txt"].includes(extension)) return;
  const stat = await lstat(path);
  if (extension === ".json" && stat.size > maxJsonBytes) return;
  const text = await readFile(path, "utf8");
  if (extension === ".json") {
    try {
      inspectLayoutJson(path, JSON.parse(text));
    } catch {
      // A partial diagnostic artifact should not abort the rest of the inventory.
    }
  } else {
    inspectLog(path, text);
  }
};

const walk = async (path) => {
  const stat = await lstat(path);
  if (!stat.isDirectory()) {
    await inspectFile(path);
    return;
  }
  const entries = await readdir(path, { withFileTypes: true });
  for (const entry of entries) {
    if (entry.name === "node_modules" || entry.name === ".git") continue;
    await walk(resolve(path, entry.name));
  }
};

if (roots.length === 0) {
  console.error(
    "usage: inventory_layout_history.mjs [--min=N] [--max=N] [--max-json-mb=N] <file-or-dir> ...",
  );
  process.exitCode = 2;
} else {
  for (const root of roots) await walk(resolve(root));

  const records = [...jsonRecords, ...logRecords].sort(
    (left, right) =>
      (left.visual ?? Number.POSITIVE_INFINITY) -
        (right.visual ?? Number.POSITIVE_INFINITY) ||
      left.path.localeCompare(right.path) ||
      (left.line ?? 0) - (right.line ?? 0),
  );
  const columns = [
    "type",
    "visual",
    "edgeCross",
    "edgeNode",
    "nodeOverlap",
    "bend",
    "nodes",
    "edges",
    "visible",
    "routeSegments",
    "carrierMembers",
    "identityCarriers",
    "bundledCarriers",
    "straightRoutes",
    "missingEndpoints",
    "disconnectedRoutes",
    "leafBundles",
    "direct",
    "rawCross",
    "nodeSignature",
    "graphSignature",
    "source",
  ];
  console.log(columns.join("\t"));
  for (const record of records) {
    const source = record.line ? `${record.path}:${record.line}` : record.path;
    console.log(
      columns
        .map((column) => {
          if (column === "source") return source;
          const value = record[column];
          return value === null || value === undefined ? "-" : String(value);
        })
        .join("\t"),
    );
  }
}
