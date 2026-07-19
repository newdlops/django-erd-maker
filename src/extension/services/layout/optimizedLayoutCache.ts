import { readFile, writeFile } from "node:fs/promises";

import type {
  LayoutEngineMetadata,
  LayoutSnapshot,
} from "../../../shared/graph/layoutContract";
import { decodeLayoutSnapshot } from "../../../shared/protocol/decodeDiagramBootstrap";
import {
  DEFAULT_CANONICAL_OBSTACLE_RELIEF_MAX_VISUAL_DEBT_PER_GAIN,
  evaluateCanonicalCrossingNonRegression,
  evaluateCanonicalObstacleRelief,
} from "./canonicalCrossingNonRegression";

export interface OptimizedLayoutFlightLease {
  readonly waited: boolean;
  release(): void;
}

export interface OptimizedLayoutCacheSelection {
  readonly candidateReason?: "canonical-obstacle-relief" | "quality";
  readonly candidateVisualCrossings?: number;
  readonly existingVisualCrossings?: number;
  readonly json: string;
  readonly preservationReason?: "canonical-non-regression" | "quality";
  readonly source: "candidate" | "existing";
}

export interface OptimizedLayoutCacheSelectionOptions {
  readonly maxCanonicalVisualDebtPerGain?: number;
}

const optimizedLayoutFlights = new Map<string, Promise<void>>();

// A keyed FIFO lease keeps identical optimized runs from racing on the same
// cache file. Followers wait for the producer, then re-check the cache and
// reuse its result. Different topology/options keys remain fully concurrent.
export async function acquireOptimizedLayoutFlight(
  key: string,
  onWait?: () => void,
): Promise<OptimizedLayoutFlightLease> {
  const previous = optimizedLayoutFlights.get(key);
  let resolveCurrent: (() => void) | undefined;
  const current = new Promise<void>((resolve) => {
    resolveCurrent = resolve;
  });
  optimizedLayoutFlights.set(key, current);

  if (previous) {
    onWait?.();
    await previous;
  }

  let released = false;
  return {
    waited: previous !== undefined,
    release(): void {
      if (released) {
        return;
      }
      released = true;
      resolveCurrent?.();
      if (optimizedLayoutFlights.get(key) === current) {
        optimizedLayoutFlights.delete(key);
      }
    },
  };
}

// Negative means left is preferable, positive means right is preferable.
// visualCrossings is the optimized layout's acceptance metric; the remaining
// fields are deterministic tie-breakers that avoid replacing an equally clean
// cache entry with one that regresses another reported conflict metric.
export function compareOptimizedLayoutQuality(
  left: LayoutEngineMetadata | undefined,
  right: LayoutEngineMetadata | undefined,
): number {
  const lowerIsBetter: Array<keyof LayoutEngineMetadata> = [
    "visualCrossings",
    "nodeOverlaps",
    "bundleNodeOverlaps",
    "edgeNodeIntersections",
    "edgeCrossings",
    "bundleEdgeIntersections",
    "overlappingEdges",
    "edgeSegmentOverlaps",
    "nodeSpacingOverlaps",
    "rawRouteCrossings",
    "boundingBoxArea",
  ];
  for (const key of lowerIsBetter) {
    const comparison = compareFiniteMetric(left?.[key], right?.[key]);
    if (comparison !== 0) {
      return comparison;
    }
  }

  const leftQuality = finiteNumber(left?.compositeQuality);
  const rightQuality = finiteNumber(right?.compositeQuality);
  if (leftQuality === undefined && rightQuality === undefined) {
    return 0;
  }
  if (leftQuality === undefined) {
    return 1;
  }
  if (rightQuality === undefined) {
    return -1;
  }
  return rightQuality - leftQuality;
}

export function selectPreferredOptimizedLayoutJson(
  existingJson: string,
  candidateJson: string,
  options: OptimizedLayoutCacheSelectionOptions = {},
): OptimizedLayoutCacheSelection {
  const candidate = decodeLayoutSnapshot(
    JSON.parse(candidateJson),
    "ogdfOptimizedLayoutCacheCandidate",
  );
  let existing: LayoutSnapshot;
  try {
    existing = decodeLayoutSnapshot(
      JSON.parse(existingJson),
      "ogdfOptimizedLayoutCacheExisting",
    );
  } catch {
    return selection("candidate", candidateJson, undefined, candidate);
  }

  if (!evaluateCanonicalCrossingNonRegression(
    existing.engineMetadata,
    candidate.engineMetadata,
  ).ok) {
    return selection(
      "existing",
      existingJson,
      existing,
      candidate,
      "canonical-non-regression",
    );
  }
  const canonicalObstacleRelief = evaluateCanonicalObstacleRelief(
    existing.engineMetadata,
    candidate.engineMetadata,
    finiteNumber(existing.engineMetadata?.visualCrossings) ?? Number.NaN,
    finiteNumber(candidate.engineMetadata?.visualCrossings) ?? Number.NaN,
    options.maxCanonicalVisualDebtPerGain
      ?? DEFAULT_CANONICAL_OBSTACLE_RELIEF_MAX_VISUAL_DEBT_PER_GAIN,
  );
  if (canonicalObstacleRelief.ok) {
    return selection(
      "candidate",
      candidateJson,
      existing,
      candidate,
      undefined,
      "canonical-obstacle-relief",
    );
  }
  if (compareOptimizedLayoutQuality(
    existing.engineMetadata,
    candidate.engineMetadata,
  ) <= 0) {
    return selection("existing", existingJson, existing, candidate, "quality");
  }
  return selection(
    "candidate",
    candidateJson,
    existing,
    candidate,
    undefined,
    "quality",
  );
}

// The caller holds the in-process flight lease. Re-reading immediately before
// write also protects against a result produced by another extension host or
// an execution that began before the current host acquired its lease.
export async function preserveBestOptimizedLayoutCache(
  cachePath: string,
  candidateJson: string,
  options: OptimizedLayoutCacheSelectionOptions = {},
): Promise<OptimizedLayoutCacheSelection> {
  let result: OptimizedLayoutCacheSelection;
  try {
    const existingJson = await readFile(cachePath, "utf8");
    result = selectPreferredOptimizedLayoutJson(
      existingJson,
      candidateJson,
      options,
    );
  } catch {
    const candidate = decodeLayoutSnapshot(
      JSON.parse(candidateJson),
      "ogdfOptimizedLayoutCacheCandidate",
    );
    result = selection("candidate", candidateJson, undefined, candidate);
  }

  if (result.source === "candidate") {
    await writeFile(cachePath, result.json, "utf8");
  }
  return result;
}

function compareFiniteMetric(left: unknown, right: unknown): number {
  const leftNumber = finiteNumber(left);
  const rightNumber = finiteNumber(right);
  if (leftNumber === undefined && rightNumber === undefined) {
    return 0;
  }
  if (leftNumber === undefined) {
    return 1;
  }
  if (rightNumber === undefined) {
    return -1;
  }
  return leftNumber - rightNumber;
}

function finiteNumber(value: unknown): number | undefined {
  return typeof value === "number" && Number.isFinite(value)
    ? value
    : undefined;
}

function selection(
  source: "candidate" | "existing",
  json: string,
  existing: LayoutSnapshot | undefined,
  candidate: LayoutSnapshot,
  preservationReason?: "canonical-non-regression" | "quality",
  candidateReason?: "canonical-obstacle-relief" | "quality",
): OptimizedLayoutCacheSelection {
  return {
    candidateReason,
    candidateVisualCrossings: finiteNumber(
      candidate.engineMetadata?.visualCrossings,
    ),
    existingVisualCrossings: finiteNumber(
      existing?.engineMetadata?.visualCrossings,
    ),
    json,
    preservationReason,
    source,
  };
}
