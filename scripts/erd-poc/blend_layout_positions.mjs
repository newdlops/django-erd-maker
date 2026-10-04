#!/usr/bin/env node

import { readFile, writeFile } from "node:fs/promises";

const args = process.argv.slice(2);
const option = (name) => {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
};
const sourcePath = option("--source");
const targetPath = option("--target");
const outPath = option("--out");
const alphaValue = Number(option("--alpha"));
const scanRaw = option("--scan") ?? "0,0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1";

if (!sourcePath || !targetPath) {
  console.error(
    "usage: blend_layout_positions.mjs --source old.json --target current.json "
      + "[--scan 0,0.25,0.5,0.75,1] [--alpha 0.5 --out positions.tsv]",
  );
  process.exit(2);
}

const [source, target] = await Promise.all([
  readFile(sourcePath, "utf8").then(JSON.parse),
  readFile(targetPath, "utf8").then(JSON.parse),
]);

const centerOf = (node) => ({
  x: node.position.x + node.size.width / 2,
  y: node.position.y + node.size.height / 2,
});
const sourceCenters = new Map(source.nodes.map((node) => [node.modelId, centerOf(node)]));
const targetCenters = new Map(target.nodes.map((node) => [node.modelId, centerOf(node)]));
const common = target.nodes.filter((node) => sourceCenters.has(node.modelId));

const fitSimilarity = (reflect) => {
  let sourceCx = 0;
  let sourceCy = 0;
  let targetCx = 0;
  let targetCy = 0;
  for (const node of common) {
    const sourcePoint = sourceCenters.get(node.modelId);
    const targetPoint = targetCenters.get(node.modelId);
    sourceCx += sourcePoint.x;
    sourceCy += sourcePoint.y;
    targetCx += targetPoint.x;
    targetCy += targetPoint.y;
  }
  sourceCx /= common.length;
  sourceCy /= common.length;
  targetCx /= common.length;
  targetCy /= common.length;

  let dot = 0;
  let cross = 0;
  let denominator = 0;
  for (const node of common) {
    const sourcePoint = sourceCenters.get(node.modelId);
    const targetPoint = targetCenters.get(node.modelId);
    const sx = sourcePoint.x - sourceCx;
    const sy = sourcePoint.y - sourceCy;
    const tx = targetPoint.x - targetCx;
    const ty = (targetPoint.y - targetCy) * (reflect ? -1 : 1);
    dot += tx * sx + ty * sy;
    cross += tx * sy - ty * sx;
    denominator += tx * tx + ty * ty;
  }
  const norm = Math.hypot(dot, cross) || 1;
  const cosine = dot / norm;
  const sine = cross / norm;
  const scale = norm / Math.max(1, denominator);
  const apply = (point) => {
    const tx = point.x - targetCx;
    const ty = (point.y - targetCy) * (reflect ? -1 : 1);
    return {
      x: sourceCx + scale * (cosine * tx - sine * ty),
      y: sourceCy + scale * (sine * tx + cosine * ty),
    };
  };
  let error = 0;
  for (const node of common) {
    const actual = sourceCenters.get(node.modelId);
    const predicted = apply(targetCenters.get(node.modelId));
    error += (actual.x - predicted.x) ** 2 + (actual.y - predicted.y) ** 2;
  }
  return { apply, error, reflect, scale };
};

const fits = [fitSimilarity(false), fitSimilarity(true)];
const fit = fits[0].error <= fits[1].error ? fits[0] : fits[1];
const alignedTarget = new Map(
  [...targetCenters].map(([modelId, point]) => [modelId, fit.apply(point)]),
);

const positionsAt = (alpha) => {
  const positions = new Map();
  for (const node of target.nodes) {
    const oldPoint = sourceCenters.get(node.modelId);
    const newPoint = alignedTarget.get(node.modelId);
    positions.set(
      node.modelId,
      oldPoint
        ? {
          x: oldPoint.x + (newPoint.x - oldPoint.x) * alpha,
          y: oldPoint.y + (newPoint.y - oldPoint.y) * alpha,
        }
        : newPoint,
    );
  }
  return positions;
};

const orientation = (a, b, c) =>
  (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
const properCross = (a, b, c, d) =>
  orientation(a, b, c) * orientation(a, b, d) < 0
  && orientation(c, d, a) * orientation(c, d, b) < 0;
const isInheritance = (edge) => edge.edgeId.startsWith("edge:inheritance:");

const measure = (alpha) => {
  const positions = positionsAt(alpha);
  const edges = target.routedEdges.filter(
    (edge) =>
      edge.sourceModelId !== edge.targetModelId
      && positions.has(edge.sourceModelId)
      && positions.has(edge.targetModelId),
  );
  let declaredDeclared = 0;
  let declaredInheritance = 0;
  let inheritanceInheritance = 0;
  for (let leftIndex = 0; leftIndex < edges.length; leftIndex += 1) {
    const left = edges[leftIndex];
    for (let rightIndex = leftIndex + 1; rightIndex < edges.length; rightIndex += 1) {
      const right = edges[rightIndex];
      if (
        left.sourceModelId === right.sourceModelId
        || left.sourceModelId === right.targetModelId
        || left.targetModelId === right.sourceModelId
        || left.targetModelId === right.targetModelId
      ) {
        continue;
      }
      if (
        !properCross(
          positions.get(left.sourceModelId),
          positions.get(left.targetModelId),
          positions.get(right.sourceModelId),
          positions.get(right.targetModelId),
        )
      ) {
        continue;
      }
      const leftInheritance = isInheritance(left);
      const rightInheritance = isInheritance(right);
      if (leftInheritance && rightInheritance) inheritanceInheritance += 1;
      else if (leftInheritance || rightInheritance) declaredInheritance += 1;
      else declaredDeclared += 1;
    }
  }

  let nodeOverlaps = 0;
  for (let leftIndex = 0; leftIndex < target.nodes.length; leftIndex += 1) {
    const left = target.nodes[leftIndex];
    const leftPoint = positions.get(left.modelId);
    for (let rightIndex = leftIndex + 1; rightIndex < target.nodes.length; rightIndex += 1) {
      const right = target.nodes[rightIndex];
      const rightPoint = positions.get(right.modelId);
      if (
        Math.abs(leftPoint.x - rightPoint.x) < (left.size.width + right.size.width) / 2
        && Math.abs(leftPoint.y - rightPoint.y) < (left.size.height + right.size.height) / 2
      ) {
        nodeOverlaps += 1;
      }
    }
  }
  return {
    alpha,
    declaredDeclared,
    declaredInheritance,
    inheritanceInheritance,
    nodeOverlaps,
    total: declaredDeclared + declaredInheritance + inheritanceInheritance,
  };
};

console.log(
  JSON.stringify({
    alignment: {
      commonNodes: common.length,
      reflect: fit.reflect,
      rmsError: Math.sqrt(fit.error / Math.max(1, common.length)),
      scale: fit.scale,
      sourceOnly: source.nodes.length - common.length,
      targetOnly: target.nodes.length - common.length,
    },
  }),
);
console.log("alpha\ttotal\tdeclared-declared\tdeclared-inheritance\tinheritance-inheritance\tnode-overlaps");
for (const alpha of scanRaw.split(",").map(Number).filter(Number.isFinite)) {
  const result = measure(alpha);
  console.log(
    [
      result.alpha,
      result.total,
      result.declaredDeclared,
      result.declaredInheritance,
      result.inheritanceInheritance,
      result.nodeOverlaps,
    ].join("\t"),
  );
}

if (outPath) {
  if (!Number.isFinite(alphaValue)) {
    throw new Error("--out requires a numeric --alpha");
  }
  const positions = positionsAt(alphaValue);
  const rows = target.nodes.map((node) => {
    const point = positions.get(node.modelId);
    return `${node.modelId}\t${point.x.toFixed(3)}\t${point.y.toFixed(3)}`;
  });
  await writeFile(outPath, `${rows.join("\n")}\n`, "utf8");
}
