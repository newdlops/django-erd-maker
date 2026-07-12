import type { LayoutEngineMetadata } from "../../../shared/graph/layoutContract";

export interface CanonicalCrossingNonRegression {
  adjacentBase: number | undefined;
  adjacentCandidate: number | undefined;
  adjacentOk: boolean;
  nodeHitsBase: number | undefined;
  nodeHitsCandidate: number | undefined;
  nodeHitsOk: boolean;
  ok: boolean;
}

function finiteNonNegativeCount(value: unknown): number | undefined {
  return typeof value === "number"
    && Number.isFinite(value)
    && value >= 0
    ? value
    : undefined;
}

function nonRegression(
  base: number | undefined,
  candidate: number | undefined,
): boolean {
  if (base === undefined) {
    return true;
  }
  return candidate !== undefined && candidate <= base;
}

export function evaluateCanonicalCrossingNonRegression(
  baseMetadata: LayoutEngineMetadata | undefined,
  candidateMetadata: LayoutEngineMetadata | undefined,
): CanonicalCrossingNonRegression {
  const adjacentBase = finiteNonNegativeCount(
    baseMetadata?.canonicalCrossing?.adjacentEdgeIntersections,
  );
  const adjacentCandidate = finiteNonNegativeCount(
    candidateMetadata?.canonicalCrossing?.adjacentEdgeIntersections,
  );
  const nodeHitsBase = finiteNonNegativeCount(
    baseMetadata?.canonicalCrossing?.nonIncidentNodeHits,
  );
  const nodeHitsCandidate = finiteNonNegativeCount(
    candidateMetadata?.canonicalCrossing?.nonIncidentNodeHits,
  );
  const adjacentOk = nonRegression(adjacentBase, adjacentCandidate);
  const nodeHitsOk = nonRegression(nodeHitsBase, nodeHitsCandidate);
  return {
    adjacentBase,
    adjacentCandidate,
    adjacentOk,
    nodeHitsBase,
    nodeHitsCandidate,
    nodeHitsOk,
    ok: adjacentOk && nodeHitsOk,
  };
}
