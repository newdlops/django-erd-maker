import { execFile } from "node:child_process";
import { mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

// Local FNV-1a 64-bit hash (avoid pulling in node:crypto type defs).
function fnvHash(...parts: string[]): string {
  let h1 = 0x811c9dc5 >>> 0;
  let h2 = 0x01000193 >>> 0;
  for (const part of parts) {
    for (let i = 0; i < part.length; i++) {
      const c = part.charCodeAt(i);
      h1 = ((h1 ^ c) * 16777619) >>> 0;
      h2 = ((h2 ^ (c << 4)) * 16777619) >>> 0;
    }
  }
  return h1.toString(16).padStart(8, "0") + h2.toString(16).padStart(8, "0");
}

async function fileStatFingerprint(filePath: string): Promise<string> {
  try {
    const fileStat = await stat(filePath);
    return `${filePath}:${fileStat.size}:${Math.trunc(fileStat.mtimeMs)}`;
  } catch {
    return `${filePath}:missing`;
  }
}

const LAYOUT_ENV_CACHE_BLOCKLIST = new Set([
  "DJANGO_ERD_PRESERVE_LAYOUT_INPUTS",
  "DJERD_LAYOUT_FROM_FILE",
  "DJERD_LAYOUT_OUTPUT_FILE",
  "DJERD_OPTIMIZED_LAYOUT_FILE",
  "DJERD_OPTIMIZED_POSITIONS_TSV",
  "DJERD_PROGRESS_FILE",
]);

function layoutEnvCacheKeyParts(
  env: Record<string, string | undefined>,
): string[] {
  return Object.keys(env)
    .filter((key) =>
      (key.startsWith("DJERD_") || key === "DJANGO_ERD_EDGE_ROUTING")
      && !LAYOUT_ENV_CACHE_BLOCKLIST.has(key)
    )
    .sort()
    .map((key) => `${key}=${env[key] ?? ""}`);
}


import type { StructuralGraphEdge } from "../../../shared/graph/diagramGraph";
import {
  DEFAULT_EDGE_ROUTING,
  getOgdfLayoutDefinition,
  normalizeLayoutMode,
  type EdgeRoutingStyle,
  type LayoutEngineMetadata,
  type LayoutMode,
  type LayoutSnapshot,
} from "../../../shared/graph/layoutContract";
import { decodeLayoutSnapshot } from "../../../shared/protocol/decodeDiagramBootstrap";
import type { DiagramBootstrapPayload } from "../../../shared/protocol/webviewContract";
import type { Logger } from "../logging/logger";
import { evaluateCanonicalCrossingNonRegression } from "./canonicalCrossingNonRegression";
import { consolidateEdges } from "./consolidateEdges";
import {
  acquireOptimizedLayoutFlight,
  preserveBestOptimizedLayoutCache,
} from "./optimizedLayoutCache";
import { resolveOgdfLayoutBinaryPath } from "./resolveOgdfLayoutBinaryPath";

const OGDF_LAYOUT_TIMEOUT_MS = 600_000;
const V35_SCORER_TIMEOUT_MS = 180_000;
const DEFAULT_POST_REROUTE_POLISH_BUDGET_MS = 90_000;
const DEFAULT_RENDERED_CARRIER_THRESHOLD = "12";
const DEFAULT_OPTIMIZED_BBOX_TARGET_B = 1.0;
const DEFAULT_VISUAL_CROSS_TARGET = 500;
const DEFAULT_VISUAL_CROSS_POLISH_VARIANTS = [
  "route",
  "route-clear",
  "route-clear-tight",
  "route-clear-wide",
  "route-clear-short",
  "route-clear-deep",
  "route-clear-lbend",
  "route-clear-deep-lbend",
  "route-clear-periphery",
  "route-clear-deep-periphery",
  "detour",
  "detour-clear",
  "knot",
  "knot-clear",
].join(",");
type EdgeNodePolishVariant = "cheap" | "local" | "holistic";
type VisualCrossPolishVariant =
  | "route"
  | "route-clear"
  | "route-clear-tight"
  | "route-clear-wide"
  | "route-clear-short"
  | "route-clear-deep"
  | "route-clear-lbend"
  | "route-clear-deep-lbend"
  | "route-clear-periphery"
  | "route-clear-deep-periphery"
  | "route-retouch"
  | "detour"
  | "detour-clear"
  | "knot"
  | "knot-clear";
type BboxTargetVariant = "local" | "holistic";
type BboxTargetPositionStrategy = "gap" | "scale" | "density-scale";

type PostReroutePolishDeadline = {
  budgetMs: number;
  deadlineMs: number;
  startedMs: number;
};

type BudgetedCandidateTimeout = {
  budgetLimited: boolean;
  timeoutMs: number;
};

function resolveV36CkptPath(extensionRootPath: string): string {
  return process.env.DJERD_V36_CKPT_PATH
    ?? process.env.DJERD_V35_CKPT_PATH
    ?? path.join(
      extensionRootPath,
      "data/erd-poc/checkpoints/v36-pure-action-scorer.pt",
    );
}

function resolveV37FamilyPriorPath(extensionRootPath: string): string {
  return process.env.DJERD_V37_FAMILY_PRIOR_CKPT_PATH
    ?? path.join(
      extensionRootPath,
      "data/erd-poc/checkpoints/v37-diverse.pt",
    );
}

function ogdfRenderedCarrierEnv(
  overrides: Record<string, string | undefined> = {},
): Record<string, string | undefined> {
  return {
    ...process.env,
    DJERD_DIAGONAL_RETOUCH: "0",
    DJERD_NODE_PAIR_RETOUCH: "0",
    DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS: "0",
    DJERD_HUB_CARRIER_CROSS_FINAL:
      process.env.DJERD_HUB_CARRIER_CROSS_FINAL ?? "1",
    DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD:
      process.env.DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD
      ?? DEFAULT_RENDERED_CARRIER_THRESHOLD,
    // Stress majorization post-pass — DEFAULT OFF (= 0). Initial
    // bundle-less binary tests on Captain looked promising (bbox -26%,
    // visualCross -0.8%) but the production VS Code path with
    // leafBundles=47 saw catastrophic regression (visualCross ×18.9,
    // bbox ×4.8, bundleEdgeIntersections 4→420). Stress majorization
    // ignores the cluster_graph + leaf-bundle scaffolding and
    // rearranges nodes purely by graph-theoretic distance, destroying
    // careful structural placement. Re-enable manually only after
    // confirming the run does not include bundle structure.
    DJERD_STRESS_POST_PASS_ITERS:
      process.env.DJERD_STRESS_POST_PASS_ITERS ?? "0",
    DJERD_STRESS_POST_PASS_EDGE_COST:
      process.env.DJERD_STRESS_POST_PASS_EDGE_COST ?? "140",
    DJERD_RENDERED_CARRIER_METRICS_FINAL:
      process.env.DJERD_RENDERED_CARRIER_METRICS_FINAL ?? "1",
    // Multi-start cluster_graph: run FMMM N times with different OGDF
    // seeds and keep the layout with the fewest straight-line edge
    // crossings. Unlike local search it explores *global* alternative
    // local optima — the only proven escape from the bundle-aware
    // visualCross plateau (memory: multistart-fmmm-success).
    //
    // RUNS=8, SEED_BASE=42 (2026-05-29). The seed lottery is high
    // variance: a baseline-style probe on the 1301 graph spread 4759–7022
    // straight-line crossings across seeds 43–53, and the best (seed=46)
    // sat *beyond* the first three seeds — RUNS=4 would have missed it.
    // RUNS=8 (seeds 43–49) doubles the sampling and captured that win.
    //
    // This used to cost ~N× the *whole* cluster_graph pipeline because
    // multistart re-ran on every positions-tsv round-trip (ML reroute,
    // bbox-target, polish) where its positions are immediately discarded
    // — ~44 wasted multistarts per Captain reload. main.cpp now gates the
    // multistart loop on an empty --positions-tsv, so it runs only on the
    // baseline layout that keeps its result. Net: RUNS=8 is *cheaper* than
    // the old RUNS=4 (52 vs 180 cluster_graph runs/reload) while sampling
    // 2× the seeds. Drop to RUNS=1 to disable for quick reloads.
    DJERD_MULTISTART_RUNS:
      process.env.DJERD_MULTISTART_RUNS ?? "8",
    DJERD_MULTISTART_SEED_BASE:
      process.env.DJERD_MULTISTART_SEED_BASE ?? "42",
    // Candidate C: multistart winner = min(straight-line crossings +
    // BBOX_WEIGHT · bbox-area-in-billions). Default 0 ⇒ pure-crossings
    // selection (byte-identical to before; raw crossings already tracks
    // the visualCrossings winner on the Captain corpus). Raise it to
    // trade a few crossings for a more compact seed — a Pareto knob, not
    // a free win, and routing-based metrics aren't available at selection
    // time so this is a cheap position-only proxy.
    DJERD_MULTISTART_BBOX_WEIGHT:
      process.env.DJERD_MULTISTART_BBOX_WEIGHT ?? "0",
    // Periphery reroute: for the worst high-crossing edges, route them around
    // the layout bbox periphery instead of straight through the dense centre,
    // keeping the candidate that minimises that edge's crossings (greedy
    // worst-first, global revert if total worsens). The FIRST inter-cluster
    // lever to actually cut edge crossings after placement (2-level community)
    // and routing-pass budgets both proved saturated (memory:
    // two-level-community-failed). Offline proxy (real-main-1301, no
    // inheritance, betweenCluster 29%): edgeCross 1275→1234 (-3.2%),
    // visualCross 2158→2128 (-1.4%), ~15 edges rerouted, edgeNode flat.
    // ON for production validation — production has 96% inter-cluster
    // crossings (more long edges) so the gain may differ; measure real
    // edgeCross on reload, then keep or set to 0. Visual cost: a handful of
    // long arcs around the diagram edge.
    // EXPERIMENT (2026-06-03): turned OFF to measure the cost of the giant
    // bbox-periphery arcs the user flagged as visually noisy. With cluster
    // orientation now aligning inter-cluster members, periphery may no longer
    // be needed; measure visualCross/bbox on reload, then keep off / restore /
    // tune TOPK down.
    DJERD_PERIPHERY_REROUTE:
      process.env.DJERD_PERIPHERY_REROUTE ?? "0",
    DJERD_PERIPHERY_TOPK:
      process.env.DJERD_PERIPHERY_TOPK ?? "400",
    // Cap the outward lane offset of peripheral arcs. Each rerouted edge was
    // pushed ~1% of the layout span further out than the last; left unbounded
    // the 65-200 reroutes inflated the ROUTE bbox (the visible extent) to
    // 6-29× the node bbox for ZERO crossing benefit. Cycling a small fixed
    // number of lanes separates parallel arcs just as well. Offline proxy
    // (real-main-1301, seed 46, orthogonal): vs unbounded, maxLanes=6 keeps the
    // crossing win (edgeCross 4381→4302, even slightly better) while cutting
    // routeBBox 285.5B→14.0B (≈1.4× the node bbox vs 29×) — a ~20× shrink of
    // the wasted peripheral whitespace. edgeNode +13 vs unbounded.
    DJERD_PERIPHERY_MAX_LANES:
      process.env.DJERD_PERIPHERY_MAX_LANES ?? "6",
    // Edge-node-aware periphery selection. Capped arcs hug the bbox, so they can
    // dodge centre crossings yet slice peripheral node boxes — that's what drove
    // the cap=6 edgeNode regression (178→310 on reload, amplified through the
    // bbox-target/polish quality gates). Score each candidate by crossings +
    // W·(node-box hits) and reroute only when the COMBINED cost drops (never
    // increasing this edge's crossings), so box-slicing arcs are rejected. Offline
    // proxy (real-main-1301, cap=6, seed 46): W=1 is a strict win over W=0 on all
    // three — edgeCross 4302→4237, visualCross 5374→5241, edgeNode 964→901, bbox
    // flat. W=2/3 cut edgeNode further but cost edgeCrossings. W=1 = a box-clip
    // weighted like a crossing (the metric's own weighting). W=0 = crossings-only.
    DJERD_PERIPHERY_EDGE_NODE_WEIGHT:
      process.env.DJERD_PERIPHERY_EDGE_NODE_WEIGHT ?? "1",
    // Carrier-aware multistart: score each seed by distinct CARRIER-PAIR
    // crossings (leaf bundles + cluster-pair "bus" carriers) instead of raw
    // straight-line crossings — i.e. the rendered carrier-grouped crossing the
    // user actually sees. Offline proxy (real-main-1301, RUNS=8): picks seed 46
    // vs raw's seed 48 — carrier-grouped 1508 vs 1559 (-3.3%), edgeCross
    // 1275 vs 1355 (-5.9%), bbox -10%, BUT edgeNode 819 vs 703 (+16%) and
    // visualCross ~tie. A Pareto knob (fewer edge-crossings + more compact, at
    // the cost of more edge-node collisions). ON for production validation —
    // production is 96% inter-cluster so the cluster-pair-bus grouping should
    // help more; measure real visualCross/edgeCross/edgeNode on reload, then
    // keep or set to 0.
    DJERD_MULTISTART_BUNDLE_AWARE:
      process.env.DJERD_MULTISTART_BUNDLE_AWARE ?? "1",
    // Multi-level Louvain coarsening. The single-level Louvain leaves the
    // cluster graph badly over-fragmented (394 raw communities on the captain
    // 3435-edge graph; production shows 289 louvainClusters with 68 singletons
    // + 68 spurious for only 640 core nodes), and ~88% of edge crossings are
    // INTER-cluster. Coarsening collapses each community into a super-node and
    // re-runs modularity (standard Louvain aggregation), folding back to a
    // flat partition: levels=2 → 140 communities (-64%), levels=3 → 104 (-74%,
    // converged). VALIDATED 2026-06-02: levels=2 REGRESSED everything on the
    // captain reload — visualCross 991→1672 (+69%), node bbox 2.31B→66.6B
    // (28×), edgeNode 221→518, and xBetweenClusters 672→786 (cluster count
    // dropped but inter-cluster crossings ROSE). Cause: coarsening produced 18
    // giant clusters (+68 singletons); the cluster_graph placement engine
    // (polar-anchor + super-graph FMMM + radial) assumes many small clusters
    // and blows up on giant ones. Crossings come from cluster *placement*, not
    // cluster *count* (matches two-level-community-failed). Reverted to
    // levels=1 (byte-identical). C++ knob kept for experimentation.
    DJERD_LOUVAIN_LEVELS:
      process.env.DJERD_LOUVAIN_LEVELS ?? "1",
    // Cluster orientation: rotate/reflect each cluster as a rigid body (about
    // its centroid) so the boundary members carrying inter-cluster edges face
    // their neighbours, scored by the ACTUAL straight-line crossings of the
    // cluster's incident edges (a length objective proved a poor proxy — it
    // shortened edges but left crossings flat). Unlike coarsening this keeps
    // cluster sizes intact (no placement blow-up). Multi-round greedy until it
    // converges; global revert if straight-line crossings don't improve. Auto-
    // skipped on --positions-tsv (ML) runs. Offline (captain 3435-edge graph):
    // straight-line crossings 13352 → 12698 (-4.9%, converges in 3 rounds). ON
    // for production validation; measure routed visualCross/edgeCross on reload.
    DJERD_CLUSTER_ORIENT:
      process.env.DJERD_CLUSTER_ORIENT ?? "1",
    // Outer loop: alternate rotate↔swap. After cluster-swap moves cluster
    // positions, re-running rotation finds better orientations at the new
    // positions (mutual refinement). Offline: outer 1 added 12577→12564,
    // outer 2 converged. Production may amplify (cluster-swap was -1% offline
    // → -82 routed). measure on reload.
    DJERD_ORIENT_OUTER:
      process.env.DJERD_ORIENT_OUTER ?? "1",
    // Outlier node-pull (runs inside the orientation pass, after rotate/swap):
    // pull abnormally-far LOW-degree nodes (deg<=4, >4% diag from their
    // neighbour centroid) toward that centroid, but ONLY when it strictly
    // reduces that node's incident crossings and the target isn't occupied
    // (overlap guard). Targets the user-flagged "abnormally far nodes". Offline
    // (captain 3435-edge graph): straight-line crossings 12698 → 10455 (-17.7%,
    // on top of orientation's -4.9% → combined 13352 → 10455 -21.7%), nodeBBox
    // unchanged (pulls inward). ON for production validation; measure routed
    // visualCross on reload.
    DJERD_NODE_PULL:
      process.env.DJERD_NODE_PULL ?? "1",
    // Cluster cohesion: pull nodes scattered from their OWN-cluster body back to
    // the same-cluster centroid (node-pull uses the NEIGHBOUR centroid, which
    // keeps e.g. AllocatedSeat by its Stakeholder hub instead of the seat family
    // it shares a cluster with). For user-flagged scattered related tables.
    // Trades straight-line cross for (hoped-for) routed cross; measure on reload.
    DJERD_CLUSTER_COHESION:
      process.env.DJERD_CLUSTER_COHESION ?? "0",
    // Cluster-swap (in orientation pass): swap centroids of crossing-heavy
    // cluster pairs with overlap guard. Offline -1% alone; trying ON in the
    // full pipeline to squeeze out more inter-cluster crossings.
    DJERD_CLUSTER_SWAP:
      process.env.DJERD_CLUSTER_SWAP ?? "1",
    ...overrides,
  };
}

function ogdfOptimizedRerouteEnv(): Record<string, string | undefined> {
  return ogdfRenderedCarrierEnv({
    DJERD_NO_KNOT_MIN:
      process.env.DJERD_NO_KNOT_MIN ?? "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_FINAL_ROUTE_SYNC_GAP ?? "100",
    DJERD_NODE_VISUAL_MARGIN:
      process.env.DJERD_NODE_VISUAL_MARGIN ?? "8",
    DJERD_LEAF_BUNDLE_VISUAL_MARGIN:
      process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32",
    DJERD_RIGID_COMPACT_BBOX_FINAL:
      process.env.DJERD_RIGID_COMPACT_BBOX_FINAL ?? "1",
    DJERD_RIGID_COMPACT_BBOX_TARGET_B:
      process.env.DJERD_RIGID_COMPACT_BBOX_TARGET_B ?? "6.0",
    DJERD_RIGID_COMPACT_BBOX_MIN_SCALE:
      process.env.DJERD_RIGID_COMPACT_BBOX_MIN_SCALE ?? "0.48",
    DJERD_RIGID_COMPACT_BBOX_SPACING_SLACK:
      process.env.DJERD_RIGID_COMPACT_BBOX_SPACING_SLACK ?? "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL:
      process.env.DJERD_RIGID_ATTACH_ISOLATED_FINAL ?? "1",
    DJERD_RIGID_ATTACH_ISOLATED_MAX_RADIUS:
      process.env.DJERD_RIGID_ATTACH_ISOLATED_MAX_RADIUS ?? "3600",
    DJERD_RIGID_ATTACH_ISOLATED_RING_STEP:
      process.env.DJERD_RIGID_ATTACH_ISOLATED_RING_STEP ?? "260",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_FINAL ?? "1",
    DJERD_ISOLATED_BBOX_COMPACT_GAP_X:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_GAP_X ?? "180",
    DJERD_ISOLATED_BBOX_COMPACT_GAP_Y:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_GAP_Y ?? "36",
    DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y ?? "140",
    DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN ?? "0.01",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_FINAL ?? "1",
    DJERD_SIDECAR_BBOX_COMPACT_GAP_X:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_GAP_X ?? "160",
    DJERD_SIDECAR_BBOX_COMPACT_GAP_Y:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_GAP_Y ?? "120",
    DJERD_SIDECAR_BBOX_COMPACT_MIN_GAIN:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_MIN_GAIN ?? "0.01",
    DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT ?? "2.2",
    DJERD_BBOX_AXIS_SCALE_FINAL:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL ?? "1",
    DJERD_BBOX_Y_SCALE_FINAL_SCALES:
      process.env.DJERD_BBOX_Y_SCALE_FINAL_SCALES ?? "0.98,0.95",
    DJERD_BBOX_AXIS_SCALE_FINAL_MIN_GAIN:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL_MIN_GAIN ?? "0.015",
    DJERD_BBOX_AXIS_SCALE_FINAL_VISUAL_SLACK:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL_VISUAL_SLACK ?? "0",
    DJERD_BBOX_AXIS_SCALE_FINAL_MAX_ASPECT:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL_MAX_ASPECT ?? "2.1",
    DJERD_DENSITY_BALANCE_FINAL:
      process.env.DJERD_DENSITY_BALANCE_FINAL ?? "0",
    DJERD_DENSITY_BALANCE_CELL:
      process.env.DJERD_DENSITY_BALANCE_CELL ?? "1600",
    DJERD_DENSITY_BALANCE_TOP:
      process.env.DJERD_DENSITY_BALANCE_TOP ?? "18",
    DJERD_DENSITY_BALANCE_SCALE:
      process.env.DJERD_DENSITY_BALANCE_SCALE ?? "1.055",
    DJERD_DENSITY_BALANCE_MAX_SCALE:
      process.env.DJERD_DENSITY_BALANCE_MAX_SCALE ?? "1.11",
    DJERD_DENSITY_BALANCE_MIN_FILL:
      process.env.DJERD_DENSITY_BALANCE_MIN_FILL ?? "0.66",
    DJERD_DENSITY_BALANCE_VISUAL_SLACK:
      process.env.DJERD_DENSITY_BALANCE_VISUAL_SLACK ?? "40",
    DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK:
      process.env.DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK ?? "80",
    DJERD_DENSITY_BALANCE_BBOX_LIMIT:
      process.env.DJERD_DENSITY_BALANCE_BBOX_LIMIT ?? "1.025",
    DJERD_DENSITY_PACK_FINAL:
      process.env.DJERD_DENSITY_PACK_FINAL ?? "1",
    DJERD_DENSITY_PACK_SCALES:
      process.env.DJERD_DENSITY_PACK_SCALES ?? "0.94,0.90,0.86,0.82,0.78,0.72",
    DJERD_DENSITY_PACK_EXPAND_SCALE:
      process.env.DJERD_DENSITY_PACK_EXPAND_SCALE ?? "1.08",
    DJERD_DENSITY_PACK_TOP:
      process.env.DJERD_DENSITY_PACK_TOP ?? "0",
    DJERD_DENSITY_PACK_EMPTY_BAND_KEEP:
      process.env.DJERD_DENSITY_PACK_EMPTY_BAND_KEEP ?? "720",
    DJERD_DENSITY_PACK_VISUAL_SLACK:
      process.env.DJERD_DENSITY_PACK_VISUAL_SLACK ?? "180",
    DJERD_DENSITY_PACK_EDGE_NODE_SLACK:
      process.env.DJERD_DENSITY_PACK_EDGE_NODE_SLACK ?? "160",
    DJERD_DENSITY_PACK_SPACING_SLACK:
      process.env.DJERD_DENSITY_PACK_SPACING_SLACK ?? "40",
    DJERD_DENSITY_PACK_P90_SLACK:
      process.env.DJERD_DENSITY_PACK_P90_SLACK ?? "1",
    DJERD_DENSITY_PACK_MAX_SLACK:
      process.env.DJERD_DENSITY_PACK_MAX_SLACK ?? "2",
    DJERD_DENSITY_PACK_CLEANUP:
      process.env.DJERD_DENSITY_PACK_CLEANUP ?? "0",
    DJERD_NODE_SPACING_CLEAR_FINAL:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL ?? "1",
    DJERD_NODE_SPACING_CLEAR_FINAL_PASSES:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES ?? "6",
    DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT ?? "220",
    DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA ?? "8",
    DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK ?? "80",
    DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK ?? "80",
    DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT ?? "1.025",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT ?? "3600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES ?? "2",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT ?? "600",
    DJERD_RIGID_NODE_EDGE_RELIEF_FINAL:
      process.env.DJERD_RIGID_NODE_EDGE_RELIEF_FINAL ?? "1",
    DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_PASSES ?? "6",
    DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT ?? "80",
    DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_STRENGTH ?? "0.8",
    DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_BUNDLE_NODE_WEIGHT:
      process.env.DJERD_RIGID_NODE_EDGE_RELIEF_FINAL_BUNDLE_NODE_WEIGHT ?? "0",
  });
}

function ogdfOptimizedPoststackEnv(
  overrides: Record<string, string | undefined> = {},
): Record<string, string | undefined> {
  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN:
      process.env.DJERD_NO_KNOT_MIN ?? "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_FINAL_ROUTE_SYNC_GAP ?? "100",
    DJERD_NODE_VISUAL_MARGIN:
      process.env.DJERD_NODE_VISUAL_MARGIN ?? "8",
    DJERD_LEAF_BUNDLE_VISUAL_MARGIN:
      process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32",
    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL:
      process.env.DJERD_ATTACH_ISOLATED_BY_NAME_FINAL ?? "1",
    DJERD_ATTACH_ISOLATED_MIN_SCORE:
      process.env.DJERD_ATTACH_ISOLATED_MIN_SCORE ?? "35",
    DJERD_ATTACH_ISOLATED_ROUTE_CHECK:
      process.env.DJERD_ATTACH_ISOLATED_ROUTE_CHECK ?? "1",
    DJERD_ATTACH_ISOLATED_BBOX_WEIGHT:
      process.env.DJERD_ATTACH_ISOLATED_BBOX_WEIGHT ?? "20",
    DJERD_ATTACH_ISOLATED_RADIUS_WEIGHT:
      process.env.DJERD_ATTACH_ISOLATED_RADIUS_WEIGHT ?? "1",
    DJERD_ATTACH_ISOLATED_OUTWARD_WEIGHT:
      process.env.DJERD_ATTACH_ISOLATED_OUTWARD_WEIGHT ?? "35",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_FINAL ?? "1",
    DJERD_ISOLATED_BBOX_COMPACT_GAP_X:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_GAP_X ?? "180",
    DJERD_ISOLATED_BBOX_COMPACT_GAP_Y:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_GAP_Y ?? "36",
    DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y ?? "140",
    DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN:
      process.env.DJERD_ISOLATED_BBOX_COMPACT_MIN_GAIN ?? "0.01",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_FINAL ?? "1",
    DJERD_SIDECAR_BBOX_COMPACT_GAP_X:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_GAP_X ?? "160",
    DJERD_SIDECAR_BBOX_COMPACT_GAP_Y:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_GAP_Y ?? "120",
    DJERD_SIDECAR_BBOX_COMPACT_MIN_GAIN:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_MIN_GAIN ?? "0.01",
    DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT:
      process.env.DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT ?? "2.2",
    DJERD_BBOX_AXIS_SCALE_FINAL:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL ?? "1",
    DJERD_BBOX_Y_SCALE_FINAL_SCALES:
      process.env.DJERD_BBOX_Y_SCALE_FINAL_SCALES ?? "0.98,0.95",
    DJERD_BBOX_AXIS_SCALE_FINAL_MIN_GAIN:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL_MIN_GAIN ?? "0.015",
    DJERD_BBOX_AXIS_SCALE_FINAL_VISUAL_SLACK:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL_VISUAL_SLACK ?? "0",
    DJERD_BBOX_AXIS_SCALE_FINAL_MAX_ASPECT:
      process.env.DJERD_BBOX_AXIS_SCALE_FINAL_MAX_ASPECT ?? "2.1",
    DJERD_DENSITY_BALANCE_FINAL:
      process.env.DJERD_DENSITY_BALANCE_FINAL ?? "0",
    DJERD_DENSITY_BALANCE_CELL:
      process.env.DJERD_DENSITY_BALANCE_CELL ?? "1600",
    DJERD_DENSITY_BALANCE_TOP:
      process.env.DJERD_DENSITY_BALANCE_TOP ?? "18",
    DJERD_DENSITY_BALANCE_SCALE:
      process.env.DJERD_DENSITY_BALANCE_SCALE ?? "1.055",
    DJERD_DENSITY_BALANCE_MAX_SCALE:
      process.env.DJERD_DENSITY_BALANCE_MAX_SCALE ?? "1.11",
    DJERD_DENSITY_BALANCE_MIN_FILL:
      process.env.DJERD_DENSITY_BALANCE_MIN_FILL ?? "0.66",
    DJERD_DENSITY_BALANCE_VISUAL_SLACK:
      process.env.DJERD_DENSITY_BALANCE_VISUAL_SLACK ?? "40",
    DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK:
      process.env.DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK ?? "80",
    DJERD_DENSITY_BALANCE_BBOX_LIMIT:
      process.env.DJERD_DENSITY_BALANCE_BBOX_LIMIT ?? "1.025",
    DJERD_DENSITY_PACK_FINAL:
      process.env.DJERD_DENSITY_PACK_FINAL ?? "1",
    DJERD_DENSITY_PACK_SCALES:
      process.env.DJERD_DENSITY_PACK_SCALES ?? "0.94,0.90,0.86,0.82,0.78,0.72",
    DJERD_DENSITY_PACK_EXPAND_SCALE:
      process.env.DJERD_DENSITY_PACK_EXPAND_SCALE ?? "1.08",
    DJERD_DENSITY_PACK_TOP:
      process.env.DJERD_DENSITY_PACK_TOP ?? "0",
    DJERD_DENSITY_PACK_EMPTY_BAND_KEEP:
      process.env.DJERD_DENSITY_PACK_EMPTY_BAND_KEEP ?? "720",
    DJERD_DENSITY_PACK_VISUAL_SLACK:
      process.env.DJERD_DENSITY_PACK_VISUAL_SLACK ?? "180",
    DJERD_DENSITY_PACK_EDGE_NODE_SLACK:
      process.env.DJERD_DENSITY_PACK_EDGE_NODE_SLACK ?? "160",
    DJERD_DENSITY_PACK_SPACING_SLACK:
      process.env.DJERD_DENSITY_PACK_SPACING_SLACK ?? "40",
    DJERD_DENSITY_PACK_P90_SLACK:
      process.env.DJERD_DENSITY_PACK_P90_SLACK ?? "1",
    DJERD_DENSITY_PACK_MAX_SLACK:
      process.env.DJERD_DENSITY_PACK_MAX_SLACK ?? "2",
    DJERD_DENSITY_PACK_CLEANUP:
      process.env.DJERD_DENSITY_PACK_CLEANUP ?? "0",
    DJERD_NODE_SPACING_CLEAR_FINAL:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL ?? "1",
    DJERD_NODE_SPACING_CLEAR_FINAL_PASSES:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES ?? "6",
    DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT ?? "220",
    DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA ?? "8",
    DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK ?? "80",
    DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK ?? "80",
    DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT ?? "1.025",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT ?? "3600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK ?? "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_NODE_OVERLAP_SLACK:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_NODE_OVERLAP_SLACK ?? "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK ?? "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT ?? "1.04",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES ?? "2",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT:
      process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT ?? "600",
    DJERD_ISOLATED_STASH:
      process.env.DJERD_ISOLATED_STASH ?? "0",
    DJERD_FACE_RASTER: process.env.DJERD_FACE_RASTER ?? "0",
    DJERD_HOT_REGION_SA: process.env.DJERD_HOT_REGION_SA ?? "0",
    DJERD_STUCK_LEAF_2D: process.env.DJERD_STUCK_LEAF_2D ?? "0",
    DJERD_XINGS_DETOUR: process.env.DJERD_XINGS_DETOUR ?? "0",
    DJERD_NO_PD_KNOT: process.env.DJERD_NO_PD_KNOT ?? "1",
    DJERD_VISUAL_KNOT: process.env.DJERD_VISUAL_KNOT ?? "0",
    DJERD_NODE_EDGE_RELIEF_FINAL:
      process.env.DJERD_NODE_EDGE_RELIEF_FINAL ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_NODE_EDGE_RELIEF_FINAL_PASSES ?? "4",
    DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT ?? "80",
    DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH ?? "0.8",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS:
      process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS ?? "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL:
      process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL ?? "0",
    ...overrides,
  });
}

function ogdfOptimizedBboxTargetEnv(
  overrides: Record<string, string | undefined> = {},
): Record<string, string | undefined> {
  return ogdfOptimizedPoststackEnv({
    DJERD_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_PASSES
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_PASSES
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_MAX_SHIFT
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_STRENGTH
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH
      ?? "0.95",
    DJERD_LEAF_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LEAF_PASSES
      ?? process.env.DJERD_LEAF_PASSES
      ?? "1",
    DJERD_LEAF_PASSES_2:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LEAF_PASSES_2
      ?? process.env.DJERD_LEAF_PASSES_2
      ?? "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_EDGE_NODE_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "24",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_VISUAL_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK
      ?? "120",
    DJERD_NODE_OVERLAP_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL
      ?? "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_PASSES
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES
      ?? "8",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT
      ?? "260",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_EXTRA
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA
      ?? "12",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK
      ?? "80",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "80",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.03",
    DJERD_DENSITY_BALANCE_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE
      ?? process.env.DJERD_DENSITY_BALANCE_FINAL
      ?? "0",
    DJERD_DENSITY_BALANCE_TOP:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_TOP
      ?? process.env.DJERD_DENSITY_BALANCE_TOP
      ?? "18",
    DJERD_DENSITY_BALANCE_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_VISUAL_SLACK
      ?? process.env.DJERD_DENSITY_BALANCE_VISUAL_SLACK
      ?? "40",
    DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_EDGE_NODE_SLACK
      ?? process.env.DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK
      ?? "80",
    DJERD_DENSITY_BALANCE_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_BBOX_LIMIT
      ?? process.env.DJERD_DENSITY_BALANCE_BBOX_LIMIT
      ?? "1.025",
    DJERD_DENSITY_PACK_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_PACK
      ?? "0",
    DJERD_NODE_SPACING_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL
      ?? "1",
    DJERD_NODE_SPACING_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_PASSES
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES
      ?? "6",
    DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_EXTRA
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA
      ?? "8",
    DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK
      ?? "80",
    DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "80",
    DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.025",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_PASSES
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES
      ?? "4",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_VISUAL_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK
      ?? "80",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_TOP
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP
      ?? "6",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_MAX_CANDIDATES
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES
      ?? "24",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_FULL_SHORTLIST
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST
      ?? "5",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_EDGE_NODE_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK
      ?? "80",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_BBOX_LIMIT
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT
      ?? "1.03",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL
      ?? "1",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_PASSES
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_PASSES
      ?? "1",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_TOP:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_TOP
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_TOP
      ?? "8",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_MAX_CANDIDATES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_MAX_CANDIDATES
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_MAX_CANDIDATES
      ?? "32",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_SHORTLIST:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_SHORTLIST
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_SHORTLIST
      ?? "8",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_FULL_SCAN:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_FULL_SCAN
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_FULL_SCAN
      ?? "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_MAX_MOVE:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_MAX_MOVE
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_MAX_MOVE
      ?? "6200",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_TOTAL_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_TOTAL_LIMIT
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_TOTAL_LIMIT
      ?? "768",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_BBOX_LIMIT
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_BBOX_LIMIT
      ?? "1.03",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_MIN_GAIN
      ?? process.env.DJERD_BUNDLE_BOX_RELOCATE_FINAL_MIN_GAIN
      ?? "0.25",
    ...overrides,
  });
}

function ogdfOptimizedBboxTargetEnvForVariant(
  variant: BboxTargetVariant,
): Record<string, string | undefined> {
  if (variant === "holistic") {
    return ogdfOptimizedBboxTargetEnv();
  }

  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN:
      process.env.DJERD_NO_KNOT_MIN ?? "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_FINAL_ROUTE_SYNC_GAP ?? "100",
    DJERD_NODE_VISUAL_MARGIN:
      process.env.DJERD_NODE_VISUAL_MARGIN ?? "8",
    DJERD_LEAF_BUNDLE_VISUAL_MARGIN:
      process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32",

    // Local bbox target is a conservative candidate: preserve the accepted
    // layout shape, then run collision cleanup. The holistic variant keeps
    // the wider post-stack available when this candidate cannot pass gates.
    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_ATTACH_ISOLATED ?? "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_ISOLATED_COMPACT ?? "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_SIDECAR_COMPACT ?? "0",
    DJERD_BBOX_AXIS_SCALE_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_AXIS_SCALE ?? "0",
    DJERD_DENSITY_BALANCE_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_DENSITY_BALANCE ?? "0",
    DJERD_DENSITY_PACK_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_DENSITY_PACK ?? "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_BUNDLE_RELOCATE ?? "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_RIGID_COMPACT ?? "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_RIGID_ATTACH_ISOLATED ?? "0",
    DJERD_LEAF_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_LEAF_PASSES ?? "0",
    DJERD_LEAF_PASSES_2:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_LEAF_PASSES_2 ?? "0",
    DJERD_ISOLATED_STASH:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_ISOLATED_STASH ?? "0",
    DJERD_FACE_RASTER:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_FACE_RASTER ?? "0",
    DJERD_FACE_UNTANGLE:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_FACE_UNTANGLE ?? "0",
    DJERD_HOT_REGION_SA:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_HOT_REGION_SA ?? "0",
    DJERD_STUCK_LEAF_2D:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_STUCK_LEAF_2D ?? "0",
    DJERD_XINGS_DETOUR:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_XINGS_DETOUR ?? "0",
    DJERD_XINGS_DETOUR_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_XINGS_DETOUR_FINAL ?? "0",
    DJERD_NO_PD_KNOT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NO_PD_KNOT ?? "1",
    DJERD_VISUAL_KNOT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_VISUAL_KNOT ?? "0",

    DJERD_NODE_EDGE_RELIEF_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_RELIEF
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_RELIEF_PASSES
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_PASSES
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_PASSES
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_RELIEF_MAX_SHIFT
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_MAX_SHIFT
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT
      ?? "140",
    DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_RELIEF_STRENGTH
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_STRENGTH
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH
      ?? "0.80",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_ENDPOINTS
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS
      ?? "0",

    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_BUNDLE_CLEAR
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_BUNDLE_EDGE_NODE_SLACK
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_EDGE_NODE_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "24",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_BUNDLE_VISUAL_SLACK
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_VISUAL_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK
      ?? "80",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_BUNDLE_BBOX_LIMIT
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.04",

    DJERD_NODE_OVERLAP_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL
      ?? "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR_PASSES
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_PASSES
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES
      ?? "6",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR_EXTRA
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_EXTRA
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA
      ?? "10",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK
      ?? "40",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "40",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_OVERLAP_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.03",

    DJERD_NODE_SPACING_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL
      ?? "1",
    DJERD_NODE_SPACING_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR_PASSES
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_PASSES
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES
      ?? "4",
    DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT
      ?? "160",
    DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR_EXTRA
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_EXTRA
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA
      ?? "8",
    DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK
      ?? "60",
    DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "60",
    DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_LOCAL_NODE_SPACING_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.025",
  });
}

function ogdfOptimizedEdgeNodePolishCommonEnv(): Record<string, string | undefined> {
  return {
    DJERD_NODE_EDGE_RELIEF_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_RELIEF
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_RELIEF_PASSES
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_PASSES
      ?? "3",
    DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_RELIEF_MAX_SHIFT
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_RELIEF_STRENGTH
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH
      ?? "0.95",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ENDPOINTS
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_TOP:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ENDPOINT_TOP
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_TOP
      ?? "40",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_STEPS:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ENDPOINT_STEPS
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_STEPS
      ?? "80,160,300,600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_EDGE_NODE_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_VISUAL_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK
      ?? "120",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_BBOX_LIMIT
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.08",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_PASSES
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES
      ?? "4",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_TOP
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP
      ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_MAX_CANDIDATES
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES
      ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_FULL_SHORTLIST
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST
      ?? "6",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK
      ?? "80",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK
      ?? "80",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT
      ?? "1.06",
  };
}

function ogdfOptimizedEdgeNodePolishCheapEnv(): Record<string, string | undefined> {
  // The cheap variant is the smallest, fastest polish: only narrow edge-node
  // relief without endpoint moves, no bundle re-locate, no overlap clear. Its
  // job is to catch obvious edge-node wins that survive aggressive bbox
  // compression without disturbing visual quality. If even this rejects, the
  // broader local/holistic variants are unlikely to do better and should be
  // skipped by the caller.
  const common = ogdfOptimizedEdgeNodePolishCommonEnv();
  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN: process.env.DJERD_NO_KNOT_MIN ?? "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_FINAL_ROUTE_SYNC_GAP ?? "100",
    DJERD_NODE_VISUAL_MARGIN:
      process.env.DJERD_NODE_VISUAL_MARGIN ?? "8",
    DJERD_LEAF_BUNDLE_VISUAL_MARGIN:
      process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32",

    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL: "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL: "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL: "0",
    DJERD_BBOX_AXIS_SCALE_FINAL: "0",
    DJERD_DENSITY_BALANCE_FINAL: "0",
    DJERD_DENSITY_PACK_FINAL: "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL: "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL: "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL: "0",
    DJERD_LEAF_PASSES: "0",
    DJERD_LEAF_PASSES_2: "0",
    DJERD_ISOLATED_STASH: "0",
    DJERD_FACE_RASTER: "0",
    DJERD_FACE_UNTANGLE: "0",
    DJERD_HOT_REGION_SA: "0",
    DJERD_STUCK_LEAF_2D: "0",
    DJERD_XINGS_DETOUR: "0",
    DJERD_XINGS_DETOUR_FINAL: "0",
    DJERD_NO_PD_KNOT: "1",
    DJERD_VISUAL_KNOT: "0",
    // Keep leaf-bundle clearance ON: relief can push nodes into rendered
    // bundle boxes, and the bundle clear pass is cheap and metric-gated so it
    // protects bundleNode without touching anything else.
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL: "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL: "0",
    DJERD_NODE_SPACING_CLEAR_FINAL: "0",

    ...common,
    DJERD_NODE_EDGE_RELIEF_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_CHEAP_RELIEF
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_CHEAP_RELIEF_PASSES
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_CHEAP_RELIEF_MAX_SHIFT
      ?? "80",
    DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_CHEAP_RELIEF_STRENGTH
      ?? "0.6",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_CHEAP_ENDPOINTS
      ?? "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL: "0",
  });
}

function ogdfOptimizedEdgeNodePolishEnv(
  variant: EdgeNodePolishVariant,
): Record<string, string | undefined> {
  const common = ogdfOptimizedEdgeNodePolishCommonEnv();
  if (variant === "holistic") {
    return ogdfOptimizedBboxTargetEnv(common);
  }
  if (variant === "cheap") {
    return ogdfOptimizedEdgeNodePolishCheapEnv();
  }

  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN:
      process.env.DJERD_NO_KNOT_MIN ?? "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_FINAL_ROUTE_SYNC_GAP ?? "100",
    DJERD_NODE_VISUAL_MARGIN:
      process.env.DJERD_NODE_VISUAL_MARGIN ?? "8",
    DJERD_LEAF_BUNDLE_VISUAL_MARGIN:
      process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32",

    // Local polish is one candidate, not a global ban. Holistic polish still
    // evaluates the wider post-stack when enabled in the variant list.
    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_ATTACH_ISOLATED ?? "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_ISOLATED_COMPACT ?? "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_SIDECAR_COMPACT ?? "0",
    DJERD_BBOX_AXIS_SCALE_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_AXIS_SCALE ?? "0",
    DJERD_DENSITY_BALANCE_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_DENSITY_BALANCE ?? "0",
    DJERD_DENSITY_PACK_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_DENSITY_PACK ?? "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_BUNDLE_RELOCATE ?? "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_RIGID_COMPACT ?? "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_RIGID_ATTACH_ISOLATED ?? "0",
    DJERD_LEAF_PASSES:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_LEAF_PASSES ?? "0",
    DJERD_LEAF_PASSES_2:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_LEAF_PASSES_2 ?? "0",
    DJERD_ISOLATED_STASH:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_ISOLATED_STASH ?? "0",
    DJERD_FACE_RASTER:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_FACE_RASTER ?? "0",
    DJERD_FACE_UNTANGLE:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_FACE_UNTANGLE ?? "0",
    DJERD_HOT_REGION_SA:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_HOT_REGION_SA ?? "0",
    DJERD_STUCK_LEAF_2D:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_STUCK_LEAF_2D ?? "0",
    DJERD_XINGS_DETOUR:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_XINGS_DETOUR ?? "0",
    DJERD_XINGS_DETOUR_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_XINGS_DETOUR_FINAL ?? "0",
    DJERD_NO_PD_KNOT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NO_PD_KNOT ?? "1",
    DJERD_VISUAL_KNOT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_VISUAL_KNOT ?? "0",

    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_BUNDLE_CLEAR
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL
      ?? "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL
      ?? "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR_PASSES
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES
      ?? "6",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR_EXTRA
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA
      ?? "10",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK
      ?? "40",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "40",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_OVERLAP_CLEAR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.04",
    DJERD_NODE_SPACING_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_LOCAL_NODE_SPACING_CLEAR ?? "0",

    ...common,
  });
}

function ogdfOptimizedVisualCrossPolishEnv(
  variant: VisualCrossPolishVariant,
): Record<string, string | undefined> {
  const runKnot = variant === "knot" || variant === "knot-clear";
  const runDiagonalRetouch =
    variant === "route-retouch"
    && readBoolEnv("DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ALLOW_RETOUCH_VARIANT", false);
  const runEdgeDetour = variant.startsWith("route") && !runDiagonalRetouch;
  const runBundleClear =
    visualCrossPolishUsesBundleClear(variant)
    && readBoolEnv("DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR", true);
  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN:
      process.env.DJERD_NO_KNOT_MIN ?? "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_FINAL_ROUTE_SYNC_GAP ?? "100",
    DJERD_NODE_VISUAL_MARGIN:
      process.env.DJERD_NODE_VISUAL_MARGIN ?? "8",
    DJERD_LEAF_BUNDLE_VISUAL_MARGIN:
      process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32",

    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL: "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL: "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL: "0",
    DJERD_BBOX_AXIS_SCALE_FINAL: "0",
    DJERD_DENSITY_BALANCE_FINAL: "0",
    DJERD_DENSITY_PACK_FINAL: "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL: "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL: "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL: "0",
    DJERD_NODE_EDGE_RELIEF_FINAL: "0",
    DJERD_NODE_OVERLAP_CLEAR_FINAL: "0",
    DJERD_NODE_SPACING_CLEAR_FINAL: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL: runBundleClear ? "1" : "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PASSES
      ?? "4",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_MAX_SHIFT
      ?? "1600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_EXTRA
      ?? "24",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_VISUAL_SLACK
      ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_NODE_OVERLAP_SLACK: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_EDGE_NODE_SLACK
      ?? "96",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_BBOX_LIMIT
      ?? "1.04",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PUSH_NODES
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PUSH_NODE_PASSES
      ?? "2",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PUSH_NODE_MAX_SHIFT
      ?? "600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL:
      runBundleClear ? "1" : "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_PASSES
      ?? "3",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_VISUAL_SLACK
      ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_EDGE_NODE_SLACK
      ?? "96",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_TOP
      ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_MAX_CANDIDATES
      ?? "48",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_FULL_SHORTLIST
      ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_BBOX_LIMIT
      ?? "1.04",
    DJERD_NO_BUNDLE_CLEAR: runBundleClear ? "0" : "1",
    DJERD_BUNDLE_SHIFT: "0",
    DJERD_HUB_PUSH: "0",
    DJERD_NO_C3: "1",
    DJERD_LEAF_PASSES: "0",
    DJERD_LEAF_PASSES_2: "0",
    DJERD_NO_LEAF_UNTANGLE: "1",
    DJERD_ISOLATED_STASH: "0",
    DJERD_FACE_RASTER: "0",
    DJERD_FACE_UNTANGLE: "0",
    DJERD_HOT_REGION_SA: "0",
    DJERD_STUCK_LEAF_2D: "0",

    DJERD_XINGS_DETOUR:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR ?? "1",
    DJERD_XINGS_DETOUR_PHASE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_PHASE
      ?? "post",
    DJERD_XINGS_DETOUR_ITERS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_ITERS
      ?? "3",
    DJERD_XINGS_DETOUR_MAX_SEGS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_MAX_SEGS
      ?? "2",
    DJERD_XINGS_DETOUR_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_TOPK
      ?? "12",
    DJERD_XINGS_DETOUR_MAX_OFFSET:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_MAX_OFFSET
      ?? "480",
    DJERD_XINGS_DETOUR_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_FINAL
      ?? "0",
    DJERD_EDGE_DETOUR_FINAL:
      runEdgeDetour
        ? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR ?? "1"
        : "0",
    DJERD_EDGE_DETOUR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_PASSES
      ?? "1",
    DJERD_EDGE_DETOUR_FINAL_CROSS_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_CROSS_WEIGHT
      ?? "1.0",
    DJERD_EDGE_DETOUR_FINAL_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_LENGTH_WEIGHT
      ?? "0",
    DJERD_EDGE_DETOUR_FINAL_CLEARANCE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_CLEARANCE
      ?? "28",
    DJERD_L_BEND_REROUTE:
      runEdgeDetour
        ? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_REROUTE
          ?? "0"
        : "0",
    DJERD_L_BEND_REROUTE_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_TOPK
      ?? "160",
    DJERD_L_BEND_REROUTE_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_MIN_GAIN
      ?? "1",
    DJERD_L_BEND_REROUTE_EDGE_NODE_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_EDGE_NODE_WEIGHT
      ?? "1",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_SEGMENT_OVERLAP_WEIGHT
      ?? "1",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_L_BEND_REROUTE_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_LENGTH_WEIGHT
      ?? "0",
    DJERD_DIAGONAL_RETOUCH:
      runDiagonalRetouch
        ? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH
          ?? "1"
        : "0",
    DJERD_NODE_PAIR_RETOUCH:
      runDiagonalRetouch
        ? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH
          ?? process.env.DJERD_NODE_PAIR_RETOUCH
          ?? "1"
        : "0",
    DJERD_NODE_PAIR_RETOUCH_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_TOPK
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_TOPK
      ?? "96",
    DJERD_NODE_PAIR_RETOUCH_ROUNDS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_ROUNDS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_ROUNDS
      ?? "3",
    DJERD_NODE_PAIR_RETOUCH_STEPS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_STEPS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_STEPS
      ?? "3",
    DJERD_NODE_PAIR_RETOUCH_MIN_SPAN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_MIN_SPAN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MIN_SPAN
      ?? "1200",
    DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_STEP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_STEP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_STEP
      ?? "320",
    DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_MAX_SHIFT
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT
      ?? "1600",
    DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_MAX_INCIDENT
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT
      ?? "28",
    DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_NODE_MARGIN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_ALLOW_NODE_OVERLAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_ALLOW_NODE_OVERLAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_ALLOW_NODE_OVERLAP
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_LEAF_SNAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_LEAF_SNAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_SNAP
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_LEAF_ONLY
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_LEAF_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_LEAF_GAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_GAP
      ?? "24",
    DJERD_NODE_PAIR_RETOUCH_RAW_ACCEPT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_RAW_ACCEPT
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_RAW_ACCEPT
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_COMPACT_RELOCATE
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_SLOT_RINGS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS
      ?? "2",
    DJERD_NODE_PAIR_RETOUCH_PAIR_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_PAIR_RETOUCH_PAIR_GAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_PAIR_GAP
      ?? "24",
    DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_ROUTE_SEGMENTS
      ?? process.env.DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS
      ?? "0",
    DJERD_DIAGONAL_RETOUCH_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_TOPK
      ?? "160",
    DJERD_DIAGONAL_RETOUCH_SEGMENTS_PER_EDGE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_SEGMENTS_PER_EDGE
      ?? "3",
    DJERD_DIAGONAL_RETOUCH_ROUNDS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_ROUNDS
      ?? "2",
    DJERD_DIAGONAL_RETOUCH_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_MIN_GAIN
      ?? "1",
    DJERD_DIAGONAL_RETOUCH_MIN_SPAN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_MIN_SPAN
      ?? "80",
    DJERD_DIAGONAL_RETOUCH_EDGE_NODE_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_EDGE_NODE_WEIGHT
      ?? "1",
    DJERD_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_WEIGHT
      ?? "1",
    DJERD_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_DIAGONAL_RETOUCH_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_LENGTH_WEIGHT
      ?? "0",
    DJERD_DIAGONAL_RETOUCH_ALLOW_NODE_HIT_DEBT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_ALLOW_NODE_HIT_DEBT
      ?? process.env.DJERD_DIAGONAL_RETOUCH_ALLOW_NODE_HIT_DEBT
      ?? "0",
    DJERD_DIAGONAL_RETOUCH_BUNDLE_OBSTACLES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_BUNDLE_OBSTACLES
      ?? process.env.DJERD_DIAGONAL_RETOUCH_BUNDLE_OBSTACLES
      ?? "1",
    DJERD_DIAGONAL_RETOUCH_TWO_BEND:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_TWO_BEND
      ?? process.env.DJERD_DIAGONAL_RETOUCH_TWO_BEND
      ?? "1",

    DJERD_NO_PD_KNOT:
      runKnot
        ? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NO_PD_KNOT ?? "1"
        : "1",
    DJERD_VISUAL_KNOT:
      runKnot
        ? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_VISUAL_KNOT ?? "1"
        : "0",
    DJERD_VISUAL_KNOT_ITERS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_VISUAL_KNOT_ITERS
      ?? "2",

    ...ogdfOptimizedVisualCrossPolishVariantEnv(variant),
  });
}

function ogdfOptimizedVisualCrossPolishSpacingRepairEnv(): Record<string, string | undefined> {
  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN: "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_ROUTE_SYNC_GAP
      ?? "1000000000",
    DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH: "0",

    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL: "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL: "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL: "0",
    DJERD_BBOX_AXIS_SCALE_FINAL: "0",
    DJERD_DENSITY_BALANCE_FINAL: "0",
    DJERD_DENSITY_PACK_FINAL: "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL: "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL: "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL: "0",
    DJERD_NODE_EDGE_RELIEF_FINAL: "0",
    DJERD_NODE_OVERLAP_CLEAR_FINAL: "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_PASSES
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES
      ?? "8",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_MAX_SHIFT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT
      ?? "480",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_EXTRA
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA
      ?? "12",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_VISUAL_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK
      ?? "24",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "24",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_BUNDLE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_BUNDLE_NODE_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BUNDLE_NODE_SLACK
      ?? "0",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_BBOX_LIMIT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.02",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL: "0",
    DJERD_NO_BUNDLE_CLEAR: "1",
    DJERD_BUNDLE_SHIFT: "0",
    DJERD_HUB_PUSH: "0",
    DJERD_NO_C3: "1",
    DJERD_LEAF_PASSES: "0",
    DJERD_LEAF_PASSES_2: "0",
    DJERD_NO_LEAF_UNTANGLE: "1",
    DJERD_ISOLATED_STASH: "0",
    DJERD_FACE_RASTER: "0",
    DJERD_FACE_UNTANGLE: "0",
    DJERD_HOT_REGION_SA: "0",
    DJERD_STUCK_LEAF_2D: "0",
    DJERD_XINGS_DETOUR: "0",
    DJERD_XINGS_DETOUR_FINAL: "0",
    DJERD_EDGE_DETOUR_FINAL: "0",
    DJERD_L_BEND_REROUTE: "0",
    DJERD_PERIPHERY_REROUTE: "0",
    DJERD_DIAGONAL_RETOUCH: "0",
    DJERD_NODE_PAIR_RETOUCH: "0",
    DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS: "0",
    DJERD_NO_PD_KNOT: "1",
    DJERD_VISUAL_KNOT: "0",

    DJERD_NODE_SPACING_CLEAR_FINAL: "1",
    DJERD_NODE_SPACING_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_PASSES
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES
      ?? "4",
    DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_SHIFT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_EXTRA
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA
      ?? "8",
    DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK
      ?? "12",
    DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "24",
    DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.01",
  });
}

function ogdfOptimizedVisualCrossRecompactPreserveEnv(): Record<string, string | undefined> {
  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN: "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_PRESERVE_ROUTE_SYNC_GAP
      ?? "1000000000",
    DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH: "0",

    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL: "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL: "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL: "0",
    DJERD_BBOX_AXIS_SCALE_FINAL: "0",
    DJERD_DENSITY_BALANCE_FINAL: "0",
    DJERD_DENSITY_PACK_FINAL: "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL: "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL: "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL: "0",
    DJERD_NODE_EDGE_RELIEF_FINAL: "0",
    DJERD_NODE_OVERLAP_CLEAR_FINAL: "0",
    DJERD_NODE_SPACING_CLEAR_FINAL: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL: "0",
    DJERD_NO_BUNDLE_CLEAR: "1",
    DJERD_BUNDLE_SHIFT: "0",
    DJERD_HUB_PUSH: "0",
    DJERD_NO_C3: "1",
    DJERD_LEAF_PASSES: "0",
    DJERD_LEAF_PASSES_2: "0",
    DJERD_NO_LEAF_UNTANGLE: "1",
    DJERD_ISOLATED_STASH: "0",
    DJERD_FACE_RASTER: "0",
    DJERD_FACE_UNTANGLE: "0",
    DJERD_HOT_REGION_SA: "0",
    DJERD_STUCK_LEAF_2D: "0",
    DJERD_XINGS_DETOUR: "0",
    DJERD_XINGS_DETOUR_FINAL: "0",
    DJERD_EDGE_DETOUR_FINAL: "0",
    DJERD_L_BEND_REROUTE: "0",
    DJERD_PERIPHERY_REROUTE: "0",
    DJERD_DIAGONAL_RETOUCH: "0",
    DJERD_NODE_PAIR_RETOUCH: "0",
    DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS: "0",
    DJERD_NO_PD_KNOT: "1",
    DJERD_VISUAL_KNOT: "0",
  });
}

function ogdfFinalExportRetouchEnv(): Record<string, string | undefined> {
  return ogdfRenderedCarrierEnv({
    DJERD_SKIP_CG_OPT: process.env.DJERD_SKIP_CG_OPT ?? "1",
    DJERD_NO_KNOT_MIN: "1",
    DJERD_FINAL_ROUTE_SYNC_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_SYNC_GAP
      ?? "1000000000",
    DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_RESTORE_LAYOUT
      ?? "0",

    DJERD_ATTACH_ISOLATED_BY_NAME_FINAL: "0",
    DJERD_ISOLATED_BBOX_COMPACT_FINAL: "0",
    DJERD_SIDECAR_BBOX_COMPACT_FINAL: "0",
    DJERD_BBOX_AXIS_SCALE_FINAL: "0",
    DJERD_DENSITY_BALANCE_FINAL: "0",
    DJERD_DENSITY_PACK_FINAL: "0",
    DJERD_BUNDLE_BOX_RELOCATE_FINAL: "0",
    DJERD_RIGID_COMPACT_BBOX_FINAL: "0",
    DJERD_RIGID_ATTACH_ISOLATED_FINAL: "0",
    DJERD_NODE_EDGE_RELIEF_FINAL: "0",
    DJERD_NODE_OVERLAP_CLEAR_FINAL: "0",
    DJERD_NODE_SPACING_CLEAR_FINAL: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL: "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL: "0",
    DJERD_NO_BUNDLE_CLEAR: "1",
    DJERD_BUNDLE_SHIFT: "0",
    DJERD_HUB_PUSH: "0",
    DJERD_NO_C3: "1",
    DJERD_LEAF_PASSES: "0",
    DJERD_LEAF_PASSES_2: "0",
    DJERD_NO_LEAF_UNTANGLE: "1",
    DJERD_ISOLATED_STASH: "0",
    DJERD_FACE_RASTER: "0",
    DJERD_FACE_UNTANGLE: "0",
    DJERD_HOT_REGION_SA: "0",
    DJERD_STUCK_LEAF_2D: "0",
    DJERD_XINGS_DETOUR: "0",
    DJERD_XINGS_DETOUR_FINAL: "0",
    DJERD_EDGE_DETOUR_FINAL: "0",
    DJERD_L_BEND_REROUTE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND
      ?? "1",
    DJERD_L_BEND_REROUTE_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_TOPK
      ?? "960",
    DJERD_L_BEND_REROUTE_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_MIN_GAIN
      ?? "1",
    DJERD_L_BEND_REROUTE_EDGE_NODE_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_EDGE_NODE_WEIGHT
      ?? "4",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_SEGMENT_OVERLAP_WEIGHT
      ?? "4",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_L_BEND_REROUTE_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_PERIPHERY_REROUTE: "0",
    DJERD_NO_PD_KNOT: "1",
    DJERD_VISUAL_KNOT: "0",

    DJERD_DIAGONAL_RETOUCH: "1",
    DJERD_NODE_PAIR_RETOUCH: "1",
    DJERD_NODE_PAIR_RETOUCH_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_TOPK
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_TOPK
      ?? "2048",
    DJERD_NODE_PAIR_RETOUCH_ROUNDS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_ROUNDS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_ROUNDS
      ?? "10",
    DJERD_NODE_PAIR_RETOUCH_STEPS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_STEPS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_STEPS
      ?? "8",
    DJERD_NODE_PAIR_RETOUCH_MIN_SPAN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_MIN_SPAN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MIN_SPAN
      ?? "700",
    DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_MIN_SPAN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MIN_SPAN
      ?? "450",
    DJERD_NODE_PAIR_RETOUCH_STEP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_STEP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_STEP
      ?? "240",
    DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_MAX_SHIFT
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT
      ?? "4200",
    DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_MAX_INCIDENT
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT
      ?? "64",
    DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_NODE_MARGIN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_ALLOW_NODE_OVERLAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_ALLOW_NODE_OVERLAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_ALLOW_NODE_OVERLAP
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_LEAF_SNAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_SNAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_SNAP
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_ONLY
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_LEAF_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_GAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_GAP
      ?? "16",
    DJERD_NODE_PAIR_RETOUCH_RAW_ACCEPT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_RAW_ACCEPT
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_RAW_ACCEPT
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_COMPACT_RELOCATE
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_SLOT_RINGS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS
      ?? "12",
    DJERD_NODE_PAIR_RETOUCH_PAIR_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_PAIR_GAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_PAIR_GAP
      ?? "16",
    DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_SEGMENTS
      ?? process.env.DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS
      ?? "0",
  });
}

function ogdfFinalExportRetouchSpacingRepairEnv(): Record<string, string | undefined> {
  return {
    ...ogdfFinalExportRetouchEnv(),
    DJERD_DIAGONAL_RETOUCH: "0",
    DJERD_NODE_PAIR_RETOUCH: "0",
    DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS: "0",
    DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH: "0",
    DJERD_NODE_SPACING_CLEAR_FINAL: "1",
    DJERD_NODE_SPACING_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_PASSES
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES
      ?? "8",
    DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_MAX_SHIFT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT
      ?? "480",
    DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_EXTRA
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA
      ?? "12",
    DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_VISUAL_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK
      ?? "24",
    DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "24",
    DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_BBOX_LIMIT
      ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.01",
  };
}

function ogdfFinalExportRetouchDebtRepairEnv(): Record<string, string | undefined> {
  return {
    ...ogdfFinalExportRetouchSpacingRepairEnv(),
    DJERD_BBOX_AXIS_SCALE_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_AXIS_SCALE
      ?? "1",
    DJERD_BBOX_Y_SCALE_FINAL_SCALES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_Y_SCALES
      ?? "0.98,0.95,0.92,0.88,0.84",
    DJERD_BBOX_AXIS_SCALE_FINAL_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_MIN_GAIN
      ?? "0.01",
    DJERD_BBOX_AXIS_SCALE_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_VISUAL_SLACK
      ?? "64",
    DJERD_BBOX_AXIS_SCALE_FINAL_BUNDLE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_BUNDLE_NODE_SLACK
      ?? "2",
    DJERD_BBOX_AXIS_SCALE_FINAL_MAX_ASPECT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_MAX_ASPECT
      ?? "2.4",
    DJERD_DENSITY_PACK_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK
      ?? "1",
    DJERD_DENSITY_PACK_SCALES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_SCALES
      ?? "0.94,0.90,0.86,0.82,0.78,0.72,0.68",
    DJERD_DENSITY_PACK_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_MIN_GAIN
      ?? "0.03",
    DJERD_DENSITY_PACK_TOP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_TOP
      ?? "0",
    DJERD_DENSITY_PACK_EMPTY_BAND_KEEP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_EMPTY_BAND_KEEP
      ?? "480",
    DJERD_DENSITY_PACK_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_VISUAL_SLACK
      ?? "64",
    DJERD_DENSITY_PACK_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_EDGE_NODE_SLACK
      ?? "96",
    DJERD_DENSITY_PACK_SPACING_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_SPACING_SLACK
      ?? "83",
    DJERD_DENSITY_PACK_BUNDLE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_BUNDLE_NODE_SLACK
      ?? "2",
    DJERD_DENSITY_PACK_NODE_OVERLAP_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_NODE_OVERLAP_SLACK
      ?? "0",
    DJERD_DENSITY_PACK_CLEANUP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_CLEANUP
      ?? "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_CLEAR
      ?? "1",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_PASSES
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES
      ?? "8",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_MAX_SHIFT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT
      ?? "480",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_EXTRA
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA
      ?? "12",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_VISUAL_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK
      ?? "24",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_EDGE_NODE_SLACK
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK
      ?? "24",
    DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_NODE_OVERLAP_BBOX_LIMIT
      ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT
      ?? "1.02",

    DJERD_NODE_EDGE_RELIEF_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_PASSES
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_PASSES
      ?? "4",
    DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_MAX_SHIFT
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT
      ?? "220",
    DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_STRENGTH
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH
      ?? "0.95",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_ENDPOINTS
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS
      ?? "1",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_TOP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_ENDPOINT_TOP
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_TOP
      ?? "80",
    DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_STEPS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_ENDPOINT_STEPS
      ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_STEPS
      ?? "80,160,300,600",

    DJERD_EDGE_DETOUR_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_EDGE_DETOUR
      ?? "1",
    DJERD_EDGE_DETOUR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_EDGE_DETOUR_PASSES
      ?? "1",
    DJERD_EDGE_DETOUR_FINAL_CROSS_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_EDGE_DETOUR_CROSS_WEIGHT
      ?? "1",
    DJERD_EDGE_DETOUR_FINAL_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_EDGE_DETOUR_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_EDGE_DETOUR_FINAL_CLEARANCE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_EDGE_DETOUR_CLEARANCE
      ?? "36",

    DJERD_L_BEND_REROUTE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND
      ?? "1",
    DJERD_L_BEND_REROUTE_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_TOPK
      ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_TOPK
      ?? "960",
    DJERD_L_BEND_REROUTE_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_MIN_GAIN
      ?? "0",
    DJERD_L_BEND_REROUTE_EDGE_NODE_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_EDGE_NODE_WEIGHT
      ?? "4",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_SEGMENT_OVERLAP_WEIGHT
      ?? "12",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.001",
    DJERD_L_BEND_REROUTE_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_LENGTH_WEIGHT
      ?? "0.0001",

    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_PASSES
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES
      ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_MAX_SHIFT
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT
      ?? "3600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_EXTRA
      ?? process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA
      ?? "32",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_VISUAL_SLACK
      ?? "48",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_NODE_OVERLAP_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_NODE_OVERLAP_SLACK
      ?? "0",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_EDGE_NODE_SLACK
      ?? "48",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_BBOX_LIMIT
      ?? "1.02",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_PUSH_NODES
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_PUSH_NODE_PASSES
      ?? "2",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR_PUSH_NODE_MAX_SHIFT
      ?? "600",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR
      ?? "1",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_PASSES
      ?? "4",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_VISUAL_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_VISUAL_SLACK
      ?? "48",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_EDGE_NODE_SLACK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_EDGE_NODE_SLACK
      ?? "48",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_TOP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_TOP
      ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_MAX_CANDIDATES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_MAX_CANDIDATES
      ?? "48",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_FULL_SHORTLIST:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_FULL_SHORTLIST
      ?? "8",
    DJERD_LEAF_BUNDLE_NODE_CLEAR_AFTER_RELOCATE_FINAL_BBOX_LIMIT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR_BBOX_LIMIT
      ?? "1.02",
  };
}

function ogdfFinalExportRetouchDebtRepairHubCorridorEnv(): Record<string, string | undefined> {
  return {
    ...ogdfFinalExportRetouchEnv(),
    DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_RESTORE_LAYOUT
      ?? "0",
    DJERD_L_BEND_REROUTE:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND
      ?? "1",
    DJERD_L_BEND_REROUTE_TOPK:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_TOPK
      ?? "960",
    DJERD_L_BEND_REROUTE_MIN_GAIN:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_MIN_GAIN
      ?? "0",
    DJERD_L_BEND_REROUTE_EDGE_NODE_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_EDGE_NODE_WEIGHT
      ?? "6",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_SEGMENT_OVERLAP_WEIGHT
      ?? "12",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.001",
    DJERD_L_BEND_REROUTE_LENGTH_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_PERIPHERY_REROUTE:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY
      ?? "1",
    DJERD_PERIPHERY_TOPK:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_TOPK
      ?? "96",
    DJERD_PERIPHERY_MAX_LANES:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_MAX_LANES
      ?? "4",
    DJERD_PERIPHERY_EDGE_NODE_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_EDGE_NODE_WEIGHT
      ?? "6",
    DJERD_PERIPHERY_SEGMENT_OVERLAP_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_SEGMENT_OVERLAP_WEIGHT
      ?? "12",
    DJERD_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.001",
    DJERD_DIAGONAL_RETOUCH:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_DIAGONAL_RETOUCH
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_RETOUCH
      ?? "1",
    DJERD_NODE_PAIR_RETOUCH_TOPK:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_TOPK
      ?? "768",
    DJERD_NODE_PAIR_RETOUCH_ROUNDS:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_ROUNDS
      ?? "4",
    DJERD_NODE_PAIR_RETOUCH_STEPS:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_STEPS
      ?? "4",
    DJERD_NODE_PAIR_RETOUCH_MIN_SPAN:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_MIN_SPAN
      ?? "420",
    DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_LEAF_MIN_SPAN
      ?? "360",
    DJERD_NODE_PAIR_RETOUCH_STEP:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_STEP
      ?? "180",
    DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_MAX_SHIFT
      ?? "2400",
    DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_MAX_INCIDENT
      ?? "512",
    DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_NODE_MARGIN
      ?? "16",
    DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_LEAF_ONLY
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_LEAF_GAP:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_LEAF_GAP
      ?? "48",
    DJERD_NODE_PAIR_RETOUCH_PAIR_GAP:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_PAIR_GAP
      ?? "48",
    DJERD_NODE_PAIR_RETOUCH_RAW_ACCEPT:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_RAW_ACCEPT
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_COMPACT_RELOCATE
      ?? "0",
    DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS:
      process.env
        .DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_SLOT_RINGS
      ?? "8",
  };
}

function ogdfFinalExportRetouchClearanceRetryEnv(): Record<string, string | undefined> {
  return {
    ...ogdfFinalExportRetouchEnv(),
    DJERD_NODE_PAIR_RETOUCH_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_NODE_PAIR_TOPK
      ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_TOPK
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_TOPK
      ?? "768",
    DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_SLOT_RINGS
      ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_SLOT_RINGS
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS
      ?? "6",
    DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_NODE_MARGIN
      ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_NODE_MARGIN
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN
      ?? "24",
    DJERD_NODE_PAIR_RETOUCH_LEAF_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_LEAF_GAP
      ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_GAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_GAP
      ?? "72",
    DJERD_NODE_PAIR_RETOUCH_PAIR_GAP:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_PAIR_GAP
      ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_PAIR_GAP
      ?? process.env.DJERD_NODE_PAIR_RETOUCH_PAIR_GAP
      ?? "72",
  };
}

function visualCrossPolishUsesBundleClear(
  variant: VisualCrossPolishVariant,
): boolean {
  return variant === "route-clear"
    || variant === "route-clear-tight"
    || variant === "route-clear-wide"
    || variant === "route-clear-short"
    || variant === "route-clear-deep"
    || variant === "route-clear-lbend"
    || variant === "route-clear-deep-lbend"
    || variant === "route-clear-periphery"
    || variant === "route-clear-deep-periphery"
    || variant === "detour-clear"
    || variant === "knot-clear";
}

function visualCrossPolishUsesPeriphery(
  variant: VisualCrossPolishVariant,
): boolean {
  return variant === "route-clear-periphery"
    || variant === "route-clear-deep-periphery";
}

function ogdfOptimizedVisualCrossPolishDeepEnv(): Record<string, string | undefined> {
  return {
    DJERD_EDGE_DETOUR_FINAL_PASSES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_PASSES
      ?? "2",
    DJERD_XINGS_DETOUR_ITERS:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_ITERS
      ?? "4",
    DJERD_XINGS_DETOUR_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_TOPK
      ?? "20",
    DJERD_XINGS_DETOUR_MAX_OFFSET:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_MAX_OFFSET
      ?? "720",
  };
}

function ogdfOptimizedVisualCrossPolishPeripheryEnv(): Record<string, string | undefined> {
  return {
    DJERD_PERIPHERY_REROUTE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_REROUTE
      ?? "1",
    DJERD_PERIPHERY_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_TOPK
      ?? "120",
    DJERD_PERIPHERY_MAX_LANES:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_MAX_LANES
      ?? "6",
    DJERD_PERIPHERY_EDGE_NODE_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_EDGE_NODE_WEIGHT
      ?? "1",
    DJERD_PERIPHERY_SEGMENT_OVERLAP_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_SEGMENT_OVERLAP_WEIGHT
      ?? "1",
    DJERD_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.0001",
  };
}

function ogdfOptimizedVisualCrossPolishLBendEnv(): Record<string, string | undefined> {
  return {
    DJERD_L_BEND_REROUTE:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_REROUTE
      ?? "1",
    DJERD_L_BEND_REROUTE_TOPK:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_TOPK
      ?? "240",
    DJERD_L_BEND_REROUTE_MIN_GAIN:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_MIN_GAIN
      ?? "1",
    DJERD_L_BEND_REROUTE_EDGE_NODE_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_EDGE_NODE_WEIGHT
      ?? "1",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_SEGMENT_OVERLAP_WEIGHT
      ?? "1",
    DJERD_L_BEND_REROUTE_SEGMENT_OVERLAP_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT
      ?? "0.0001",
    DJERD_L_BEND_REROUTE_LENGTH_WEIGHT:
      process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_LENGTH_WEIGHT
      ?? "0",
  };
}

function ogdfOptimizedVisualCrossPolishVariantEnv(
  variant: VisualCrossPolishVariant,
): Record<string, string | undefined> {
  switch (variant) {
    case "route-clear-tight":
      return {
        DJERD_EDGE_DETOUR_FINAL_CLEARANCE:
          process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_CLEARANCE
          ?? "16",
      };
    case "route-clear-wide":
      return {
        DJERD_EDGE_DETOUR_FINAL_CLEARANCE:
          process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_CLEARANCE
          ?? "44",
      };
    case "route-clear-short":
      return {
        DJERD_EDGE_DETOUR_FINAL_LENGTH_WEIGHT:
          process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_LENGTH_WEIGHT
          ?? "0.03",
        DJERD_XINGS_DETOUR_MAX_OFFSET:
          process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_MAX_OFFSET
          ?? "360",
      };
    case "route-clear-deep":
      return ogdfOptimizedVisualCrossPolishDeepEnv();
    case "route-clear-lbend":
      return ogdfOptimizedVisualCrossPolishLBendEnv();
    case "route-clear-deep-lbend":
      return {
        ...ogdfOptimizedVisualCrossPolishDeepEnv(),
        ...ogdfOptimizedVisualCrossPolishLBendEnv(),
      };
    case "route-clear-periphery":
      return ogdfOptimizedVisualCrossPolishPeripheryEnv();
    case "route-clear-deep-periphery":
      return {
        ...ogdfOptimizedVisualCrossPolishDeepEnv(),
        ...ogdfOptimizedVisualCrossPolishPeripheryEnv(),
      };
    case "route-retouch":
      return {
        DJERD_FINAL_ROUTE_SYNC_GAP:
          process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_SYNC_GAP
          ?? "1000000000",
        DJERD_RESTORE_LAYOUT_TSV_BEFORE_RETOUCH:
          process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_RESTORE_LAYOUT
          ?? "1",
        DJERD_XINGS_DETOUR: "0",
        DJERD_EDGE_DETOUR_FINAL: "0",
        DJERD_L_BEND_REROUTE: "0",
        DJERD_PERIPHERY_REROUTE: "0",
        DJERD_NO_PD_KNOT: "1",
        DJERD_VISUAL_KNOT: "0",
      };
    default:
      return {};
  }
}

function renderedCarrierCacheKeyParts(): string[] {
  return [
    `renderedCarrier=${process.env.DJERD_RENDERED_CARRIER_METRICS_FINAL ?? "1"}`,
    `hubCarrier=${process.env.DJERD_HUB_CARRIER_CROSS_FINAL ?? "1"}`,
    `hubThreshold=${
      process.env.DJERD_HUB_CARRIER_CROSS_FINAL_THRESHOLD
      ?? DEFAULT_RENDERED_CARRIER_THRESHOLD
    }`,
    `stressPostPassIters=${process.env.DJERD_STRESS_POST_PASS_ITERS ?? "0"}`,
    `stressPostPassEdgeCost=${process.env.DJERD_STRESS_POST_PASS_EDGE_COST ?? "140"}`,
    `noCarrier=${process.env.DJERD_NO_CARRIER_CROSS ?? "0"}`,
    `nodeMargin=${process.env.DJERD_NODE_VISUAL_MARGIN ?? "8"}`,
    `leafBundleMargin=${process.env.DJERD_LEAF_BUNDLE_VISUAL_MARGIN ?? "32"}`,
    `isolatedBboxCompact=${process.env.DJERD_ISOLATED_BBOX_COMPACT_FINAL ?? "1"}`,
    `isolatedBboxCompactGapX=${process.env.DJERD_ISOLATED_BBOX_COMPACT_GAP_X ?? "180"}`,
    `isolatedBboxCompactGapY=${process.env.DJERD_ISOLATED_BBOX_COMPACT_GAP_Y ?? "36"}`,
    `isolatedBboxCompactOffsetY=${process.env.DJERD_ISOLATED_BBOX_COMPACT_OFFSET_Y ?? "140"}`,
    `sidecarBboxCompact=${process.env.DJERD_SIDECAR_BBOX_COMPACT_FINAL ?? "1"}`,
    `sidecarBboxCompactGapX=${process.env.DJERD_SIDECAR_BBOX_COMPACT_GAP_X ?? "160"}`,
    `sidecarBboxCompactGapY=${process.env.DJERD_SIDECAR_BBOX_COMPACT_GAP_Y ?? "120"}`,
    `sidecarBboxCompactMaxAspect=${process.env.DJERD_SIDECAR_BBOX_COMPACT_MAX_ASPECT ?? "2.2"}`,
    `bboxAxisScale=${process.env.DJERD_BBOX_AXIS_SCALE_FINAL ?? "1"}`,
    `densityBalance=${process.env.DJERD_DENSITY_BALANCE_FINAL ?? "0"}`,
    `densityBalanceCell=${process.env.DJERD_DENSITY_BALANCE_CELL ?? "1600"}`,
    `densityBalanceTop=${process.env.DJERD_DENSITY_BALANCE_TOP ?? "18"}`,
    `densityBalanceScale=${process.env.DJERD_DENSITY_BALANCE_SCALE ?? "1.055"}`,
    `densityBalanceMaxScale=${process.env.DJERD_DENSITY_BALANCE_MAX_SCALE ?? "1.11"}`,
    `densityBalanceMinFill=${process.env.DJERD_DENSITY_BALANCE_MIN_FILL ?? "0.66"}`,
    `densityBalanceVisualSlack=${process.env.DJERD_DENSITY_BALANCE_VISUAL_SLACK ?? "40"}`,
    `densityBalanceEdgeNodeSlack=${process.env.DJERD_DENSITY_BALANCE_EDGE_NODE_SLACK ?? "80"}`,
    `densityBalanceBboxLimit=${process.env.DJERD_DENSITY_BALANCE_BBOX_LIMIT ?? "1.025"}`,
    `densityPack=${process.env.DJERD_DENSITY_PACK_FINAL ?? "1"}`,
    `densityPackScales=${process.env.DJERD_DENSITY_PACK_SCALES ?? "0.94,0.90,0.86,0.82,0.78,0.72"}`,
    `densityPackExpandScale=${process.env.DJERD_DENSITY_PACK_EXPAND_SCALE ?? "1.08"}`,
    `densityPackTop=${process.env.DJERD_DENSITY_PACK_TOP ?? "0"}`,
    `densityPackEmptyBandKeep=${process.env.DJERD_DENSITY_PACK_EMPTY_BAND_KEEP ?? "720"}`,
    `densityPackVisualSlack=${process.env.DJERD_DENSITY_PACK_VISUAL_SLACK ?? "180"}`,
    `densityPackEdgeNodeSlack=${process.env.DJERD_DENSITY_PACK_EDGE_NODE_SLACK ?? "160"}`,
    `densityPackSpacingSlack=${process.env.DJERD_DENSITY_PACK_SPACING_SLACK ?? "40"}`,
    `densityPackP90Slack=${process.env.DJERD_DENSITY_PACK_P90_SLACK ?? "1"}`,
    `densityPackMaxSlack=${process.env.DJERD_DENSITY_PACK_MAX_SLACK ?? "2"}`,
    `densityPackCleanup=${process.env.DJERD_DENSITY_PACK_CLEANUP ?? "0"}`,
    `skipCgOpt=${process.env.DJERD_SKIP_CG_OPT ?? "1"}`,
    `noKnotMin=${process.env.DJERD_NO_KNOT_MIN ?? "1"}`,
    `nodeSpacingClear=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL ?? "1"}`,
    `nodeSpacingClearPasses=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES ?? "6"}`,
    `nodeSpacingClearMaxShift=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT ?? "220"}`,
    `nodeSpacingClearExtra=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA ?? "8"}`,
    `nodeSpacingClearVisualSlack=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK ?? "80"}`,
    `nodeSpacingClearEdgeNodeSlack=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK ?? "80"}`,
    `nodeSpacingClearBboxLimit=${process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT ?? "1.025"}`,
    `leafBundleNodeClear=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL ?? "1"}`,
    `leafBundleNodeClearPasses=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PASSES ?? "8"}`,
    `leafBundleNodeClearMaxShift=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_MAX_SHIFT ?? "3600"}`,
    `leafBundleNodeClearExtra=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EXTRA ?? "32"}`,
    `leafBundleNodeClearVisualSlack=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_VISUAL_SLACK ?? "0"}`,
    `leafBundleNodeClearNodeOverlapSlack=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_NODE_OVERLAP_SLACK ?? "0"}`,
    `leafBundleNodeClearEdgeNodeSlack=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_EDGE_NODE_SLACK ?? "0"}`,
    `leafBundleNodeClearBboxLimit=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_BBOX_LIMIT ?? "1.04"}`,
    `leafBundleNodeClearPushNodes=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODES ?? "1"}`,
    `leafBundleNodeClearPushNodePasses=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_PASSES ?? "2"}`,
    `leafBundleNodeClearPushNodeMaxShift=${process.env.DJERD_LEAF_BUNDLE_NODE_CLEAR_FINAL_PUSH_NODE_MAX_SHIFT ?? "600"}`,
    `optimizedBboxTargetB=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_B ?? DEFAULT_OPTIMIZED_BBOX_TARGET_B.toFixed(1)}`,
    `optimizedBboxTargetVariants=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_VARIANTS ?? "local,holistic"}`,
    `optimizedBboxTargetPositionStrategies=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_POSITION_STRATEGIES ?? "gap,density-scale,scale"}`,
    `optimizedBboxTargetDensityCellFactor=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_CELL_FACTOR ?? "4"}`,
    `optimizedBboxTargetDensitySparseRatio=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_SPARSE_RATIO ?? "0.5"}`,
    `optimizedBboxTargetDensityBias=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS ?? "auto"}`,
    `optimizedBboxTargetDensityBiasCvMin=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_CV_MIN ?? "0.25"}`,
    `optimizedBboxTargetDensityBiasCvMax=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_CV_MAX ?? "1.5"}`,
    `optimizedBboxTargetDensityBiasAutoMin=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_AUTO_MIN ?? "0.4"}`,
    `optimizedBboxTargetDensityBiasAutoMax=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_AUTO_MAX ?? "0.85"}`,
    `optimizedBboxTargetDensityPreBalance=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_PRE_BALANCE ?? "0.25"}`,
    `optimizedBboxTargetGapMinFactor=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_FACTOR ?? "0.85"}`,
    `optimizedBboxTargetGapMinX=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_X ?? "auto"}`,
    `optimizedBboxTargetGapMinY=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_Y ?? "auto"}`,
    `optimizedBboxTargetStages=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGES_B ?? "relative"}`,
    `optimizedBboxTargetStageRatios=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_RATIOS ?? "0.80,0.63,0.47,0.34,0.23,0.14"}`,
    `optimizedBboxTargetSafeties=${
      process.env.DJERD_OPTIMIZED_BBOX_TARGET_SAFETIES
      ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_SAFETY
      ?? "0.99,0.94,0.90"
    }`,
    `optimizedBboxTargetStageMaxQualityDebtPerGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_MAX_QUALITY_DEBT_PER_GAIN ?? "0.40"}`,
    `optimizedBboxTargetFinalMaxQualityDebtPerGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_FINAL_MAX_QUALITY_DEBT_PER_GAIN ?? "0.40"}`,
    `optimizedBboxTargetStageMaxSpacingDebtPerGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_MAX_SPACING_DEBT_PER_GAIN ?? "1.00"}`,
    `optimizedBboxTargetFinalMaxSpacingDebtPerGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_FINAL_MAX_SPACING_DEBT_PER_GAIN ?? "0.25"}`,
    `optimizedBboxTargetEdgeNodeDebtWeight=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_EDGE_NODE_DEBT_WEIGHT ?? "0.50"}`,
    `optimizedBboxTargetTimeoutMs=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_TIMEOUT_MS ?? "60000"}`,
    `optimizedBboxTargetLeafPasses=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_LEAF_PASSES ?? "1"}`,
    `optimizedBboxTargetLeafPasses2=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_LEAF_PASSES_2 ?? "0"}`,
    `optimizedBboxTargetMaxVisual=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL ?? "auto"}`,
    `optimizedBboxTargetMaxBundleNode=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_MAX_BUNDLE_NODE ?? "0"}`,
    `optimizedBboxTargetMaxNodeSpacing=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_MAX_NODE_SPACING ?? "auto"}`,
    `optimizedBboxTargetSoftB=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_SOFT_B ?? "1.2"}`,
    `optimizedBboxTargetAcceptMinGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_ACCEPT_MIN_GAIN ?? "0.35"}`,
    `optimizedBboxTargetPartialAccept=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_ACCEPT ?? "1"}`,
    `optimizedBboxTargetContinueAfterFinalPartial=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_CONTINUE_AFTER_FINAL_PARTIAL ?? "1"}`,
    `optimizedBboxTargetPartialMinGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MIN_GAIN ?? "0.025"}`,
    `optimizedBboxTargetPartialMaxQualityDebt=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MAX_QUALITY_DEBT ?? "0.04"}`,
    `optimizedBboxTargetPartialMaxSpacingDebt=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MAX_SPACING_DEBT ?? "0.01"}`,
    `optimizedBboxTargetMaxVisualDebt=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL_DEBT_RATIO ?? "0.05"}`,
    `optimizedBboxTargetStageBailAfter=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_AFTER ?? "6"}`,
    `optimizedBboxTargetStageBailQualityRatio=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_QUALITY_RATIO ?? "5.0"}`,
    `optimizedBboxTargetStageBailSpacingRatio=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_SPACING_RATIO ?? "10.0"}`,
    `optimizedBboxTargetStageBailNearRatio=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_NEAR_RATIO ?? "2.0"}`,
    `optimizedBboxTargetBundleEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_EDGE_NODE_SLACK ?? "24"}`,
    `optimizedBboxTargetBundleVisualSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_VISUAL_SLACK ?? "120"}`,
    `optimizedBboxTargetNodeOverlapClear=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR ?? "1"}`,
    `optimizedBboxTargetNodeOverlapClearPasses=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_PASSES ?? "8"}`,
    `optimizedBboxTargetNodeOverlapClearMaxShift=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_MAX_SHIFT ?? "260"}`,
    `optimizedBboxTargetNodeOverlapClearExtra=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_EXTRA ?? "12"}`,
    `optimizedBboxTargetNodeOverlapClearVisualSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_VISUAL_SLACK ?? "80"}`,
    `optimizedBboxTargetNodeOverlapClearEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_EDGE_NODE_SLACK ?? "80"}`,
    `optimizedBboxTargetNodeOverlapClearBboxLimit=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_OVERLAP_CLEAR_BBOX_LIMIT ?? "1.03"}`,
    `optimizedBboxTargetDensityBalance=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE ?? "0"}`,
    `optimizedBboxTargetDensityBalanceTop=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_TOP ?? "18"}`,
    `optimizedBboxTargetDensityBalanceVisualSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_VISUAL_SLACK ?? "40"}`,
    `optimizedBboxTargetDensityBalanceEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_EDGE_NODE_SLACK ?? "80"}`,
    `optimizedBboxTargetDensityBalanceBboxLimit=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BALANCE_BBOX_LIMIT ?? "1.025"}`,
    `optimizedBboxTargetDensityPack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_PACK ?? "0"}`,
    `optimizedBboxTargetNodeSpacingClear=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR ?? "1"}`,
    `optimizedBboxTargetNodeSpacingClearPasses=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_PASSES ?? "6"}`,
    `optimizedBboxTargetNodeSpacingClearMaxShift=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_MAX_SHIFT ?? "220"}`,
    `optimizedBboxTargetNodeSpacingClearExtra=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_EXTRA ?? "8"}`,
    `optimizedBboxTargetNodeSpacingClearVisualSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_VISUAL_SLACK ?? "80"}`,
    `optimizedBboxTargetNodeSpacingClearEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_EDGE_NODE_SLACK ?? "80"}`,
    `optimizedBboxTargetNodeSpacingClearBboxLimit=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_NODE_SPACING_CLEAR_BBOX_LIMIT ?? "1.025"}`,
    `optimizedBboxTargetBundleClearAfterRelocate=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE ?? "1"}`,
    `optimizedBboxTargetBundleClearAfterRelocatePasses=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_PASSES ?? "4"}`,
    `optimizedBboxTargetBundleClearAfterRelocateVisualSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_VISUAL_SLACK ?? "80"}`,
    `optimizedBboxTargetBundleClearAfterRelocateTop=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_TOP ?? "6"}`,
    `optimizedBboxTargetBundleClearAfterRelocateMaxCandidates=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_MAX_CANDIDATES ?? "24"}`,
    `optimizedBboxTargetBundleClearAfterRelocateFullShortlist=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_FULL_SHORTLIST ?? "5"}`,
    `optimizedBboxTargetBundleClearAfterRelocateEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_EDGE_NODE_SLACK ?? "80"}`,
    `optimizedBboxTargetBundleClearAfterRelocateBboxLimit=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_CLEAR_AFTER_RELOCATE_BBOX_LIMIT ?? "1.03"}`,
    `optimizedBboxTargetBundleRelocate=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE ?? "1"}`,
    `optimizedBboxTargetBundleRelocatePasses=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_PASSES ?? "1"}`,
    `optimizedBboxTargetBundleRelocateTop=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_TOP ?? "8"}`,
    `optimizedBboxTargetBundleRelocateMaxCandidates=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_MAX_CANDIDATES ?? "32"}`,
    `optimizedBboxTargetBundleRelocateShortlist=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_SHORTLIST ?? "8"}`,
    `optimizedBboxTargetBundleRelocateFullScan=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_FULL_SCAN ?? "0"}`,
    `optimizedBboxTargetBundleRelocateMaxMove=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_MAX_MOVE ?? "6200"}`,
    `optimizedBboxTargetBundleRelocateTotalLimit=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_TOTAL_LIMIT ?? "768"}`,
    `optimizedBboxTargetBundleRelocateBboxLimit=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_BBOX_LIMIT ?? "1.03"}`,
    `optimizedBboxTargetBundleRelocateMinGain=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_BUNDLE_RELOCATE_MIN_GAIN ?? "0.25"}`,
    `optimizedBboxTargetReliefPasses=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_PASSES ?? "1"}`,
    `optimizedBboxTargetReliefMaxShift=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_MAX_SHIFT ?? "220"}`,
    `optimizedBboxTargetReliefStrength=${process.env.DJERD_OPTIMIZED_BBOX_TARGET_RELIEF_STRENGTH ?? "0.95"}`,
    `optimizedPostReroutePolishBudgetMs=${process.env.DJERD_OPTIMIZED_POST_REROUTE_POLISH_BUDGET_MS ?? DEFAULT_POST_REROUTE_POLISH_BUDGET_MS.toString()}`,
    `optimizedEdgeNodePolish=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH ?? "1"}`,
    `optimizedEdgeNodePolishVariants=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_VARIANTS ?? "cheap,local,holistic"}`,
    `optimizedEdgeNodePolishMinGain=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_MIN_GAIN_RATIO ?? "0.08"}`,
    `optimizedEdgeNodePolishMinVisualGain=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_MIN_VISUAL_GAIN_RATIO ?? "0.005"}`,
    `optimizedEdgeNodePolishAllowVisualRegression=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ALLOW_VISUAL_REGRESSION ?? "0"}`,
    `optimizedEdgeNodePolishBboxGrowth=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BBOX_GROWTH_LIMIT ?? "1.08"}`,
    `optimizedEdgeNodePolishAboveTargetBboxGrowth=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ABOVE_TARGET_BBOX_GROWTH_LIMIT ?? "1.002"}`,
    `optimizedEdgeNodePolishVisualDebtPerGain=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_MAX_VISUAL_DEBT_PER_GAIN ?? "0.75"}`,
    `optimizedEdgeNodePolishSpacingDebtPerGain=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_MAX_SPACING_DEBT_PER_GAIN ?? "1.00"}`,
    `optimizedEdgeNodePolishReliefPasses=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_RELIEF_PASSES ?? "3"}`,
    `optimizedEdgeNodePolishEndpoints=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ENDPOINTS ?? "1"}`,
    `optimizedEdgeNodePolishEndpointTop=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ENDPOINT_TOP ?? "40"}`,
    `optimizedEdgeNodePolishEndpointSteps=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_ENDPOINT_STEPS ?? "80,160,300,600"}`,
    `optimizedEdgeNodePolishBundleEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_EDGE_NODE_SLACK ?? "32"}`,
    `optimizedEdgeNodePolishBundleBboxLimit=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_BBOX_LIMIT ?? "1.08"}`,
    `optimizedEdgeNodePolishBundleAfterClear=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR ?? "1"}`,
    `optimizedEdgeNodePolishBundleAfterClearPasses=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_PASSES ?? "4"}`,
    `optimizedEdgeNodePolishBundleAfterClearTop=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_TOP ?? "8"}`,
    `optimizedEdgeNodePolishBundleAfterClearMaxCandidates=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_MAX_CANDIDATES ?? "32"}`,
    `optimizedEdgeNodePolishBundleAfterClearFullShortlist=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_FULL_SHORTLIST ?? "6"}`,
    `optimizedEdgeNodePolishBundleAfterClearBboxLimit=${process.env.DJERD_OPTIMIZED_EDGE_NODE_POLISH_BUNDLE_AFTER_CLEAR_BBOX_LIMIT ?? "1.06"}`,
    `optimizedVisualCrossPolish=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH ?? "1"}`,
    `optimizedVisualCrossPolishVariants=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_VARIANTS ?? DEFAULT_VISUAL_CROSS_POLISH_VARIANTS}`,
    `optimizedVisualCrossTarget=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_TARGET ?? DEFAULT_VISUAL_CROSS_TARGET.toString()}`,
    `optimizedVisualCrossPolishRounds=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUNDS ?? "4"}`,
    `optimizedVisualCrossPolishMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MIN_GAIN ?? "1"}`,
    `optimizedVisualCrossPolishMinGainRatio=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MIN_GAIN_RATIO ?? "0"}`,
    `optimizedVisualCrossPolishNodeBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_BBOX_GROWTH_LIMIT ?? "1.04"}`,
    `optimizedVisualCrossPolishRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUTE_BBOX_GROWTH_LIMIT ?? "1.08"}`,
    `optimizedVisualCrossPolishRouteBboxTieVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUTE_BBOX_TIE_VISUAL_SLACK ?? "8"}`,
    `optimizedVisualCrossPolishRouteBboxTieRatio=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUTE_BBOX_TIE_RATIO ?? "0.92"}`,
    `optimizedVisualCrossPolishTopologyTieVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_TOPOLOGY_TIE_VISUAL_SLACK ?? "64"}`,
    `optimizedVisualCrossPolishTopologyTieEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_TOPOLOGY_TIE_EDGE_NODE_SLACK ?? "32"}`,
    `optimizedVisualCrossPolishMaxRouteBboxDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_ROUTE_BBOX_DEBT_PER_GAIN ?? "0.05"}`,
    `optimizedVisualCrossPolishMaxSpacingDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SPACING_DEBT ?? "0"}`,
    `optimizedVisualCrossPolishMaxSpacingDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SPACING_DEBT_PER_GAIN ?? "0.00"}`,
    `optimizedVisualCrossPolishSpacingRepair=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR ?? "1"}`,
    `optimizedVisualCrossPolishSpacingRepairMaxDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_DEBT ?? "2"}`,
    `optimizedVisualCrossPolishSpacingRepairMaxNodeOverlaps=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_NODE_OVERLAPS ?? "8"}`,
    `optimizedVisualCrossPolishSpacingRepairMaxNodeOverlapSpacingDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_NODE_OVERLAP_SPACING_DEBT ?? "64"}`,
    `optimizedVisualCrossPolishSpacingRepairMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MIN_GAIN ?? "32"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_PASSES ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_PASSES ?? "8"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_MAX_SHIFT ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_MAX_SHIFT ?? "480"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapExtra=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_EXTRA ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EXTRA ?? "12"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_VISUAL_SLACK ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_VISUAL_SLACK ?? "24"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_EDGE_NODE_SLACK ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_EDGE_NODE_SLACK ?? "24"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapBundleNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_BUNDLE_NODE_SLACK ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BUNDLE_NODE_SLACK ?? "0"}`,
    `optimizedVisualCrossPolishSpacingRepairNodeOverlapBboxLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_NODE_OVERLAP_BBOX_LIMIT ?? process.env.DJERD_NODE_OVERLAP_CLEAR_FINAL_BBOX_LIMIT ?? "1.02"}`,
    `optimizedVisualCrossPolishSpacingRepairPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_PASSES ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES ?? "4"}`,
    `optimizedVisualCrossPolishSpacingRepairMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_SHIFT ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT ?? "220"}`,
    `optimizedVisualCrossPolishSpacingRepairExtra=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_EXTRA ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA ?? "8"}`,
    `optimizedVisualCrossPolishSpacingRepairVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_VISUAL_SLACK ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK ?? "12"}`,
    `optimizedVisualCrossPolishSpacingRepairEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_EDGE_NODE_SLACK ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK ?? "24"}`,
    `optimizedVisualCrossPolishSpacingRepairBboxLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_BBOX_LIMIT ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT ?? "1.01"}`,
    `optimizedVisualCrossPolishSpacingRepairTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_TIMEOUT_MS ?? "60000"}`,
    `optimizedVisualCrossRecompact=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT ?? "1"}`,
    `optimizedVisualCrossRecompactGrowthTrigger=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_GROWTH_TRIGGER ?? "1.08"}`,
    `optimizedVisualCrossRecompactTargetGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_GROWTH ?? "1.03"}`,
    `optimizedVisualCrossRecompactTargetB=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_B ?? process.env.DJERD_OPTIMIZED_BBOX_TARGET_B ?? DEFAULT_OPTIMIZED_BBOX_TARGET_B.toFixed(1)}`,
    `optimizedVisualCrossRecompactTargetTolerance=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_TOLERANCE ?? "1.02"}`,
    `optimizedVisualCrossRecompactTargetRatios=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_RATIOS ?? "0.95,0.90,0.85,0.75,0.65"}`,
    `optimizedVisualCrossRecompactTargetsB=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGETS_B ?? "auto"}`,
    `optimizedVisualCrossRecompactMinBboxGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_MIN_BBOX_GAIN ?? "0.015"}`,
    `optimizedVisualCrossRecompactMaxVisualDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_MAX_VISUAL_DEBT ?? "auto<=8"}`,
    `optimizedVisualCrossRecompactMaxEdgeNodeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_MAX_EDGE_NODE_DEBT ?? "8"}`,
    `optimizedVisualCrossRecompactRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_ROUTE_BBOX_GROWTH_LIMIT ?? "1.01"}`,
    `optimizedVisualCrossRecompactTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TIMEOUT_MS ?? "60000"}`,
    `optimizedVisualCrossRecompactPreserveRoutes=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_PRESERVE_ROUTES ?? "1"}`,
    `optimizedVisualCrossRecompactPreserveRouteSyncGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_PRESERVE_ROUTE_SYNC_GAP ?? "1000000000"}`,
    `optimizedVisualCrossRecompactSafetyLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_SAFETY_LIMIT ?? "1"}`,
    `optimizedVisualCrossRecompactStrategyLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_STRATEGY_LIMIT ?? "1"}`,
    `optimizedVisualCrossRecompactVariantLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_VARIANT_LIMIT ?? "2"}`,
    `optimizedVisualCrossPolishMaxOverlappingEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_OVERLAPPING_EDGE_DEBT ?? "0"}`,
    `optimizedVisualCrossPolishMaxOverlappingEdgeDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_OVERLAPPING_EDGE_DEBT_PER_GAIN ?? "0.25"}`,
    `optimizedVisualCrossPolishMaxSegmentOverlapDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SEGMENT_OVERLAP_DEBT ?? "0"}`,
    `optimizedVisualCrossPolishMaxSegmentOverlapDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SEGMENT_OVERLAP_DEBT_PER_GAIN ?? "0.25"}`,
    `optimizedVisualCrossPolishMaxBundleNode=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_BUNDLE_NODE ?? "6"}`,
    `optimizedVisualCrossPolishBundleClear=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR ?? "1"}`,
    `optimizedVisualCrossPolishBundleClearPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PASSES ?? "4"}`,
    `optimizedVisualCrossPolishBundleClearMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_MAX_SHIFT ?? "1600"}`,
    `optimizedVisualCrossPolishBundleClearExtra=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_EXTRA ?? "24"}`,
    `optimizedVisualCrossPolishBundleClearVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_VISUAL_SLACK ?? "32"}`,
    `optimizedVisualCrossPolishBundleClearEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_EDGE_NODE_SLACK ?? "96"}`,
    `optimizedVisualCrossPolishBundleClearBboxLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_BBOX_LIMIT ?? "1.04"}`,
    `optimizedVisualCrossPolishBundleClearPushNodes=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PUSH_NODES ?? "1"}`,
    `optimizedVisualCrossPolishBundleClearPushNodePasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PUSH_NODE_PASSES ?? "2"}`,
    `optimizedVisualCrossPolishBundleClearPushNodeMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_CLEAR_PUSH_NODE_MAX_SHIFT ?? "600"}`,
    `optimizedVisualCrossPolishBundleAfterClearPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_PASSES ?? "3"}`,
    `optimizedVisualCrossPolishBundleAfterClearVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_VISUAL_SLACK ?? "32"}`,
    `optimizedVisualCrossPolishBundleAfterClearEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_EDGE_NODE_SLACK ?? "96"}`,
    `optimizedVisualCrossPolishBundleAfterClearTop=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_TOP ?? "8"}`,
    `optimizedVisualCrossPolishBundleAfterClearMaxCandidates=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_MAX_CANDIDATES ?? "48"}`,
    `optimizedVisualCrossPolishBundleAfterClearFullShortlist=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_FULL_SHORTLIST ?? "8"}`,
    `optimizedVisualCrossPolishBundleAfterClearBboxLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_BUNDLE_AFTER_CLEAR_BBOX_LIMIT ?? "1.04"}`,
    `optimizedVisualCrossPolishXingsDetour=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR ?? "1"}`,
    `optimizedVisualCrossPolishXingsDetourPhase=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_PHASE ?? "post"}`,
    `optimizedVisualCrossPolishXingsDetourIters=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_ITERS ?? "3"}`,
    `optimizedVisualCrossPolishXingsDetourMaxSegs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_MAX_SEGS ?? "2"}`,
    `optimizedVisualCrossPolishXingsDetourTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_TOPK ?? "12"}`,
    `optimizedVisualCrossPolishXingsDetourMaxOffset=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_MAX_OFFSET ?? "480"}`,
    `optimizedVisualCrossPolishXingsDetourFinal=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_XINGS_DETOUR_FINAL ?? "0"}`,
    `optimizedVisualCrossPolishEdgeDetour=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR ?? "1"}`,
    `optimizedVisualCrossPolishEdgeDetourPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_PASSES ?? "1"}`,
    `optimizedVisualCrossPolishEdgeDetourCrossWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_CROSS_WEIGHT ?? "1.0"}`,
    `optimizedVisualCrossPolishEdgeDetourLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_LENGTH_WEIGHT ?? "0"}`,
    `optimizedVisualCrossPolishEdgeDetourClearance=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_EDGE_DETOUR_CLEARANCE ?? "28"}`,
    `optimizedVisualCrossPolishLBend=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_REROUTE ?? "0"}`,
    `optimizedVisualCrossPolishLBendTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_TOPK ?? "160"}`,
    `optimizedVisualCrossPolishLBendMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_MIN_GAIN ?? "1"}`,
    `optimizedVisualCrossPolishLBendEdgeNodeWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_EDGE_NODE_WEIGHT ?? "1"}`,
    `optimizedVisualCrossPolishLBendSegmentOverlapWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_SEGMENT_OVERLAP_WEIGHT ?? "1"}`,
    `optimizedVisualCrossPolishLBendSegmentOverlapLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT ?? "0.0001"}`,
    `optimizedVisualCrossPolishLBendLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_LENGTH_WEIGHT ?? "0"}`,
    `optimizedVisualCrossPolishDiagonalRetouch=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH ?? "1"}`,
    `optimizedVisualCrossPolishDiagonalRetouchTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_TOPK ?? "160"}`,
    `optimizedVisualCrossPolishDiagonalRetouchSegmentsPerEdge=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_SEGMENTS_PER_EDGE ?? "3"}`,
    `optimizedVisualCrossPolishDiagonalRetouchRounds=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_ROUNDS ?? "2"}`,
    `optimizedVisualCrossPolishDiagonalRetouchMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_MIN_GAIN ?? "1"}`,
    `optimizedVisualCrossPolishDiagonalRetouchMinSpan=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_MIN_SPAN ?? "80"}`,
    `optimizedVisualCrossPolishDiagonalRetouchEdgeNodeWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_EDGE_NODE_WEIGHT ?? "1"}`,
    `optimizedVisualCrossPolishDiagonalRetouchSegmentOverlapWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_WEIGHT ?? "1"}`,
    `optimizedVisualCrossPolishDiagonalRetouchSegmentOverlapLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_SEGMENT_OVERLAP_LENGTH_WEIGHT ?? "0.0001"}`,
    `optimizedVisualCrossPolishDiagonalRetouchLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_LENGTH_WEIGHT ?? "0"}`,
    `optimizedVisualCrossPolishDiagonalRetouchAllowNodeHitDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_ALLOW_NODE_HIT_DEBT ?? process.env.DJERD_DIAGONAL_RETOUCH_ALLOW_NODE_HIT_DEBT ?? "0"}`,
    `optimizedVisualCrossPolishDiagonalRetouchBundleObstacles=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_BUNDLE_OBSTACLES ?? process.env.DJERD_DIAGONAL_RETOUCH_BUNDLE_OBSTACLES ?? "1"}`,
    `optimizedVisualCrossPolishDiagonalRetouchTwoBend=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_DIAGONAL_RETOUCH_TWO_BEND ?? process.env.DJERD_DIAGONAL_RETOUCH_TWO_BEND ?? "1"}`,
    "optimizedVisualCrossPolishRouteClearTightClearance=16",
    "optimizedVisualCrossPolishRouteClearWideClearance=44",
    "optimizedVisualCrossPolishRouteClearShortLengthWeight=0.03",
    "optimizedVisualCrossPolishRouteClearShortMaxOffset=360",
    "optimizedVisualCrossPolishRouteClearDeepPasses=2",
    "optimizedVisualCrossPolishRouteClearDeepIters=4",
    "optimizedVisualCrossPolishRouteClearDeepTopK=20",
    "optimizedVisualCrossPolishRouteClearDeepMaxOffset=720",
    `optimizedVisualCrossPolishRouteClearLBend=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_REROUTE ?? "1"}`,
    `optimizedVisualCrossPolishRouteClearLBendTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_TOPK ?? "240"}`,
    `optimizedVisualCrossPolishRouteClearLBendMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_L_BEND_MIN_GAIN ?? "1"}`,
    "optimizedVisualCrossPolishRouteClearDeepLBend=route-clear-deep+lbend",
    `optimizedVisualCrossPolishPeripheryReroute=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_REROUTE ?? "1"}`,
    `optimizedVisualCrossPolishPeripheryTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_TOPK ?? "120"}`,
    `optimizedVisualCrossPolishPeripheryMaxLanes=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_MAX_LANES ?? "6"}`,
    `optimizedVisualCrossPolishPeripheryEdgeNodeWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_EDGE_NODE_WEIGHT ?? "1"}`,
    `optimizedVisualCrossPolishPeripherySegmentOverlapWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_SEGMENT_OVERLAP_WEIGHT ?? "1"}`,
    `optimizedVisualCrossPolishPeripherySegmentOverlapLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT ?? "0.0001"}`,
    `optimizedVisualCrossPolishPeripheryRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_ROUTE_BBOX_GROWTH_LIMIT ?? "1.57"}`,
    `optimizedVisualCrossPolishPeripheryMaxRouteBboxDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_MAX_ROUTE_BBOX_DEBT_PER_GAIN ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_ROUTE_BBOX_DEBT_PER_GAIN ?? "0.05"}`,
    `optimizedVisualCrossPolishNoPdKnot=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NO_PD_KNOT ?? "1"}`,
    `optimizedVisualCrossPolishVisualKnot=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_VISUAL_KNOT ?? "1"}`,
    `optimizedVisualCrossPolishVisualKnotIters=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_VISUAL_KNOT_ITERS ?? "2"}`,
    `optimizedVisualCrossFinalRetouch=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH ?? "1"}`,
    `optimizedVisualCrossFinalRetouchRestoreLayout=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_RESTORE_LAYOUT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MIN_GAIN ?? "1"}`,
    `optimizedVisualCrossFinalRetouchMaxSpacingDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SPACING_DEBT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchMaxSpacingDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SPACING_DEBT_PER_GAIN ?? "0"}`,
    `optimizedVisualCrossFinalRetouchMaxOverlappingEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_OVERLAPPING_EDGE_DEBT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchMaxOverlappingEdgeDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_OVERLAPPING_EDGE_DEBT_PER_GAIN ?? "0.5"}`,
    `optimizedVisualCrossFinalRetouchMaxSegmentOverlapDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SEGMENT_OVERLAP_DEBT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchMaxSegmentOverlapDebtPerGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SEGMENT_OVERLAP_DEBT_PER_GAIN ?? "0.5"}`,
    `optimizedVisualCrossFinalRetouchNodeBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_BBOX_GROWTH_LIMIT ?? "1.01"}`,
    `optimizedVisualCrossFinalRetouchRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_BBOX_GROWTH_LIMIT ?? "1.01"}`,
    `optimizedVisualCrossFinalRetouchTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_TIMEOUT_MS ?? "180000"}`,
    `optimizedVisualCrossFinalRetouchRouteSyncGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_SYNC_GAP ?? "1000000000"}`,
    `optimizedVisualCrossFinalRetouchNodePairTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_TOPK ?? process.env.DJERD_NODE_PAIR_RETOUCH_TOPK ?? "2048"}`,
    `optimizedVisualCrossFinalRetouchNodePairRounds=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_ROUNDS ?? process.env.DJERD_NODE_PAIR_RETOUCH_ROUNDS ?? "10"}`,
    `optimizedVisualCrossFinalRetouchNodePairSteps=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_STEPS ?? process.env.DJERD_NODE_PAIR_RETOUCH_STEPS ?? "8"}`,
    `optimizedVisualCrossFinalRetouchNodePairMinSpan=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_MIN_SPAN ?? process.env.DJERD_NODE_PAIR_RETOUCH_MIN_SPAN ?? "700"}`,
    `optimizedVisualCrossFinalRetouchNodePairLeafMinSpan=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_MIN_SPAN ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_MIN_SPAN ?? process.env.DJERD_NODE_PAIR_RETOUCH_MIN_SPAN ?? "450"}`,
    `optimizedVisualCrossFinalRetouchNodePairStep=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_STEP ?? process.env.DJERD_NODE_PAIR_RETOUCH_STEP ?? "240"}`,
    `optimizedVisualCrossFinalRetouchNodePairMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_MAX_SHIFT ?? process.env.DJERD_NODE_PAIR_RETOUCH_MAX_SHIFT ?? "4200"}`,
    `optimizedVisualCrossFinalRetouchNodePairMaxIncident=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_MAX_INCIDENT ?? process.env.DJERD_NODE_PAIR_RETOUCH_MAX_INCIDENT ?? "64"}`,
    `optimizedVisualCrossFinalRetouchNodePairLeafGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_GAP ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_GAP ?? "16"}`,
    `optimizedVisualCrossFinalRetouchNodePairSlotRings=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_SLOT_RINGS ?? process.env.DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS ?? "12"}`,
    `optimizedVisualCrossFinalRetouchNodePairPairGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_PAIR_GAP ?? process.env.DJERD_NODE_PAIR_RETOUCH_PAIR_GAP ?? "16"}`,
    `optimizedVisualCrossFinalRetouchNodePairLeafOnly=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_ONLY ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_ONLY ?? "1"}`,
    `optimizedVisualCrossFinalRetouchNodePairCompactRelocate=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_COMPACT_RELOCATE ?? process.env.DJERD_NODE_PAIR_RETOUCH_COMPACT_RELOCATE ?? "0"}`,
    `optimizedVisualCrossFinalRetouchLBend=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND ?? "1"}`,
    `optimizedVisualCrossFinalRetouchLBendTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_TOPK ?? "960"}`,
    `optimizedVisualCrossFinalRetouchLBendEdgeNodeWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_EDGE_NODE_WEIGHT ?? "4"}`,
    `optimizedVisualCrossFinalRetouchLBendSegmentOverlapWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_SEGMENT_OVERLAP_WEIGHT ?? "4"}`,
    `optimizedVisualCrossFinalRetouchLBendLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_L_BEND_LENGTH_WEIGHT ?? "0.0001"}`,
    `optimizedVisualCrossFinalRetouchRouteSegments=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_SEGMENTS ?? process.env.DJERD_DIAGONAL_RETOUCH_ROUTE_SEGMENTS ?? "0"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetry=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY ?? "1"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetryNodePairTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_NODE_PAIR_TOPK ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_TOPK ?? process.env.DJERD_NODE_PAIR_RETOUCH_TOPK ?? "768"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetrySlotRings=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_SLOT_RINGS ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_SLOT_RINGS ?? process.env.DJERD_NODE_PAIR_RETOUCH_SLOT_RINGS ?? "6"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetryNodeMargin=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_NODE_MARGIN ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_NODE_MARGIN ?? process.env.DJERD_NODE_PAIR_RETOUCH_NODE_MARGIN ?? "24"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetryLeafGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_LEAF_GAP ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_LEAF_GAP ?? process.env.DJERD_NODE_PAIR_RETOUCH_LEAF_GAP ?? "72"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetryPairGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_PAIR_GAP ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_PAIR_PAIR_GAP ?? process.env.DJERD_NODE_PAIR_RETOUCH_PAIR_GAP ?? "72"}`,
    `optimizedVisualCrossFinalRetouchClearanceRetryTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_TIMEOUT_MS ?? "120000"}`,
    `optimizedVisualCrossFinalRetouchDebtRepair=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairMaxBundleNodeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_MAX_BUNDLE_NODE_DEBT ?? "8"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairMaxOverlappingEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_MAX_OVERLAPPING_EDGE_DEBT ?? "18"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairMaxSegmentOverlapDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_MAX_SEGMENT_OVERLAP_DEBT ?? "12"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairSalvageAccept=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairSalvageAcceptMinVisualGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT_MIN_VISUAL_GAIN ?? "80"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairSalvageAcceptNodeBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT_NODE_BBOX_GROWTH_LIMIT ?? "1.35"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairSalvageAcceptRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT_ROUTE_BBOX_GROWTH_LIMIT ?? "1.20"}`,
    `optimizedVisualCrossFinalRetouchSalvage=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE ?? "1"}`,
    `optimizedVisualCrossFinalRetouchSalvageMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_MIN_GAIN ?? "40"}`,
    `optimizedVisualCrossFinalRetouchSalvageRepairMaxBundleNodeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_MAX_BUNDLE_NODE_DEBT ?? "16"}`,
    `optimizedVisualCrossFinalRetouchSalvageRepairMaxOverlappingEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_MAX_OVERLAPPING_EDGE_DEBT ?? "12"}`,
    `optimizedVisualCrossFinalRetouchSalvageRepairMaxSegmentOverlapDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_MAX_SEGMENT_OVERLAP_DEBT ?? "8"}`,
    `optimizedVisualCrossFinalRetouchSalvageRepairNodeBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_NODE_BBOX_GROWTH_LIMIT ?? "1.35"}`,
    `optimizedVisualCrossFinalRetouchSalvageRepairRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_ROUTE_BBOX_GROWTH_LIMIT ?? "1.08"}`,
    `optimizedVisualCrossFinalRetouchSalvageAcceptMaxBundleNodeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_ACCEPT_MAX_BUNDLE_NODE_DEBT ?? "2"}`,
    `optimizedVisualCrossFinalRetouchSalvageAcceptMaxOverlappingEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_ACCEPT_MAX_OVERLAPPING_EDGE_DEBT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchSalvageAcceptMaxSegmentOverlapDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_ACCEPT_MAX_SEGMENT_OVERLAP_DEBT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchCrossRepair=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMinEdgeCrossGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MIN_EDGE_CROSS_GAIN ?? "80"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMinRawRouteGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MIN_RAW_ROUTE_GAIN ?? "200"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMaxVisualDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_VISUAL_DEBT ?? "96"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMaxEdgeNodeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_EDGE_NODE_DEBT ?? "220"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMaxBundleEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_BUNDLE_EDGE_DEBT ?? "16"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMaxBundleNodeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_BUNDLE_NODE_DEBT ?? "8"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMaxOverlappingEdgeDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_OVERLAPPING_EDGE_DEBT ?? "8"}`,
    `optimizedVisualCrossFinalRetouchCrossRepairMaxSegmentOverlapDebt=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_SEGMENT_OVERLAP_DEBT ?? "4"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_TIMEOUT_MS ?? "120000"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairRelief=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairReliefPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_PASSES ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_PASSES ?? "4"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairReliefMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_MAX_SHIFT ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_MAX_SHIFT ?? "220"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairReliefStrength=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_STRENGTH ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_STRENGTH ?? "0.95"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairReliefEndpoints=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_ENDPOINTS ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINTS ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairReliefEndpointTop=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_ENDPOINT_TOP ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_TOP ?? "80"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairReliefEndpointSteps=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_RELIEF_ENDPOINT_STEPS ?? process.env.DJERD_NODE_EDGE_RELIEF_FINAL_ENDPOINT_STEPS ?? "80,160,300,600"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairEdgeDetour=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_EDGE_DETOUR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairLBend=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairLBendMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_L_BEND_MIN_GAIN ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBundleClear=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_CLEAR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBundleAfterClear=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BUNDLE_AFTER_CLEAR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBboxAxisScale=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_AXIS_SCALE ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBboxYScales=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_Y_SCALES ?? "0.98,0.95,0.92,0.88,0.84"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBboxMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_MIN_GAIN ?? "0.01"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBboxVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_VISUAL_SLACK ?? "64"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBboxBundleNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_BUNDLE_NODE_SLACK ?? "2"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairBboxMaxAspect=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_BBOX_MAX_ASPECT ?? "2.4"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackScales=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_SCALES ?? "0.94,0.90,0.86,0.82,0.78,0.72,0.68"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_MIN_GAIN ?? "0.03"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackTop=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_TOP ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackEmptyBandKeep=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_EMPTY_BAND_KEEP ?? "480"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_VISUAL_SLACK ?? "64"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_EDGE_NODE_SLACK ?? "96"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackSpacingSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_SPACING_SLACK ?? "83"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackBundleNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_BUNDLE_NODE_SLACK ?? "2"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackNodeOverlapSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_NODE_OVERLAP_SLACK ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairDensityPackCleanup=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_DENSITY_PACK_CLEANUP ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairSpacingRepair=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SPACING_REPAIR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairSpacingRepairTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SPACING_REPAIR_TIMEOUT_MS ?? process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_TIMEOUT_MS ?? "120000"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridor=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorMinAdditionalVisualGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_MIN_ADDITIONAL_VISUAL_GAIN ?? "32"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodeBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_BBOX_GROWTH_LIMIT ?? "1.45"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorRouteBboxGrowth=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_ROUTE_BBOX_GROWTH_LIMIT ?? "1.35"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_TIMEOUT_MS ?? "120000"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorRestoreLayout=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_RESTORE_LAYOUT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBend=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBendTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_TOPK ?? "960"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBendMinGain=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_MIN_GAIN ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBendEdgeNodeWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_EDGE_NODE_WEIGHT ?? "6"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBendSegmentOverlapWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_SEGMENT_OVERLAP_WEIGHT ?? "12"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBendSegmentOverlapLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_SEGMENT_OVERLAP_LENGTH_WEIGHT ?? "0.001"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorLBendLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_L_BEND_LENGTH_WEIGHT ?? "0.0001"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorPeriphery=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorPeripheryTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_TOPK ?? "96"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorPeripheryMaxLanes=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_MAX_LANES ?? "4"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorPeripheryEdgeNodeWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_EDGE_NODE_WEIGHT ?? "6"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorPeripherySegmentOverlapWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_SEGMENT_OVERLAP_WEIGHT ?? "12"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorPeripherySegmentOverlapLengthWeight=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_PERIPHERY_SEGMENT_OVERLAP_LENGTH_WEIGHT ?? "0.001"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorDiagonalRetouch=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_DIAGONAL_RETOUCH ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairRetouch=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_RETOUCH ?? "1"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairTopK=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_TOPK ?? "768"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairRounds=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_ROUNDS ?? "4"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairSteps=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_STEPS ?? "4"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairMinSpan=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_MIN_SPAN ?? "420"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairLeafMinSpan=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_LEAF_MIN_SPAN ?? "360"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairStep=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_STEP ?? "180"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_MAX_SHIFT ?? "2400"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairMaxIncident=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_MAX_INCIDENT ?? "512"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairNodeMargin=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_NODE_MARGIN ?? "16"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairLeafOnly=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_LEAF_ONLY ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairLeafGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_LEAF_GAP ?? "48"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairPairGap=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_PAIR_GAP ?? "48"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairRawAccept=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_RAW_ACCEPT ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairCompactRelocate=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_COMPACT_RELOCATE ?? "0"}`,
    `optimizedVisualCrossFinalRetouchDebtRepairHubCorridorNodePairSlotRings=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_PAIR_SLOT_RINGS ?? "8"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepair=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR ?? "1"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairPasses=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_PASSES ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_PASSES ?? "8"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairMaxShift=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_MAX_SHIFT ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_MAX_SHIFT ?? "480"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairExtra=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_EXTRA ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EXTRA ?? "12"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairVisualSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_VISUAL_SLACK ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_VISUAL_SLACK ?? "24"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairEdgeNodeSlack=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_EDGE_NODE_SLACK ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_EDGE_NODE_SLACK ?? "24"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairBboxLimit=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_BBOX_LIMIT ?? process.env.DJERD_NODE_SPACING_CLEAR_FINAL_BBOX_LIMIT ?? "1.01"}`,
    `optimizedVisualCrossFinalRetouchSpacingRepairTimeoutMs=${process.env.DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_TIMEOUT_MS ?? "120000"}`,
  ];
}

export interface OgdfLayoutResult {
  applied: boolean;
  durationMs: number;
  layout: LayoutSnapshot;
  engineMetadata?: LayoutEngineMetadata;
  qualityDegraded?: boolean;
  reason?: string;
  requestedLayoutMode: LayoutMode;
}

/**
 * Intermediate multistart layout, streamed while the cluster_graph binary is
 * still running (one frame per new-best seed). `positions` is modelId →
 * [topLeftX, topLeftY] (same convention as the final layout JSON node
 * positions). Pre-route, so the webview previews it with straight edges.
 */
export interface OgdfProgressFrame {
  // From the C++ multistart side-channel (pre-route). Absent for the later
  // ML-pipeline stages, which carry `stage` instead.
  run?: number;
  seed?: number;
  crossings?: number;
  // ML-pipeline stage label ("reroute" | "bbox" | "polish") for the routed
  // intermediates streamed from the extension.
  stage?: string;
  positions: Record<string, [number, number]>;
}

// Optional sink for intermediate layout frames. The ERD panel registers its
// `webview.postMessage` here around a re-layout (the webview is alive during
// the refresh), so we avoid threading an onProgress callback through the whole
// openDiagram → relayout → applyRequestedLayout → runOgdfLayout chain. Only one
// diagram lays out at a time, so a single module-level slot is sufficient; the
// panel clears it when the layout finishes.
let ogdfProgressListener: ((frame: OgdfProgressFrame) => void) | undefined;

export function setOgdfProgressListener(
  listener: ((frame: OgdfProgressFrame) => void) | undefined,
): void {
  ogdfProgressListener = listener;
}

// Stream one ML-pipeline intermediate (a fully-parsed layout JSON) to the
// webview as a positions preview, so the user sees each long stage (reroute,
// each bbox-target candidate, polish) land instead of waiting for the whole
// pipeline. No-op when no listener is registered. The node positions are
// top-left (same convention the webview treats as basePosition); the webview
// previews them with straight edges and snaps to the routed final on reload.
function streamIntermediateLayout(stage: string, layout: unknown): void {
  if (!ogdfProgressListener) return;
  const nodes = (
    layout as { nodes?: Array<{ modelId?: unknown; position?: { x?: unknown; y?: unknown } }> }
  )?.nodes;
  if (!Array.isArray(nodes)) return;
  const positions: Record<string, [number, number]> = {};
  for (const node of nodes) {
    if (typeof node?.modelId === "string" && node.position) {
      positions[node.modelId] = [
        Number(node.position.x) || 0,
        Number(node.position.y) || 0,
      ];
    }
  }
  if (Object.keys(positions).length === 0) return;
  ogdfProgressListener({ stage, positions });
}

export async function runOgdfLayout(
  extensionRootPath: string,
  payload: DiagramBootstrapPayload,
  requestedLayoutMode: LayoutMode,
  logger?: Logger,
  requestId?: number,
  edgeRouting: EdgeRoutingStyle = DEFAULT_EDGE_ROUTING,
  clusterGraphLayout: boolean = false,
  bubbleLayout: boolean = false,
  optimizedLayout: boolean = false,
): Promise<OgdfLayoutResult> {
  const envEdgeRouting = process.env.DJANGO_ERD_EDGE_ROUTING;
  const envEdgeRoutingValid =
    envEdgeRouting === "straight"
    || envEdgeRouting === "straight_smart"
    || envEdgeRouting === "orthogonal";
  const effectiveEdgeRouting: EdgeRoutingStyle = envEdgeRoutingValid
    ? (envEdgeRouting as EdgeRoutingStyle)
    : edgeRouting;
  const started = Date.now();
  const binaryPath = await resolveOgdfLayoutBinaryPath(extensionRootPath);
  const normalizedRequestedLayoutMode = normalizeLayoutMode(requestedLayoutMode);
  const layoutDefinition = getOgdfLayoutDefinition(normalizedRequestedLayoutMode);

  if (!binaryPath) {
    const reason =
      `no native OGDF binary for ${process.platform}-${process.arch}`;
    logger?.warn(
      [
        "OGDF layout skipped because no native binary was found",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `layout=${normalizedRequestedLayoutMode}`,
        `label=${layoutDefinition.label}`,
        `ogdfClass=${layoutDefinition.ogdfClass}`,
        `platform=${process.platform}`,
        `arch=${process.arch}`,
      ].join(" · "),
    );
    return {
      applied: false,
      durationMs: Date.now() - started,
      layout: payload.layout,
      reason,
      requestedLayoutMode: normalizedRequestedLayoutMode,
    };
  }

  const binaryFingerprint = await fileStatFingerprint(binaryPath);

  const requestDirectory = await mkdtemp(
    path.join(os.tmpdir(), `django-erd-ogdf-${normalizedRequestedLayoutMode}-`),
  );
  const nodesPath = path.join(requestDirectory, "nodes.tsv");
  const edgesPath = path.join(requestDirectory, "edges.tsv");
  const transientPaths = new Set<string>();
  const trackTransientPath = (filePath: string): string => {
    transientPaths.add(filePath);
    return filePath;
  };
  const preserveInputs = readBoolEnv(
    "DJANGO_ERD_PRESERVE_LAYOUT_INPUTS",
    false,
  );
  let preserveRequestDirectory = preserveInputs;
  let releaseOptimizedLayoutFlight: (() => void) | undefined;

  // Edge consolidation (DJERD_CONSOLIDATE_EDGES=1, default off):
  // group multiple edges between the same (source, target) directional
  // pair into a single representative edge before sending to the OGDF
  // binary. The representative carries the first underlying edge's id;
  // the remaining edges have no route in the layout output (they will
  // share the representative's visual line in the webview when render-
  // side badges are added later).
  const consolidateEnv = process.env.DJERD_CONSOLIDATE_EDGES;
  const consolidateActive =
    consolidateEnv !== undefined && consolidateEnv !== "0";
  const layoutEdges: readonly StructuralGraphEdge[] = consolidateActive
    ? consolidateEdges(payload.graph.structuralEdges).layoutEdges
    : payload.graph.structuralEdges;
  const selfLoopEdgeCount = layoutEdges.filter(
    (edge) => edge.sourceModelId === edge.targetModelId,
  ).length;

  try {
    await writeFile(nodesPath, serializeNodes(payload), "utf8");
    await writeFile(edgesPath, serializeEdges(layoutEdges), "utf8");

    if (preserveInputs) {
      logger?.info(
        `OGDF layout inputs preserved at ${requestDirectory} (DJANGO_ERD_PRESERVE_LAYOUT_INPUTS=${process.env.DJANGO_ERD_PRESERVE_LAYOUT_INPUTS})`,
      );
    }

    logger?.info(
      [
        "OGDF layout starting",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `binary=${binaryPath}`,
        `layout=${normalizedRequestedLayoutMode}`,
        `label=${layoutDefinition.label}`,
        `family=${layoutDefinition.family}`,
        `ogdfClass=${layoutDefinition.ogdfClass}`,
        `edgeRouting=${effectiveEdgeRouting}`,
        `edgeRoutingSource=${envEdgeRoutingValid ? "env(DJANGO_ERD_EDGE_ROUTING)" : "default"}`,
        `nodes=${payload.layout.nodes.length}`,
        `edges=${layoutEdges.length}`
          + (consolidateActive
            ? ` (consolidated from ${payload.graph.structuralEdges.length})`
            : ""),
        `selfLoopEdges=${selfLoopEdgeCount}`,
      ].join(" · "),
    );

    // ML polish round-trip:
    //   DJERD_LAYOUT_FROM_FILE=<path>   load layout JSON from disk, skip
    //                                   C++. Used to visually verify
    //                                   ML-polished output. If file
    //                                   doesn't exist, falls through to
    //                                   normal C++ run (so first launch
    //                                   can populate it via
    //                                   DJERD_LAYOUT_OUTPUT_FILE).
    //   DJERD_LAYOUT_OUTPUT_FILE=<path> save C++ output JSON to disk
    //                                   alongside normal pipeline. Used
    //                                   to capture input for the polish
    //                                   round-trip.
    // Optimized toggle: run the v36 pure action scorer pipeline live
    // (+ C++ reroute). To skip the live pipeline and load a precomputed
    // result instead, set DJERD_OPTIMIZED_LAYOUT_FILE explicitly.
    const optimizedFilePath = optimizedLayout
      ? process.env.DJERD_OPTIMIZED_LAYOUT_FILE
      : undefined;
    const layoutFromFile = optimizedFilePath
      ?? process.env.DJERD_LAYOUT_FROM_FILE;
    const layoutOutputFile = process.env.DJERD_LAYOUT_OUTPUT_FILE;
    let stdout = "";
    let stderr = "";
    let loadedFromFile = false;
    let optimizedCachePath: string | undefined;
    let loadedOptimizedFinalFromCache = false;
    let postReroutePolishDeadline: PostReroutePolishDeadline | undefined;
    if (layoutFromFile) {
      try {
        stdout = await readFile(layoutFromFile, "utf8");
        stderr = "";
        loadedFromFile = true;
        logger?.info(
          `OGDF layout loaded from file${optimizedFilePath ? " [optimized toggle]" : ""}: ${layoutFromFile}`,
        );
      } catch {
        logger?.warn(
          `OGDF layout file not found, running C++ binary: ${layoutFromFile}`,
        );
      }
    }
    const precomputedOptimizedPositions =
      process.env.DJERD_OPTIMIZED_POSITIONS_TSV?.trim();
    if (
      !loadedFromFile
      && optimizedLayout
      && !layoutOutputFile
      && !precomputedOptimizedPositions
      && readBoolEnv("DJERD_OPTIMIZED_LAYOUT_CACHE", true)
    ) {
      try {
        const nodesData = await readFile(nodesPath, "utf8");
        const edgesData = await readFile(edgesPath, "utf8");
        const ckptPathForCache = resolveV36CkptPath(extensionRootPath);
        const familyPriorPathForCache =
          resolveV37FamilyPriorPath(extensionRootPath);
        const scorerScriptPathForCache = path.join(
          extensionRootPath,
          "scripts/erd-poc/eval_v35_scorer_filter.py",
        );
        const [
          ckptFingerprint,
          familyPriorFingerprint,
          scorerScriptFingerprint,
        ] = await Promise.all([
          fileStatFingerprint(ckptPathForCache),
          fileStatFingerprint(familyPriorPathForCache),
          fileStatFingerprint(scorerScriptPathForCache),
        ]);
        const key = fnvHash(
          nodesData,
          edgesData,
          "optimized-layout-cache-v11",
          `binary=${binaryFingerprint}`,
          `scorer=${scorerScriptFingerprint}`,
          `checkpoint=${ckptFingerprint}`,
          `familyPrior=${familyPriorFingerprint}`,
          `requested=${normalizedRequestedLayoutMode}`,
          `edgeRouting=${effectiveEdgeRouting}`,
          `clusterGraph=${clusterGraphLayout ? "1" : "0"}`,
          `bubble=${bubbleLayout ? "1" : "0"}`,
          ...renderedCarrierCacheKeyParts(),
          ...layoutEnvCacheKeyParts(ogdfRenderedCarrierEnv()),
          ...layoutEnvCacheKeyParts(ogdfOptimizedRerouteEnv()),
          ...buildV36ScorerArgs(
            "eval_v35_scorer_filter.py",
            "baseline.json",
            "baseline-positions.tsv",
            "positions.tsv",
            ckptPathForCache,
            familyPriorPathForCache,
          ).map((arg, index) => `v36Arg${index}=${arg}`),
        );
        optimizedCachePath = path.join(
          os.tmpdir(),
          `django-erd-optimized-layout-cache-${key}.json`,
        );
        const flight = await acquireOptimizedLayoutFlight(
          optimizedCachePath,
          () => logger?.info(
            "OGDF optimized layout joining in-flight execution"
            + ` · requestId=${requestId ?? "absent"}`
            + ` · cache=${optimizedCachePath}`,
          ),
        );
        releaseOptimizedLayoutFlight = flight.release;
        if (flight.waited) {
          logger?.info(
            "OGDF optimized layout in-flight execution completed; "
            + `rechecking cache · requestId=${requestId ?? "absent"}`
            + ` · cache=${optimizedCachePath}`,
          );
        }
        try {
          const cachedLayout = await readFile(optimizedCachePath, "utf8");
          decodeLayoutSnapshot(
            JSON.parse(cachedLayout),
            "ogdfOptimizedLayoutCache",
          );
          stdout = cachedLayout;
          stderr = "";
          loadedFromFile = true;
          loadedOptimizedFinalFromCache = true;
          logger?.info(`OGDF optimized layout cache hit: ${optimizedCachePath}`);
        } catch (error) {
          const reason = error instanceof Error ? error.message : String(error);
          logger?.info(
            `OGDF optimized layout cache miss or invalid; `
            + `will save to ${optimizedCachePath} · reason=${reason}`,
          );
        }
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        logger?.warn(`OGDF optimized layout cache lookup failed: ${msg}`);
        optimizedCachePath = undefined;
      }
    }
    // Layout cache: hash(nodes.tsv + edges.tsv + mode + flags) → cached
    // layout JSON. cluster_graph fallback on 1000+ nodes takes 5+ min;
    // caching makes a second toggle/reload instant.
    let cachePath: string | undefined;
    if (!loadedFromFile && !optimizedLayout) {
      try {
        const nodesData = await readFile(nodesPath, "utf8");
        const edgesData = await readFile(edgesPath, "utf8");
        const key = fnvHash(
          nodesData,
          edgesData,
          "layout-cache-v2",
          `binary=${binaryFingerprint}`,
          normalizedRequestedLayoutMode,
          effectiveEdgeRouting,
          clusterGraphLayout ? "cg=1" : "cg=0",
          bubbleLayout ? "b=1" : "b=0",
          ...renderedCarrierCacheKeyParts(),
          ...layoutEnvCacheKeyParts(ogdfRenderedCarrierEnv()),
        );
        cachePath = path.join(os.tmpdir(), `django-erd-layout-cache-${key}.json`);
        try {
          const cachedLayout = await readFile(cachePath, "utf8");
          decodeLayoutSnapshot(JSON.parse(cachedLayout), "ogdfLayoutCache");
          stdout = cachedLayout;
          stderr = "";
          loadedFromFile = true;
          logger?.info(`OGDF layout cache hit: ${cachePath}`);
        } catch (error) {
          const reason = error instanceof Error ? error.message : String(error);
          logger?.info(
            `OGDF layout cache miss or invalid; will save to ${cachePath} · `
            + `reason=${reason}`,
          );
        }
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        logger?.warn(`OGDF layout cache lookup failed: ${msg}`);
        cachePath = undefined;
      }
    }
    if (!loadedFromFile) {
      // Progressive rendering: when a caller wants intermediate frames, ask
      // the binary to dump each multistart new-best to a temp file
      // (DJERD_PROGRESS_FILE) and poll it while execFileAsync runs (the call
      // is buffered, so we can't read its stream — the side-channel file is
      // the bridge). The binary writes atomically (.tmp + rename) so each read
      // sees a complete JSON. Only the baseline call runs multistart, so this
      // is the only call that needs it.
      const progressSink = ogdfProgressListener;
      let progressTimer: ReturnType<typeof setInterval> | undefined;
      const progressPath = progressSink
        ? path.join(
            os.tmpdir(),
            `django-erd-progress-${requestId ?? 0}-${Date.now()}.json`,
          )
        : undefined;
      if (progressSink && progressPath) {
        let lastProgress = "";
        progressTimer = setInterval(() => {
          void readFile(progressPath, "utf8")
            .then((content) => {
              if (!content || content === lastProgress) return;
              lastProgress = content;
              try {
                progressSink(JSON.parse(content) as OgdfProgressFrame);
              } catch {
                // Mid-rename partial read — ignore; next poll catches it.
              }
            })
            .catch(() => {
              // File not created yet (before the first new-best) — ignore.
            });
        }, 150);
      }
      try {
        ({ stderr, stdout } = await execFileAsync(
          binaryPath,
          [
            "layout",
            "--mode",
            normalizedRequestedLayoutMode,
            "--nodes-file",
            nodesPath,
            "--edges-file",
            edgesPath,
            "--edge-routing",
            effectiveEdgeRouting,
            ...(clusterGraphLayout ? ["--cluster-graph", "1"] : []),
            ...(bubbleLayout ? ["--bubble", "1"] : []),
          ],
          {
            cwd: extensionRootPath,
            env: ogdfRenderedCarrierEnv(
              progressPath ? { DJERD_PROGRESS_FILE: progressPath } : {},
            ),
            maxBuffer: 100 * 1024 * 1024,
            timeout: OGDF_LAYOUT_TIMEOUT_MS,
          },
        ));
      } finally {
        if (progressTimer) clearInterval(progressTimer);
        if (progressPath) void rm(progressPath, { force: true }).catch(() => {});
      }
      if (layoutOutputFile) {
        try {
          await writeFile(layoutOutputFile, stdout, "utf8");
          logger?.info(`OGDF layout output saved to: ${layoutOutputFile}`);
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          logger?.warn(`OGDF layout output save failed: ${msg}`);
        }
      }
      if (cachePath) {
        try {
          decodeLayoutSnapshot(JSON.parse(stdout), "ogdfLayoutCacheWrite");
          await writeFile(cachePath, stdout, "utf8");
          logger?.info(`OGDF layout cached to ${cachePath}`);
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          logger?.warn(`OGDF layout cache save failed: ${msg}`);
        }
      }
    }

    if (stderr.trim().length > 0) {
      logger?.info(`OGDF stderr: ${stderr.trim()}`);
    }

    // === Optimized layout: v36 pure action scorer pipeline ===
    // 1. cluster_graph baseline (always, regardless of the user's layout
    //    mode dropdown) gives the action scorer a stable clustered state.
    // 2. Python `eval_v35_scorer_filter.py` lets the v36 ranker shortlist
    //    candidate actions, then exact-measures the shortlist before each
    //    accepted move. It writes only the accepted node positions to TSV.
    // 3. C++ re-routes with `--rigid-positions` so the exact-verified
    //    positions drive the final visual layout.
    // DJERD_OPTIMIZED_POSITIONS_TSV can point at a precomputed TSV for
    // visual inspection while keeping the same C++ reroute/measurement path.
    // Runtime defaults use a fast one-round profile; quality experiments can
    // still override the DJERD_V35_* env knobs.
    // The Y-axis 2-pass fallback runs only if v36 optimization fails.
    if (optimizedLayout && !loadedFromFile) {
      // Force a cluster_graph baseline so ML inference receives the
      // distribution it was trained on. If the user-requested mode was
      // something else (fmmm, sifting, …), the visible result still uses
      // the ML positions but the input layout matches training.
      if (!clusterGraphLayout) {
        try {
          logger?.info(
            "[ML] forcing cluster_graph baseline (overrides user layout mode for inference)",
          );
          // ML training baseline was generated with the launch.json env
          // (post-pass DISABLED + cluster fallback DISABLED), giving a
          // tight pos_std ~16k. Inheriting that same env here keeps the
          // captain baseline at the same scale; previously we forced
          // post-passes ON, which re-enabled ClusterGraphLayout(FMMM)
          // fallback and blew pos_std to 112k → 7× spread on ML output.
          // DJERD_SKIP_CG_OPT=1 skips §13-cluster-swap / §14-chain /
          // §15-pruned-cross-min — these only adjust positions to reduce
          // crossings. For an ML inference baseline we only need cluster
          // structure (memberships, leaf bundles); the §13/14/15 passes
          // re-run anyway in the first reroute. Saves ~125s on captain.
          const clusterBaselineEnv = ogdfRenderedCarrierEnv({
            DJERD_SKIP_CG_OPT: "1",
          });
          let clusterBaselineCachePath: string | undefined;
          let baselineLoadedFromCache = false;
          if (readBoolEnv("DJERD_OPTIMIZED_BASELINE_CACHE", true)) {
            try {
              const nodesData = await readFile(nodesPath, "utf8");
              const edgesData = await readFile(edgesPath, "utf8");
              const key = fnvHash(
                nodesData,
                edgesData,
                "optimized-cluster-baseline-cache-v2",
                `binary=${binaryFingerprint}`,
                "mode=hierarchical_barycenter",
                `edgeRouting=${effectiveEdgeRouting}`,
                "clusterGraph=1",
                "skipCgOpt=1",
                ...layoutEnvCacheKeyParts(clusterBaselineEnv),
              );
              clusterBaselineCachePath = path.join(
                os.tmpdir(),
                `django-erd-optimized-baseline-cache-${key}.json`,
              );
              try {
                const cachedBaseline = await readFile(
                  clusterBaselineCachePath,
                  "utf8",
                );
                decodeLayoutSnapshot(
                  JSON.parse(cachedBaseline),
                  "ogdfOptimizedBaselineCache",
                );
                stdout = cachedBaseline;
                baselineLoadedFromCache = true;
                logger?.info(
                  `[ML] cluster_graph baseline cache hit: ${clusterBaselineCachePath}`,
                );
              } catch (error) {
                const reason = error instanceof Error
                  ? error.message
                  : String(error);
                logger?.info(
                  `[ML] cluster_graph baseline cache miss or invalid; `
                  + `will save to ${clusterBaselineCachePath} · reason=${reason}`,
                );
              }
            } catch (err) {
              const msg = err instanceof Error ? err.message : String(err);
              logger?.warn(`[ML] cluster_graph baseline cache lookup failed: ${msg}`);
            }
          }
          if (!baselineLoadedFromCache) {
            const clusterBaseline = await execFileAsync(
              binaryPath,
              [
                "layout",
                "--mode", "hierarchical_barycenter",
                "--nodes-file", nodesPath,
                "--edges-file", edgesPath,
                "--edge-routing", effectiveEdgeRouting,
                "--cluster-graph", "1",
              ],
              {
                cwd: extensionRootPath,
                maxBuffer: 100 * 1024 * 1024,
                timeout: OGDF_LAYOUT_TIMEOUT_MS,
                env: clusterBaselineEnv,
              },
            );
            if (clusterBaseline.stderr.trim().length > 0) {
              logger?.info(`[ML] cluster_graph baseline stderr: ${clusterBaseline.stderr.trim()}`);
            }
            stdout = clusterBaseline.stdout;
            if (clusterBaselineCachePath) {
              try {
                decodeLayoutSnapshot(
                  JSON.parse(stdout),
                  "ogdfOptimizedBaselineCacheWrite",
                );
                await writeFile(clusterBaselineCachePath, stdout, "utf8");
                logger?.info(
                  `[ML] cluster_graph baseline cached to ${clusterBaselineCachePath}`,
                );
              } catch (err) {
                const msg = err instanceof Error ? err.message : String(err);
                logger?.warn(`[ML] cluster_graph baseline cache save failed: ${msg}`);
              }
            }
          }
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          logger?.warn(
            `[ML] cluster_graph baseline failed; using user-mode stdout: ${msg}`,
          );
        }
      }
      const baselinePath = trackTransientPath(path.join(
        os.tmpdir(),
        `django-erd-ml-baseline-${Date.now()}.json`,
      ));
      const positionsPath = trackTransientPath(path.join(
        os.tmpdir(),
        `django-erd-v36-positions-${Date.now()}.tsv`,
      ));
      const baselinePositionsPath = trackTransientPath(path.join(
        os.tmpdir(),
        `django-erd-v36-baseline-positions-${Date.now()}.tsv`,
      ));
      const venvPython = path.join(extensionRootPath, ".venv-ml/bin/python");
      const scorerScript = path.join(
        extensionRootPath,
        "scripts/erd-poc/eval_v35_scorer_filter.py",
      );
      const ckptPath = resolveV36CkptPath(extensionRootPath);
      // v37-diverse.pt: the production default since 2026-05-30. Retrained
      // from v37-multistart-1301-light's predecessor datasets PLUS 15
      // synthetic graphs (n=92–530) to fix a Captain-overfit in the
      // family prior. The light prior (trained on Captain alone) learned to
      // deprioritise the overlap-resolution families
      // (overlap_component_line/ring/batch, louvain_cluster_anchor) because
      // Captain's dense biconnected core rarely needs them; on sparser graphs
      // those families ARE the workhorses, so the light prior left search
      // gains on the table (held-out synth: recovered only ~57% of the
      // exhaustive-search gain, never beat it; see memory
      // [[project-generalization-synth]]).
      //
      // v37-diverse validation (2026-05-30, memory [[project-v37-diverse-prior]]):
      //   - Captain (real-main-1301): IDENTICAL to light (single-round gain 26
      //     both; compare_v37_ckpts 4-start top-5 overlap 4-5/5, #1 family
      //     identical) — zero regression on the production target.
      //   - 16 held-out + 8 fresh synth graphs: matches-or-beats the
      //     exhaustive (prior-off) search (ratio ~1.1 / ~1.0, 0–1 graphs worse,
      //     2–3 better) — the overfit is gone.
      // The diverse prior carries 17 action families (vs light's 12); the 5
      // extra families synth graphs surface stay dormant on Captain.
      //
      // Predecessor v37-multistart-1301-light.pt (visualCross 609, bbox 1.67B,
      // qSub cmp 0.06 on Captain 1304) and the en3 experiment (REVERTED;
      // [[en3-edge-node-failed]]) are kept on disk for record.
      const familyPriorPath = resolveV37FamilyPriorPath(extensionRootPath);
      const precomputedPositionsPath = process.env.DJERD_OPTIMIZED_POSITIONS_TSV;
      let effectivePositionsPath = positionsPath;
      let mlOk = false;
      try {
        await writeFile(baselinePath, stdout, "utf8");
        await writePositionsTsvFromLayoutJson(stdout, baselinePositionsPath);

        if (precomputedPositionsPath && precomputedPositionsPath.trim().length > 0) {
          effectivePositionsPath = precomputedPositionsPath;
          logger?.info(
            `[ML] using precomputed v36 positions: ${effectivePositionsPath}`,
          );
          mlOk = true;
        } else {
          const mlStart = Date.now();
          logger?.info(
            `[ML] running v36 pure action scorer (ckpt=${path.basename(ckptPath).replace(/\.pt$/, "")}, exact-verified shortlist)`,
          );
          const mlArgs = buildV36ScorerArgs(
            scorerScript,
            baselinePath,
            baselinePositionsPath,
            positionsPath,
            ckptPath,
            familyPriorPath,
          );
          // PyTorch / BLAS thread pools — match Apple Silicon performance
          // cores. The exact verifier is NumPy-heavy, so keeping these
          // bounded avoids one refresh monopolizing the whole machine.
          const mlEnv: Record<string, string> = {
            ...(process.env as Record<string, string>),
            OMP_NUM_THREADS: process.env.OMP_NUM_THREADS ?? "8",
            MKL_NUM_THREADS: process.env.MKL_NUM_THREADS ?? "8",
            OPENBLAS_NUM_THREADS: process.env.OPENBLAS_NUM_THREADS ?? "8",
            VECLIB_MAXIMUM_THREADS: process.env.VECLIB_MAXIMUM_THREADS ?? "8",
            NUMEXPR_NUM_THREADS: process.env.NUMEXPR_NUM_THREADS ?? "8",
          };
          const mlRun = await execFileAsync(venvPython, mlArgs, {
            cwd: extensionRootPath,
            maxBuffer: 32 * 1024 * 1024,
            timeout: readPositiveIntEnv("DJERD_V35_TIMEOUT_MS", V35_SCORER_TIMEOUT_MS),
            env: mlEnv,
          } as Parameters<typeof execFileAsync>[2]);
          const mlLines = mlRun.stdout
            .split("\n")
            .filter((line) => line.trim().length > 0)
            .join(" | ");
          logger?.info(
            `[ML] v36 scorer done in ${Date.now() - mlStart}ms · ${mlLines}`,
          );
          if (mlRun.stderr.trim().length > 0) {
            logger?.info(`[ML] v36 python stderr: ${mlRun.stderr.trim()}`);
          }
          mlOk = true;
        }
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        logger?.warn(
          `[ML] v36 optimization failed, falling back to Y-scale 2-pass: ${msg}`,
        );
      }

      if (mlOk) {
        try {
          // v36 emits positions; C++ then refreshes routes, bundles, and
          // rendered metrics. The default post-stack path keeps straight
          // routes but allows the existing knot/leaf/node-edge passes to
          // untangle the answer. Set DJERD_OPTIMIZED_REROUTE_POSTSTACK=0
          // to use the faster rigid-only verifier.
          const usePoststackReroute = readBoolEnv(
            "DJERD_OPTIMIZED_REROUTE_POSTSTACK",
            true,
          );
          logger?.info(
            `[ML] ${usePoststackReroute ? "post-stack" : "rigid"} reroute (v36 positions)`,
          );
          const rerouteStart = Date.now();
          const rerouteArgs = [
            "layout",
            "--mode", normalizedRequestedLayoutMode,
            "--nodes-file", nodesPath,
            "--edges-file", edgesPath,
            "--edge-routing", effectiveEdgeRouting,
            "--cluster-graph", "1",
            "--positions-tsv", effectivePositionsPath,
            ...(usePoststackReroute ? [] : ["--rigid-positions", "1"]),
          ];
          const reroute = await execFileAsync(
            binaryPath,
            rerouteArgs,
            {
              cwd: extensionRootPath,
              env: usePoststackReroute
                ? ogdfOptimizedPoststackEnv()
                : ogdfOptimizedRerouteEnv(),
              maxBuffer: 100 * 1024 * 1024,
              timeout: OGDF_LAYOUT_TIMEOUT_MS,
            },
          );
          if (reroute.stderr.trim().length > 0) {
            logger?.info(`[ML] OGDF stderr: ${reroute.stderr.trim()}`);
          }
          logger?.info(`[ML] reroute done in ${Date.now() - rerouteStart}ms`);
          const reroutedLayout = JSON.parse(reroute.stdout);
          let acceptedStdout = reroute.stdout;
          let acceptedLayout = reroutedLayout;
          const visualPolishBboxCeilingSummary = summarizeLayout(
            decodeLayoutSnapshot(reroutedLayout, "ogdfLayout"),
          );
          const visualPolishNodeBboxCeilingB =
            visualPolishBboxCeilingSummary.nodeBBoxWidth
            * visualPolishBboxCeilingSummary.nodeBBoxHeight
            / 1e9;
          const visualPolishRouteBboxCeilingB =
            visualPolishBboxCeilingSummary.routeBBoxWidth
            * visualPolishBboxCeilingSummary.routeBBoxHeight
            / 1e9;
          // Show the routed reroute result immediately (don't wait for the
          // long bbox-target / polish stages that follow).
          streamIntermediateLayout("reroute", reroutedLayout);

          if (usePoststackReroute) {
            // Strong default target: keep squeezing toward the final export
            // budget while bounded visual/spacing debt gates prevent collapse.
            const bboxTargetB = readFloatEnv(
              "DJERD_OPTIMIZED_BBOX_TARGET_B",
              DEFAULT_OPTIMIZED_BBOX_TARGET_B,
            );
            if (bboxTargetB > 0) {
              const bboxTargetSafeties = readBboxTargetSafeties();
              const bboxTargetVariants = readBboxTargetVariants();
              const bboxTargetPositionStrategies = readBboxTargetPositionStrategies();
              const bboxInitialLayout =
                decodeLayoutSnapshot(acceptedLayout, "ogdfLayout");
              const bboxInitialSummary = summarizeLayout(bboxInitialLayout);
              const bboxInitialAreaB =
                bboxInitialSummary.nodeBBoxWidth
                * bboxInitialSummary.nodeBBoxHeight
                / 1e9;
              const bboxTargetStages = readBboxTargetStages(
                bboxTargetB,
                bboxInitialAreaB,
              );
              logger?.info(
                `[bbox target] stages=${bboxTargetStages
                  .map((stage) => `${stage.toFixed(2)}B`)
                  .join(",")} · current=${bboxInitialAreaB.toFixed(2)}B · `
                + `final=${bboxTargetB.toFixed(2)}B · `
                + `strategies=${bboxTargetPositionStrategies.join(",")} · `
                + `variants=${bboxTargetVariants.join(",")}`,
              );
              const bboxMaxVisual = readOptionalPositiveIntEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL",
              );
              const bboxStageMaxQualityDebtPerGain = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_MAX_QUALITY_DEBT_PER_GAIN",
                0.40,
              );
              const bboxFinalMaxQualityDebtPerGain = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_FINAL_MAX_QUALITY_DEBT_PER_GAIN",
                0.40,
              );
              const bboxStageMaxSpacingDebtPerGain = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_MAX_SPACING_DEBT_PER_GAIN",
                1.00,
              );
              const bboxFinalMaxSpacingDebtPerGain = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_FINAL_MAX_SPACING_DEBT_PER_GAIN",
                0.25,
              );
              const bboxEdgeNodeDebtWeight = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_EDGE_NODE_DEBT_WEIGHT",
                0.50,
              );
              const bboxMaxBundleNode = readNonNegativeIntEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_MAX_BUNDLE_NODE",
                0,
              );
              const bboxMaxNodeSpacing = readOptionalNonNegativeIntEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_MAX_NODE_SPACING",
              );
              const bboxCandidateTimeoutMs = readPositiveIntEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_TIMEOUT_MS",
                60_000,
              );
              const bboxAreaTolerance =
                readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_TOLERANCE", 1.02);
              const bboxSoftCapB =
                readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_SOFT_B", 1.2);
              const bboxAcceptMinGain =
                readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_ACCEPT_MIN_GAIN", 0.35);
              const bboxPartialAccept = readBoolEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_ACCEPT",
                true,
              );
              const bboxContinueAfterFinalPartial = readBoolEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_CONTINUE_AFTER_FINAL_PARTIAL",
                true,
              );
              const bboxPartialMinGain = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MIN_GAIN",
                0.025,
              );
              const bboxPartialMaxQualityDebt = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MAX_QUALITY_DEBT",
                0.04,
              );
              const bboxPartialMaxSpacingDebt = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_PARTIAL_MAX_SPACING_DEBT",
                0.01,
              );
              const bboxMaxVisualDebtRatio = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_MAX_VISUAL_DEBT_RATIO",
                0.05,
              );
              const bboxStageBailAfter = readNonNegativeIntEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_AFTER",
                6,
              );
              const bboxStageBailQualityRatio = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_QUALITY_RATIO",
                5.0,
              );
              const bboxStageBailSpacingRatio = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_SPACING_RATIO",
                10.0,
              );
              const bboxStageBailNearRatio = readFloatEnv(
                "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_BAIL_NEAR_RATIO",
                2.0,
              );
              let bboxTried = false;
              let bboxAnyAccepted = false;
              for (const bboxStageTargetB of bboxTargetStages) {
                const bboxStageBaseLayout =
                  decodeLayoutSnapshot(acceptedLayout, "ogdfLayout");
                const bboxStageBaseSummary = summarizeLayout(bboxStageBaseLayout);
                const bboxStageBaseAreaB =
                  bboxStageBaseSummary.nodeBBoxWidth
                  * bboxStageBaseSummary.nodeBBoxHeight
                  / 1e9;
                if (bboxStageBaseAreaB <= bboxStageTargetB * bboxAreaTolerance) {
                  logger?.info(
                    `[bbox target] stage=${bboxStageTargetB.toFixed(2)}B skipped; `
                    + `current bbox=${bboxStageBaseAreaB.toFixed(2)}B`,
                  );
                  continue;
                }
                let bboxStageAccepted = false;
                let bboxStageAcceptedPartial = false;
                let bboxStageBestAreaB = bboxStageBaseAreaB;
                let bboxStageBailed = false;
                let bboxStageBadStreak = 0;
                const bboxStageSeenPositionCandidates = new Set<string>();
                for (const bboxSafety of bboxTargetSafeties) {
                  const stageTag = String(Math.round(bboxStageTargetB * 100));
                  const safetyTag = String(Math.round(bboxSafety * 1000));
                  for (const bboxPositionStrategy of bboxTargetPositionStrategies) {
                    const bboxStart = Date.now();
                    let bboxTargetPositionsPath: string | undefined;
                    try {
                      const bboxTarget = await writePositionsTsvForBBoxTarget(
                        acceptedLayout,
                        path.join(
                          os.tmpdir(),
                          `django-erd-bbox-target-${Date.now()}-${stageTag}-${safetyTag}-${bboxPositionStrategy}.tsv`,
                        ),
                        bboxStageTargetB,
                        bboxSafety,
                        bboxPositionStrategy,
                      );
                      if (bboxTarget === undefined) {
                        continue;
                      }
                      bboxTargetPositionsPath = bboxTarget.positionsPath;
                      const bboxPositionCandidate = await readFile(
                        bboxTarget.positionsPath,
                        "utf8",
                      );
                      if (bboxStageSeenPositionCandidates.has(bboxPositionCandidate)) {
                        logger?.info(
                          `[bbox target] stage=${bboxStageTargetB.toFixed(2)}B `
                          + `safety=${bboxSafety.toFixed(3)} `
                          + `strategy=${bboxPositionStrategy} skipped; `
                          + "generated positions duplicate an earlier candidate",
                        );
                        continue;
                      }
                      bboxStageSeenPositionCandidates.add(bboxPositionCandidate);
                      bboxTried = true;
                      logger?.info(
                        `[bbox target] stage=${bboxStageTargetB.toFixed(2)}B `
                        + `safety=${bboxSafety.toFixed(3)} `
                        + `strategy=${bboxPositionStrategy} `
                        + `${bboxTarget.description}`,
                      );
                      for (const bboxVariant of bboxTargetVariants) {
                      const bboxVariantStart = Date.now();
                      try {
                        const bboxRerouteArgs = [
                          "layout",
                          "--mode", normalizedRequestedLayoutMode,
                          "--nodes-file", nodesPath,
                          "--edges-file", edgesPath,
                          "--edge-routing", effectiveEdgeRouting,
                          "--cluster-graph", "1",
                          "--positions-tsv", bboxTarget.positionsPath,
                        ];
                        const bboxReroute = await execFileAsync(
                          binaryPath,
                          bboxRerouteArgs,
                          {
                            cwd: extensionRootPath,
                            env: ogdfOptimizedBboxTargetEnvForVariant(bboxVariant),
                            killSignal: "SIGKILL",
                            maxBuffer: 100 * 1024 * 1024,
                            timeout: bboxCandidateTimeoutMs,
                          },
                        );
                        if (bboxReroute.stderr.trim().length > 0) {
                          logger?.info(
                            `[bbox target:${bboxVariant}] OGDF stderr: `
                            + bboxReroute.stderr.trim(),
                          );
                        }
                        const bboxCandidate = JSON.parse(bboxReroute.stdout);
                        // Draw every bbox-target candidate as it's computed so
                        // the user sees progress through this long stage
                        // (accepted or not — the final routed layout reloads
                        // when the whole pipeline finishes).
                        streamIntermediateLayout("bbox", bboxCandidate);
                        const bboxCandidateLayout =
                          decodeLayoutSnapshot(bboxCandidate, "ogdfLayout");
                        const bboxCandidateSummary = summarizeLayout(bboxCandidateLayout);
                        const bboxCandidateAreaB =
                          bboxCandidateSummary.nodeBBoxWidth
                          * bboxCandidateSummary.nodeBBoxHeight
                          / 1e9;
                        const bboxCandidateMetadata = bboxCandidate?.engineMetadata ?? {};
                        const bboxCandidateNodeOverlaps =
                          Number(bboxCandidateMetadata.nodeOverlaps ?? 0);
                        const bboxCandidateBundleNode =
                          Number(bboxCandidateMetadata.bundleNodeOverlaps ?? 0);
                        const bboxCandidateNodeSpacing =
                          Number(bboxCandidateMetadata.nodeSpacingOverlaps ?? 0);
                        const bboxCandidateVisual =
                          Number(bboxCandidateMetadata.visualCrossings ?? 0);
                        const bboxCandidateEdgeNode =
                          Number(bboxCandidateMetadata.edgeNodeIntersections ?? 0);
                        const bboxBeforeMetadata = acceptedLayout?.engineMetadata ?? {};
                        const bboxBeforeVisual =
                          Number(bboxBeforeMetadata.visualCrossings ?? Number.POSITIVE_INFINITY);
                        const bboxBeforeEdgeNode =
                          Number(bboxBeforeMetadata.edgeNodeIntersections ?? 0);
                        const bboxBeforeBundleNode =
                          Number(bboxBeforeMetadata.bundleNodeOverlaps ?? 0);
                        const bboxBeforeNodeSpacing =
                          Number(bboxBeforeMetadata.nodeSpacingOverlaps ?? 0);
                        const isFinalBboxStage =
                          bboxStageTargetB <= bboxTargetB + 1e-6;
                        const bboxHardOk =
                          bboxCandidateAreaB <= bboxStageTargetB * bboxAreaTolerance;
                        const bboxSoftGain =
                          Math.max(0, Math.min(0.95, bboxAcceptMinGain));
                        const bboxSoftOk =
                          bboxSoftCapB > 0
                          && bboxSoftGain > 0
                          && bboxCandidateAreaB <= bboxSoftCapB
                          && bboxCandidateAreaB <= bboxStageBestAreaB * (1 - bboxSoftGain);
                        const bboxCompressionGainRatio = ratioGain(
                          bboxStageBestAreaB,
                          bboxCandidateAreaB,
                        );
                        const bboxVisualGainRatio = ratioGain(
                          bboxBeforeVisual,
                          bboxCandidateVisual,
                        );
                        const bboxGraphNodeCount = Math.max(
                          1,
                          bboxStageBaseLayout.nodes.length,
                          bboxCandidateLayout.nodes.length,
                        );
                        const bboxGraphEdgeCount = Math.max(
                          1,
                          bboxStageBaseLayout.routedEdges.length,
                          bboxCandidateLayout.routedEdges.length,
                        );
                        const bboxVisualDebtRatio = positiveDeltaRatio(
                          bboxCandidateVisual,
                          bboxBeforeVisual,
                          bboxBeforeVisual,
                        );
                        const bboxEdgeNodeDebtRatio = positiveDeltaRatio(
                          bboxCandidateEdgeNode,
                          bboxBeforeEdgeNode,
                          bboxGraphEdgeCount,
                        );
                        const bboxSpacingDebtRatio = positiveDeltaRatio(
                          bboxCandidateNodeSpacing,
                          bboxBeforeNodeSpacing,
                          bboxGraphNodeCount,
                        );
                        const bboxQualityDebtRatio =
                          bboxVisualDebtRatio
                          + Math.max(0, bboxEdgeNodeDebtWeight) * bboxEdgeNodeDebtRatio;
                        const bboxQualityDebtPerGain =
                          bboxQualityDebtRatio
                          / Math.max(0.001, bboxCompressionGainRatio);
                        const bboxSpacingDebtPerGain =
                          bboxSpacingDebtRatio / Math.max(0.001, bboxCompressionGainRatio);
                        const bboxQualityDebtLimit = isFinalBboxStage
                          ? bboxFinalMaxQualityDebtPerGain
                          : bboxStageMaxQualityDebtPerGain;
                        const bboxSpacingDebtLimit = isFinalBboxStage
                          ? bboxFinalMaxSpacingDebtPerGain
                          : bboxStageMaxSpacingDebtPerGain;
                        const bboxAbsoluteVisualOk =
                          bboxMaxVisual === undefined || bboxCandidateVisual <= bboxMaxVisual;
                        const bboxVisualDebtOk =
                          bboxVisualDebtRatio <= bboxMaxVisualDebtRatio;
                        const bboxAbsoluteSpacingOk =
                          bboxMaxNodeSpacing === undefined
                          || bboxCandidateNodeSpacing <= bboxMaxNodeSpacing;
                        const bboxQualityOk =
                          bboxQualityDebtPerGain <= bboxQualityDebtLimit;
                        const bboxSpacingOk =
                          bboxSpacingDebtPerGain <= bboxSpacingDebtLimit;
                        const bboxBundleNodeOk =
                          bboxCandidateBundleNode <= bboxMaxBundleNode
                          || bboxCandidateBundleNode <= bboxBeforeBundleNode;
                        const bboxGoalOk = bboxHardOk || bboxSoftOk;
                        const bboxPartialOk =
                          bboxPartialAccept
                          && !bboxGoalOk
                          && bboxCompressionGainRatio >= bboxPartialMinGain
                          && bboxCandidateAreaB < bboxStageBestAreaB
                          && bboxQualityDebtRatio <= bboxPartialMaxQualityDebt
                          && bboxSpacingDebtRatio <= bboxPartialMaxSpacingDebt;
                        const bboxAcceptanceMode = bboxHardOk
                          ? "hard"
                          : bboxSoftOk
                            ? "soft"
                            : bboxPartialOk
                              ? "partial"
                              : "no";
                        const bboxEffectiveQualityOk = bboxGoalOk
                          ? bboxQualityOk
                          : bboxPartialOk;
                        const bboxEffectiveSpacingOk = bboxGoalOk
                          ? bboxSpacingOk
                          : bboxPartialOk;
                        const bboxAccept =
                          (bboxGoalOk || bboxPartialOk)
                          && bboxCandidateNodeOverlaps === 0
                          && bboxAbsoluteVisualOk
                          && bboxVisualDebtOk
                          && bboxEffectiveQualityOk
                          && bboxBundleNodeOk
                          && bboxAbsoluteSpacingOk
                          && bboxEffectiveSpacingOk;
                        logger?.info(
                          `[bbox target:${bboxVariant}] candidate done in `
                          + `${Date.now() - bboxVariantStart}ms · `
                          + `stage=${bboxStageTargetB.toFixed(2)}B · `
                          + `safety=${bboxSafety.toFixed(3)} · `
                          + `strategy=${bboxPositionStrategy} · `
                          + `bbox=${bboxCandidateAreaB.toFixed(2)}B · `
                          + `bboxOk=${bboxAcceptanceMode} · `
                          + `bboxGain=${bboxCompressionGainRatio.toFixed(3)} · `
                          + `visual=${bboxCandidateMetadata.visualCrossings ?? "?"} · `
                          + `visualGain=${bboxVisualGainRatio.toFixed(3)}`
                          + ` · `
                          + `visualDebt=${bboxVisualDebtRatio.toFixed(3)} · `
                          + `visualDebtOk=${bboxVisualDebtOk}`
                          + `/${bboxMaxVisualDebtRatio.toFixed(3)} · `
                          + `qualityDebt=${bboxQualityDebtRatio.toFixed(3)}`
                          + `/${bboxPartialMaxQualityDebt.toFixed(3)} · `
                          + `qualityDebtPerGain=${bboxQualityDebtPerGain.toFixed(3)}`
                          + `/${bboxQualityDebtLimit.toFixed(3)} · `
                          + `qualityOk=${bboxEffectiveQualityOk} · `
                          + `edgeNode=${bboxCandidateMetadata.edgeNodeIntersections ?? "?"} · `
                          + `edgeNodeDebt=${bboxEdgeNodeDebtRatio.toFixed(3)} · `
                          + `bundleNode=${bboxBeforeBundleNode}->`
                          + `${bboxCandidateBundleNode}/${bboxMaxBundleNode} · `
                          + `bundleNodeOk=${bboxBundleNodeOk} · `
                          + `nodeOverlaps=${bboxCandidateNodeOverlaps} · `
                          + `nodeSpacing=${bboxCandidateNodeSpacing} · `
                          + `spacingDebt=${bboxSpacingDebtRatio.toFixed(3)}`
                          + `/${bboxPartialMaxSpacingDebt.toFixed(3)} · `
                          + `spacingDebtPerGain=${bboxSpacingDebtPerGain.toFixed(3)}`
                          + `/${bboxSpacingDebtLimit.toFixed(3)} · `
                          + `spacingOk=${bboxEffectiveSpacingOk} · `
                          + `partialOk=${bboxPartialOk} · `
                          + `accepted=${bboxAccept}`,
                        );
                        if (bboxAccept) {
                          acceptedStdout = bboxReroute.stdout;
                          acceptedLayout = bboxCandidate;
                          bboxStageBestAreaB = bboxCandidateAreaB;
                          bboxAnyAccepted = true;
                          const bboxKeepSearchingAfterPartial =
                            bboxContinueAfterFinalPartial
                            && isFinalBboxStage
                            && bboxPartialOk
                            && !bboxGoalOk;
                          if (bboxKeepSearchingAfterPartial) {
                            bboxStageAcceptedPartial = true;
                            logger?.info(
                              `[bbox target] stage=${bboxStageTargetB.toFixed(2)}B `
                              + `accepted partial bbox=${bboxCandidateAreaB.toFixed(2)}B; `
                              + "continuing final-stage search",
                            );
                            continue;
                          }
                          bboxStageAccepted = true;
                          break;
                        }
                        if (bboxStageBailAfter > 0) {
                          // Bail criterion uses per-gain debt against the
                          // stage's accept limit (qualityOk threshold). This
                          // way candidates that hit bbox-hard but barely miss
                          // quality are not counted as "far", so the search
                          // continues into lower safeties where a slightly
                          // more aggressive scale can pass.
                          const qualityLimit = Math.max(
                            bboxQualityDebtLimit,
                            1e-6,
                          );
                          const spacingLimit = Math.max(
                            bboxSpacingDebtLimit,
                            1e-6,
                          );
                          const qualityPerGainOverLimit =
                            bboxQualityDebtPerGain / qualityLimit;
                          const spacingPerGainOverLimit =
                            bboxSpacingDebtPerGain / spacingLimit;
                          const farFromAcceptance =
                            qualityPerGainOverLimit >= bboxStageBailQualityRatio
                            || spacingPerGainOverLimit >= bboxStageBailSpacingRatio
                            || bboxCandidateNodeOverlaps > 0;
                          const nearAcceptance =
                            qualityPerGainOverLimit <= bboxStageBailNearRatio
                            && spacingPerGainOverLimit <= bboxStageBailNearRatio
                            && bboxCandidateNodeOverlaps === 0
                            && bboxAbsoluteVisualOk
                            && bboxVisualDebtOk
                            && bboxBundleNodeOk
                            && bboxAbsoluteSpacingOk;
                          if (nearAcceptance) {
                            bboxStageBadStreak = 0;
                          } else if (farFromAcceptance) {
                            bboxStageBadStreak += 1;
                            if (bboxStageBadStreak >= bboxStageBailAfter) {
                              bboxStageBailed = true;
                              logger?.info(
                                `[bbox target] stage=${bboxStageTargetB.toFixed(2)}B `
                                + `bail-out after ${bboxStageBadStreak} bad candidates · `
                                + `qualityPerGain=${qualityPerGainOverLimit.toFixed(2)}`
                                + `/${bboxStageBailQualityRatio.toFixed(2)} · `
                                + `spacingPerGain=${spacingPerGainOverLimit.toFixed(2)}`
                                + `/${bboxStageBailSpacingRatio.toFixed(2)}`,
                              );
                              break;
                            }
                          }
                        }
                      } catch (err) {
                        const msg = err instanceof Error ? err.message : String(err);
                        logger?.warn(
                          `[bbox target:${bboxVariant}] candidate failed in `
                          + `${Date.now() - bboxVariantStart}ms · `
                          + `stage=${bboxStageTargetB.toFixed(2)}B · `
                          + `safety=${bboxSafety.toFixed(3)} · `
                          + `strategy=${bboxPositionStrategy}: ${msg}`,
                        );
                      }
                      }
                      if (bboxStageAccepted || bboxStageBailed) {
                        break;
                      }
                    } catch (err) {
                      const msg = err instanceof Error ? err.message : String(err);
                      logger?.warn(
                        `[bbox target] candidate failed in ${Date.now() - bboxStart}ms · `
                        + `stage=${bboxStageTargetB.toFixed(2)}B · `
                        + `safety=${bboxSafety.toFixed(3)} · `
                        + `strategy=${bboxPositionStrategy} · trying next candidate: ${msg}`,
                      );
                    } finally {
                      if (bboxTargetPositionsPath) {
                        await rm(bboxTargetPositionsPath, { force: true });
                      }
                    }
                  }
                  if (bboxStageAccepted || bboxStageBailed) {
                    break;
                  }
                }
                if (!bboxStageAccepted && bboxStageAcceptedPartial) {
                  bboxStageAccepted = true;
                }
                if (!bboxStageAccepted) {
                  logger?.info(
                    `[bbox target] stage=${bboxStageTargetB.toFixed(2)}B `
                    + (bboxStageBailed
                      ? "bailed out early; stopping staged compression"
                      : "had no accepted candidate; stopping staged compression"),
                  );
                  break;
                }
              }
              if (bboxTried && !bboxAnyAccepted) {
                logger?.info(
                  "[bbox target] no candidate accepted; keeping v36 post-stack reroute",
                );
              }
            }

            postReroutePolishDeadline ??=
              startPostReroutePolishDeadline(logger);

            if (readBoolEnv("DJERD_OPTIMIZED_EDGE_NODE_POLISH", true)) {
              const polishVariants = readEdgeNodePolishVariants();
              const polishSkipOnVisualBlowup = readFloatEnv(
                "DJERD_OPTIMIZED_EDGE_NODE_POLISH_SKIP_ON_VISUAL_RATIO",
                1.15,
              );
              let polishSkipRemaining = false;
              for (const polishVariant of polishVariants) {
                if (
                  remainingPostReroutePolishBudgetMs(
                    postReroutePolishDeadline,
                  ) <= 0
                ) {
                  logger?.info(
                    `[post-reroute polish budget] edge-node polish stopped; `
                    + `budget exhausted after `
                    + `${Date.now() - postReroutePolishDeadline.startedMs}ms`
                    + `/${postReroutePolishDeadline.budgetMs}ms`,
                  );
                  break;
                }
                if (polishSkipRemaining) {
                  logger?.info(
                    `[edge-node polish:${polishVariant}] skipped; `
                    + "earlier variant showed visual blow-up",
                  );
                  continue;
                }
                const polishBaseLayout =
                  decodeLayoutSnapshot(acceptedLayout, "ogdfLayout");
                const polishBaseSummary = summarizeLayout(polishBaseLayout);
                const polishBaseAreaB =
                  polishBaseSummary.nodeBBoxWidth
                  * polishBaseSummary.nodeBBoxHeight
                  / 1e9;
                const polishBaseMetadata = acceptedLayout?.engineMetadata ?? {};
                const polishBaseEdgeNode =
                  Number(polishBaseMetadata.edgeNodeIntersections ?? 0);
                if (polishBaseEdgeNode <= 0 || polishBaseAreaB <= 0) {
                  break;
                }

                const polishStart = Date.now();
                const polishPositionsPath = trackTransientPath(path.join(
                  os.tmpdir(),
                  `django-erd-edge-node-polish-${polishVariant}-${Date.now()}.tsv`,
                ));
                let polishBudgetedTimeout:
                  BudgetedCandidateTimeout | undefined;
                try {
                  await writePositionsTsvFromLayoutJson(
                    JSON.stringify(acceptedLayout),
                    polishPositionsPath,
                  );
                  const polishTimeoutMs = readPositiveIntEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_TIMEOUT_MS",
                    60_000,
                  );
                  polishBudgetedTimeout = budgetCandidateTimeout(
                    postReroutePolishDeadline,
                    polishTimeoutMs,
                    `edge-node polish:${polishVariant}`,
                    logger,
                  );
                  if (!polishBudgetedTimeout) {
                    break;
                  }
                  const polishReroute = await execFileAsync(
                    binaryPath,
                    [
                      "layout",
                      "--mode", normalizedRequestedLayoutMode,
                      "--nodes-file", nodesPath,
                      "--edges-file", edgesPath,
                      "--edge-routing", effectiveEdgeRouting,
                      "--cluster-graph", "1",
                      "--positions-tsv", polishPositionsPath,
                    ],
                    {
                      cwd: extensionRootPath,
                      env: ogdfOptimizedEdgeNodePolishEnv(polishVariant),
                      killSignal: "SIGKILL",
                      maxBuffer: 100 * 1024 * 1024,
                      timeout: polishBudgetedTimeout.timeoutMs,
                    },
                  );
                  if (polishReroute.stderr.trim().length > 0) {
                    logger?.info(
                      `[edge-node polish:${polishVariant}] OGDF stderr: `
                      + polishReroute.stderr.trim(),
                    );
                  }
                  const polishCandidate = JSON.parse(polishReroute.stdout);
                  // Draw every polish candidate as it's computed.
                  streamIntermediateLayout("polish", polishCandidate);
                  const polishCandidateLayout =
                    decodeLayoutSnapshot(polishCandidate, "ogdfLayout");
                  const polishCandidateSummary = summarizeLayout(polishCandidateLayout);
                  const polishCandidateAreaB =
                    polishCandidateSummary.nodeBBoxWidth
                    * polishCandidateSummary.nodeBBoxHeight
                    / 1e9;
                  const polishCandidateMetadata = polishCandidate?.engineMetadata ?? {};
                  const polishCandidateEdgeNode =
                    Number(polishCandidateMetadata.edgeNodeIntersections ?? 0);
                  const polishCandidateVisual =
                    Number(polishCandidateMetadata.visualCrossings ?? 0);
                  const polishCandidateNodeOverlaps =
                    Number(polishCandidateMetadata.nodeOverlaps ?? 0);
                  const polishCandidateBundleNode =
                    Number(polishCandidateMetadata.bundleNodeOverlaps ?? 0);
                  const polishCandidateSpacing =
                    Number(polishCandidateMetadata.nodeSpacingOverlaps ?? 0);
                  const polishBaseVisual =
                    Number(polishBaseMetadata.visualCrossings ?? 0);
                  const polishBaseSpacing =
                    Number(polishBaseMetadata.nodeSpacingOverlaps ?? 0);
                  const polishNodeCount = Math.max(
                    1,
                    polishBaseLayout.nodes.length,
                    polishCandidateLayout.nodes.length,
                  );
                  const polishEdgeNodeGainRatio = ratioGain(
                    polishBaseEdgeNode,
                    polishCandidateEdgeNode,
                  );
                  const polishVisualGainRatio = ratioGain(
                    polishBaseVisual,
                    polishCandidateVisual,
                  );
                  const polishPrimaryGainRatio = Math.max(
                    polishEdgeNodeGainRatio,
                    polishVisualGainRatio,
                  );
                  const polishVisualDebtRatio = positiveDeltaRatio(
                    polishCandidateVisual,
                    polishBaseVisual,
                    polishBaseVisual,
                  );
                  const polishSpacingDebtRatio = positiveDeltaRatio(
                    polishCandidateSpacing,
                    polishBaseSpacing,
                    polishNodeCount,
                  );
                  const polishVisualDebtPerGain =
                    polishVisualDebtRatio / Math.max(0.001, polishPrimaryGainRatio);
                  const polishSpacingDebtPerGain =
                    polishSpacingDebtRatio / Math.max(0.001, polishPrimaryGainRatio);
                  const polishBboxGrowth =
                    polishCandidateAreaB / Math.max(0.001, polishBaseAreaB);
                  const polishMinGainRatio = readFloatEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_MIN_GAIN_RATIO",
                    0.08,
                  );
                  const polishMinVisualGainRatio = readFloatEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_MIN_VISUAL_GAIN_RATIO",
                    0.005,
                  );
                  const polishBboxGrowthLimit = readFloatEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_BBOX_GROWTH_LIMIT",
                    1.08,
                  );
                  const polishBboxTargetTolerance = readFloatEnv(
                    "DJERD_OPTIMIZED_BBOX_TARGET_TOLERANCE",
                    1.02,
                  );
                  const polishAboveTargetBboxGrowthLimit = readFloatEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_ABOVE_TARGET_BBOX_GROWTH_LIMIT",
                    1.002,
                  );
                  const polishAboveBboxTarget =
                    bboxTargetB > 0
                    && polishBaseAreaB > bboxTargetB * polishBboxTargetTolerance;
                  const polishEffectiveBboxGrowthLimit =
                    polishAboveBboxTarget
                      ? Math.min(
                        polishBboxGrowthLimit,
                        polishAboveTargetBboxGrowthLimit,
                      )
                      : polishBboxGrowthLimit;
                  const polishMaxVisualDebtPerGain = readFloatEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_MAX_VISUAL_DEBT_PER_GAIN",
                    0.75,
                  );
                  const polishMaxSpacingDebtPerGain = readFloatEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_MAX_SPACING_DEBT_PER_GAIN",
                    1.00,
                  );
                  const polishAllowVisualRegression = readBoolEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_ALLOW_VISUAL_REGRESSION",
                    false,
                  );
                  const polishMaxBundleNode = readNonNegativeIntEnv(
                    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_MAX_BUNDLE_NODE",
                    0,
                  );
                  const polishGainOk =
                    polishEdgeNodeGainRatio >= polishMinGainRatio;
                  const polishVisualGainOk =
                    polishCandidateVisual > 0
                    && polishVisualGainRatio >= polishMinVisualGainRatio;
                  const polishBboxOk =
                    polishBboxGrowth <= polishEffectiveBboxGrowthLimit;
                  const polishVisualOk =
                    polishVisualDebtPerGain <= polishMaxVisualDebtPerGain;
                  const polishVisualNonRegressionOk =
                    polishAllowVisualRegression
                    || polishCandidateVisual <= polishBaseVisual;
                  const polishSpacingOk =
                    polishSpacingDebtPerGain <= polishMaxSpacingDebtPerGain;
                  const polishAcceptReason = polishGainOk
                    ? "edge-node"
                    : polishVisualGainOk
                      ? "visual"
                      : "none";
                  const polishAccept =
                    (polishGainOk || polishVisualGainOk)
                    && polishBboxOk
                    && polishVisualOk
                    && polishVisualNonRegressionOk
                    && polishSpacingOk
                    && polishCandidateNodeOverlaps === 0
                    && polishCandidateBundleNode <= polishMaxBundleNode;
                  logger?.info(
                    `[edge-node polish:${polishVariant}] candidate done in `
                    + `${Date.now() - polishStart}ms · `
                    + `edgeNode=${polishBaseEdgeNode}->${polishCandidateEdgeNode} · `
                    + `edgeNodeGain=${polishEdgeNodeGainRatio.toFixed(3)}`
                    + `/${polishMinGainRatio.toFixed(3)} · `
                    + `visual=${polishBaseVisual}->${polishCandidateVisual} · `
                    + `visualGain=${polishVisualGainRatio.toFixed(3)}`
                    + `/${polishMinVisualGainRatio.toFixed(3)} · `
                    + `visualDebtPerGain=${polishVisualDebtPerGain.toFixed(3)}`
                    + `/${polishMaxVisualDebtPerGain.toFixed(3)} · `
                    + `visualNonRegression=${polishVisualNonRegressionOk} · `
                    + `bbox=${polishBaseAreaB.toFixed(2)}B->`
                    + `${polishCandidateAreaB.toFixed(2)}B · `
                    + `bboxGrowth=${polishBboxGrowth.toFixed(3)}`
                    + `/${polishEffectiveBboxGrowthLimit.toFixed(3)}`
                    + `(aboveTarget=${polishAboveBboxTarget}) · `
                    + `bundleNode=${polishCandidateBundleNode} · `
                    + `nodeOverlaps=${polishCandidateNodeOverlaps} · `
                    + `nodeSpacing=${polishBaseSpacing}->${polishCandidateSpacing} · `
                    + `spacingDebtPerGain=${polishSpacingDebtPerGain.toFixed(3)}`
                    + `/${polishMaxSpacingDebtPerGain.toFixed(3)} · `
                    + `acceptReason=${polishAcceptReason} · `
                    + `accepted=${polishAccept}`,
                  );
                  if (polishAccept) {
                    acceptedStdout = polishReroute.stdout;
                    acceptedLayout = polishCandidate;
                  } else {
                    const polishBaseBundleNode = Number(
                      polishBaseMetadata.bundleNodeOverlaps ?? 0,
                    );
                    const visualBlowup =
                      polishSkipOnVisualBlowup > 0
                      && polishBaseVisual > 0
                      && polishCandidateVisual
                        >= polishBaseVisual * polishSkipOnVisualBlowup;
                    const bundleRegression =
                      polishCandidateBundleNode > polishBaseBundleNode
                      && polishCandidateBundleNode > polishMaxBundleNode;
                    const edgeNodeRegression =
                      polishCandidateEdgeNode > polishBaseEdgeNode;
                    if (visualBlowup || (bundleRegression && edgeNodeRegression)) {
                      polishSkipRemaining = true;
                    }
                  }
                } catch (err) {
                  const budgetFailure = isBudgetCausedCandidateFailure(
                    err,
                    polishBudgetedTimeout,
                  );
                  if (budgetFailure) {
                    logger?.info(
                      `[post-reroute polish budget] edge-node polish stopped; `
                      + `budget-limited candidate timed out · `
                      + `variant=${polishVariant}`,
                    );
                    break;
                  }
                  const msg = err instanceof Error ? err.message : String(err);
                  logger?.warn(
                    `[edge-node polish:${polishVariant}] candidate failed in `
                    + `${Date.now() - polishStart}ms: ${msg}`,
                  );
                }
              }
            }

            const preVisualPolishLayout = acceptedLayout;
            const preVisualPolishSummary = summarizeLayout(
              decodeLayoutSnapshot(preVisualPolishLayout, "ogdfLayout"),
            );
            const preVisualPolishNodeAreaB =
              preVisualPolishSummary.nodeBBoxWidth
              * preVisualPolishSummary.nodeBBoxHeight
              / 1e9;
            const preVisualPolishRouteAreaB =
              preVisualPolishSummary.routeBBoxWidth
              * preVisualPolishSummary.routeBBoxHeight
              / 1e9;
            const preVisualPolishMetadata =
              preVisualPolishLayout?.engineMetadata ?? {};
            const preVisualPolishVisual =
              Number(preVisualPolishMetadata.visualCrossings ?? 0);

            if (readBoolEnv("DJERD_OPTIMIZED_VISUAL_CROSS_POLISH", true)) {
              const visualVariants = readVisualCrossPolishVariants();
              const visualTarget =
                readOptionalPositiveIntEnv("DJERD_OPTIMIZED_VISUAL_CROSS_TARGET")
                ?? DEFAULT_VISUAL_CROSS_TARGET;
              const visualRounds = readPositiveIntEnv(
                "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUNDS",
                4,
              );
              let visualPolishBudgetStopped = false;
              for (let visualRound = 1; visualRound <= visualRounds; visualRound++) {
                if (
                  visualPolishBudgetStopped
                  || remainingPostReroutePolishBudgetMs(
                    postReroutePolishDeadline,
                  ) <= 0
                ) {
                  logger?.info(
                    `[post-reroute polish budget] visual-cross polish stopped; `
                    + `round=${visualRound}/${visualRounds} · `
                    + `budget exhausted after `
                    + `${Date.now() - postReroutePolishDeadline.startedMs}ms`
                    + `/${postReroutePolishDeadline.budgetMs}ms`,
                  );
                  break;
                }
                const visualBaseLayout =
                  decodeLayoutSnapshot(acceptedLayout, "ogdfLayout");
                const visualBaseSummary = summarizeLayout(visualBaseLayout);
                const visualBaseNodeAreaB =
                  visualBaseSummary.nodeBBoxWidth
                  * visualBaseSummary.nodeBBoxHeight
                  / 1e9;
                const visualBaseRouteAreaB =
                  visualBaseSummary.routeBBoxWidth
                  * visualBaseSummary.routeBBoxHeight
                  / 1e9;
                const visualBaseMetadata = acceptedLayout?.engineMetadata ?? {};
                const visualBaseVisual =
                  Number(visualBaseMetadata.visualCrossings ?? 0);
                if (visualBaseVisual <= 0) {
                  break;
                }
                if (visualBaseVisual <= visualTarget) {
                  logger?.info(
                    `[visual-cross polish] target reached; `
                    + `visual=${visualBaseVisual}/${visualTarget} · `
                    + `round=${visualRound}/${visualRounds}`,
                  );
                  break;
                }

                type VisualCrossPolishCandidate = {
                  bundleEdge: number;
                  bundleNode: number;
                  canonicalAdjacent: number | undefined;
                  canonicalNodeHits: number | undefined;
                  edgeCross: number;
                  edgeNode: number;
                  edgeSegmentOverlap: number;
                  gain: number;
                  gainRatio: number;
                  layout: typeof acceptedLayout;
                  overlappingEdges: number;
                  routeAreaB: number;
                  stdout: string;
                  variant: VisualCrossPolishVariant;
                  visual: number;
                };
                let visualBest: VisualCrossPolishCandidate | undefined;
                const visualRouteBboxTieVisualSlack = readNonNegativeIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUTE_BBOX_TIE_VISUAL_SLACK",
                  8,
                );
                const visualRouteBboxTieRatio = readFloatEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUTE_BBOX_TIE_RATIO",
                  0.92,
                );
                const visualTopologyTieVisualSlack = readNonNegativeIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_TOPOLOGY_TIE_VISUAL_SLACK",
                  64,
                );
                const visualTopologyTieEdgeNodeSlack = readNonNegativeIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_TOPOLOGY_TIE_EDGE_NODE_SLACK",
                  32,
                );
                const visualRouteBboxTieWins = (
                  candidate: VisualCrossPolishCandidate,
                  current: VisualCrossPolishCandidate,
                ): boolean =>
                  visualRouteBboxTieVisualSlack > 0
                  && visualRouteBboxTieRatio > 0
                  && candidate.visual
                    <= current.visual + visualRouteBboxTieVisualSlack
                  && candidate.routeAreaB
                    < current.routeAreaB * visualRouteBboxTieRatio;
                const visualTopologyDebt = (
                  candidate: VisualCrossPolishCandidate,
                ): number =>
                  candidate.edgeSegmentOverlap + candidate.overlappingEdges;
                const visualTopologyTieWins = (
                  candidate: VisualCrossPolishCandidate,
                  current: VisualCrossPolishCandidate,
                ): boolean =>
                  visualTopologyDebt(candidate) < visualTopologyDebt(current)
                  && candidate.visual
                    <= current.visual + visualTopologyTieVisualSlack
                  && candidate.edgeNode
                    <= current.edgeNode + visualTopologyTieEdgeNodeSlack
                  && candidate.bundleNode <= current.bundleNode;
                const visualCandidateBeatsBest = (
                  candidate: VisualCrossPolishCandidate,
                ): boolean => {
                  if (visualBest === undefined) {
                    return true;
                  }
                  if (visualTopologyTieWins(candidate, visualBest)) {
                    return true;
                  }
                  if (visualTopologyTieWins(visualBest, candidate)) {
                    return false;
                  }
                  if (visualRouteBboxTieWins(candidate, visualBest)) {
                    return true;
                  }
                  if (visualRouteBboxTieWins(visualBest, candidate)) {
                    return false;
                  }
                  return candidate.visual < visualBest.visual
                    || (
                      candidate.visual === visualBest.visual
                      && candidate.edgeNode < visualBest.edgeNode
                    )
                    || (
                      candidate.visual === visualBest.visual
                      && candidate.edgeNode === visualBest.edgeNode
                      && candidate.bundleNode < visualBest.bundleNode
                    )
                    || (
                      candidate.visual === visualBest.visual
                      && candidate.edgeNode === visualBest.edgeNode
                      && candidate.bundleNode === visualBest.bundleNode
                      && candidate.bundleEdge < visualBest.bundleEdge
                    )
                    || (
                      candidate.visual === visualBest.visual
                      && candidate.edgeNode === visualBest.edgeNode
                      && candidate.bundleNode === visualBest.bundleNode
                      && candidate.bundleEdge === visualBest.bundleEdge
                      && candidate.edgeCross < visualBest.edgeCross
                    )
                    || (
                      candidate.visual === visualBest.visual
                      && candidate.edgeNode === visualBest.edgeNode
                      && candidate.bundleNode === visualBest.bundleNode
                      && candidate.bundleEdge === visualBest.bundleEdge
                      && candidate.edgeCross === visualBest.edgeCross
                      && candidate.edgeSegmentOverlap
                      < visualBest.edgeSegmentOverlap
                    )
                    || (
                      candidate.visual === visualBest.visual
                      && candidate.edgeNode === visualBest.edgeNode
                      && candidate.bundleNode === visualBest.bundleNode
                      && candidate.bundleEdge === visualBest.bundleEdge
                      && candidate.edgeCross === visualBest.edgeCross
                      && candidate.edgeSegmentOverlap
                      === visualBest.edgeSegmentOverlap
                      && candidate.overlappingEdges < visualBest.overlappingEdges
                    );
                };
                for (const visualVariant of visualVariants) {
                  if (
                    remainingPostReroutePolishBudgetMs(
                      postReroutePolishDeadline,
                    ) <= 0
                  ) {
                    visualPolishBudgetStopped = true;
                    logger?.info(
                      `[post-reroute polish budget] visual-cross polish stopped; `
                      + `round=${visualRound}/${visualRounds} · `
                      + `nextVariant=${visualVariant} · budget exhausted`,
                    );
                    break;
                  }
                  const visualStart = Date.now();
                  const visualPositionsPath = trackTransientPath(path.join(
                    os.tmpdir(),
                    `django-erd-visual-cross-polish-${visualVariant}-r${visualRound}-${Date.now()}.tsv`,
                  ));
                  const visualRoutesPath = visualVariant === "route-retouch"
                    ? trackTransientPath(path.join(
                      os.tmpdir(),
                      `django-erd-visual-cross-polish-${visualVariant}-r${visualRound}-${Date.now()}-routes.tsv`,
                    ))
                    : undefined;
                  let visualBudgetedTimeout:
                    BudgetedCandidateTimeout | undefined;
                  try {
                    await writePositionsTsvFromLayoutJson(
                      JSON.stringify(acceptedLayout),
                      visualPositionsPath,
                    );
                    if (visualRoutesPath) {
                      await writeRoutesTsvFromLayoutJson(
                        JSON.stringify(acceptedLayout),
                        visualRoutesPath,
                      );
                    }
                    const visualTimeoutMs = readPositiveIntEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_TIMEOUT_MS",
                      60_000,
                    );
                    visualBudgetedTimeout = budgetCandidateTimeout(
                      postReroutePolishDeadline,
                      visualTimeoutMs,
                      `visual-cross polish:${visualVariant}`,
                      logger,
                    );
                    if (!visualBudgetedTimeout) {
                      visualPolishBudgetStopped = true;
                      break;
                    }
                    const visualArgs = [
                      "layout",
                      "--mode", normalizedRequestedLayoutMode,
                      "--nodes-file", nodesPath,
                      "--edges-file", edgesPath,
                      "--edge-routing", effectiveEdgeRouting,
                      "--cluster-graph", "1",
                      "--positions-tsv", visualPositionsPath,
                    ];
                    if (visualRoutesPath) {
                      visualArgs.push("--routes-tsv", visualRoutesPath);
                    }
                    const visualReroute = await execFileAsync(
                      binaryPath,
                      visualArgs,
                      {
                        cwd: extensionRootPath,
                        env: ogdfOptimizedVisualCrossPolishEnv(visualVariant),
                        killSignal: "SIGKILL",
                        maxBuffer: 100 * 1024 * 1024,
                        timeout: visualBudgetedTimeout.timeoutMs,
                      },
                    );
                    if (visualReroute.stderr.trim().length > 0) {
                      logger?.info(
                        `[visual-cross polish:${visualVariant}] OGDF stderr: `
                        + visualReroute.stderr.trim(),
                      );
                    }
                    const visualCandidate = JSON.parse(visualReroute.stdout);
                    streamIntermediateLayout("polish", visualCandidate);
                    const visualCandidateLayout =
                      decodeLayoutSnapshot(visualCandidate, "ogdfLayout");
                    const visualCandidateSummary =
                      summarizeLayout(visualCandidateLayout);
                    const visualCandidateNodeAreaB =
                      visualCandidateSummary.nodeBBoxWidth
                      * visualCandidateSummary.nodeBBoxHeight
                      / 1e9;
                    const visualCandidateRouteAreaB =
                      visualCandidateSummary.routeBBoxWidth
                      * visualCandidateSummary.routeBBoxHeight
                      / 1e9;
                    const visualCandidateMetadata =
                      visualCandidate?.engineMetadata ?? {};
                    const visualCanonicalNonRegression =
                      evaluateCanonicalCrossingNonRegression(
                        visualBaseMetadata,
                        visualCandidateMetadata,
                      );
                    const visualCandidateVisual =
                      Number(visualCandidateMetadata.visualCrossings ?? 0);
                    const visualBaseEdgeCross =
                      Number(visualBaseMetadata.edgeCrossings ?? 0);
                    const visualCandidateEdgeCross =
                      Number(visualCandidateMetadata.edgeCrossings ?? 0);
                    const visualBaseEdgeNode =
                      Number(visualBaseMetadata.edgeNodeIntersections ?? 0);
                    const visualCandidateEdgeNode =
                      Number(visualCandidateMetadata.edgeNodeIntersections ?? 0);
                    const visualBaseBundleEdge =
                      Number(visualBaseMetadata.bundleEdgeIntersections ?? 0);
                    const visualCandidateBundleEdge =
                      Number(visualCandidateMetadata.bundleEdgeIntersections ?? 0);
                    const visualBaseOverlappingEdges =
                      Number(visualBaseMetadata.overlappingEdges ?? 0);
                    const visualCandidateOverlappingEdges =
                      Number(visualCandidateMetadata.overlappingEdges ?? 0);
                    const visualBaseEdgeSegmentOverlaps =
                      Number(visualBaseMetadata.edgeSegmentOverlaps ?? 0);
                    const visualCandidateEdgeSegmentOverlaps =
                      Number(visualCandidateMetadata.edgeSegmentOverlaps ?? 0);
                    const visualBaseBundleNode =
                      Number(visualBaseMetadata.bundleNodeOverlaps ?? 0);
                    const visualCandidateNodeOverlaps =
                      Number(visualCandidateMetadata.nodeOverlaps ?? 0);
                    const visualCandidateBundleNode =
                      Number(visualCandidateMetadata.bundleNodeOverlaps ?? 0);
                    const visualBaseSpacing =
                      Number(visualBaseMetadata.nodeSpacingOverlaps ?? 0);
                    const visualCandidateSpacing =
                      Number(visualCandidateMetadata.nodeSpacingOverlaps ?? 0);
                    const visualGain = Math.max(
                      0,
                      visualBaseVisual - visualCandidateVisual,
                    );
                    const visualGainRatio = ratioGain(
                      visualBaseVisual,
                      visualCandidateVisual,
                    );
                    const visualMinGain = readNonNegativeIntEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MIN_GAIN",
                      1,
                    );
                    const visualMinGainRatio = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MIN_GAIN_RATIO",
                      0,
                    );
                    const visualNodeBboxGrowthLimit = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_NODE_BBOX_GROWTH_LIMIT",
                      1.04,
                    );
                    const visualRouteBboxGrowthLimit =
                      visualCrossPolishUsesPeriphery(visualVariant)
                        ? readFloatEnv(
                          "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_ROUTE_BBOX_GROWTH_LIMIT",
                          1.57,
                        )
                        : readFloatEnv(
                          "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ROUTE_BBOX_GROWTH_LIMIT",
                          1.08,
                        );
                    const visualMaxRouteBboxDebtPerGain =
                      visualCrossPolishUsesPeriphery(visualVariant)
                        ? readFloatEnv(
                          "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_PERIPHERY_MAX_ROUTE_BBOX_DEBT_PER_GAIN",
                          readFloatEnv(
                            "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_ROUTE_BBOX_DEBT_PER_GAIN",
                            0.05,
                          ),
                        )
                        : readFloatEnv(
                          "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_ROUTE_BBOX_DEBT_PER_GAIN",
                          0.05,
                        );
                    const visualMaxSpacingDebt = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SPACING_DEBT",
                      0,
                    );
                    const visualMaxSpacingDebtPerGain = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SPACING_DEBT_PER_GAIN",
                      0,
                    );
                    const visualMaxOverlappingEdgeDebt = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_OVERLAPPING_EDGE_DEBT",
                      0,
                    );
                    const visualMaxOverlappingEdgeDebtPerGain = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_OVERLAPPING_EDGE_DEBT_PER_GAIN",
                      0.25,
                    );
                    const visualMaxSegmentOverlapDebt = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SEGMENT_OVERLAP_DEBT",
                      0,
                    );
                    const visualMaxSegmentOverlapDebtPerGain = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_SEGMENT_OVERLAP_DEBT_PER_GAIN",
                      0.25,
                    );
                    const visualMaxBundleNode = readNonNegativeIntEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_MAX_BUNDLE_NODE",
                      6,
                    );
                    const visualBundleNodeCeiling = Math.max(
                      visualMaxBundleNode,
                      visualBaseBundleNode,
                    );
                    const visualSpacingDebt = Math.max(
                      0,
                      visualCandidateSpacing - visualBaseSpacing,
                    );
                    const visualSpacingDebtPerGain =
                      visualSpacingDebt / Math.max(1, visualGain);
                    const visualOverlappingEdgeDebt = Math.max(
                      0,
                      visualCandidateOverlappingEdges - visualBaseOverlappingEdges,
                    );
                    const visualOverlappingEdgeDebtPerGain =
                      visualOverlappingEdgeDebt / Math.max(1, visualGain);
                    const visualSegmentOverlapDebt = Math.max(
                      0,
                      visualCandidateEdgeSegmentOverlaps - visualBaseEdgeSegmentOverlaps,
                    );
                    const visualSegmentOverlapDebtPerGain =
                      visualSegmentOverlapDebt / Math.max(1, visualGain);
                    const visualNodeBboxGrowth =
                      visualCandidateNodeAreaB
                      / Math.max(0.001, visualBaseNodeAreaB);
                    const visualRouteBboxGrowth =
                      visualCandidateRouteAreaB
                      / Math.max(0.001, visualBaseRouteAreaB);
                    const visualRouteBboxDebt = Math.max(
                      0,
                      visualCandidateRouteAreaB - visualBaseRouteAreaB,
                    );
                    const visualRouteBboxDebtPerGain =
                      visualRouteBboxDebt / Math.max(1, visualGain);
                    const visualAllowSavedBboxHeadroom = readBoolEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_ALLOW_SAVED_BBOX_HEADROOM",
                      true,
                    );
                    const visualNodeBboxHeadroomOk =
                      visualAllowSavedBboxHeadroom
                      && visualCandidateNodeAreaB <= visualPolishNodeBboxCeilingB;
                    const visualRouteBboxHeadroomOk =
                      visualAllowSavedBboxHeadroom
                      && visualCandidateRouteAreaB <= visualPolishRouteBboxCeilingB;
                    const visualGainOk =
                      visualGain >= visualMinGain
                      && visualGainRatio >= visualMinGainRatio;
                    const visualNodeBboxOk =
                      visualNodeBboxGrowth <= visualNodeBboxGrowthLimit
                      || visualNodeBboxHeadroomOk;
                    const visualRouteBboxDebtOk =
                      visualRouteBboxDebtPerGain
                      <= visualMaxRouteBboxDebtPerGain;
                    const visualRouteBboxOk =
                      (
                        visualRouteBboxGrowth <= visualRouteBboxGrowthLimit
                        || visualRouteBboxHeadroomOk
                      )
                      && visualRouteBboxDebtOk;
                    const visualSpacingOk =
                      visualSpacingDebt <= visualMaxSpacingDebt
                      && visualSpacingDebtPerGain <= visualMaxSpacingDebtPerGain;
                    const visualSegmentOverlapOk =
                      visualSegmentOverlapDebt <= visualMaxSegmentOverlapDebt
                      && visualSegmentOverlapDebtPerGain
                      <= visualMaxSegmentOverlapDebtPerGain;
                    const visualOverlappingEdgeOk =
                      visualOverlappingEdgeDebt <= visualMaxOverlappingEdgeDebt
                      && visualOverlappingEdgeDebtPerGain
                      <= visualMaxOverlappingEdgeDebtPerGain;
                    const visualNodeOverlapOk =
                      visualCandidateNodeOverlaps === 0;
                    const visualBundleNodeOk =
                      visualCandidateBundleNode <= visualBundleNodeCeiling;
                    const visualAccept =
                      visualGainOk
                      && visualNodeBboxOk
                      && visualRouteBboxOk
                      && visualSpacingOk
                      && visualOverlappingEdgeOk
                      && visualSegmentOverlapOk
                      && visualNodeOverlapOk
                      && visualBundleNodeOk
                      && visualCanonicalNonRegression.ok;
                    const visualCandidateForBest: VisualCrossPolishCandidate = {
                      bundleEdge: visualCandidateBundleEdge,
                      bundleNode: visualCandidateBundleNode,
                      canonicalAdjacent:
                        visualCanonicalNonRegression.adjacentCandidate,
                      canonicalNodeHits:
                        visualCanonicalNonRegression.nodeHitsCandidate,
                      edgeCross: visualCandidateEdgeCross,
                      edgeNode: visualCandidateEdgeNode,
                      edgeSegmentOverlap: visualCandidateEdgeSegmentOverlaps,
                      gain: visualGain,
                      gainRatio: visualGainRatio,
                      layout: visualCandidate,
                      overlappingEdges: visualCandidateOverlappingEdges,
                      routeAreaB: visualCandidateRouteAreaB,
                      stdout: visualReroute.stdout,
                      variant: visualVariant,
                      visual: visualCandidateVisual,
                    };
                    const visualPromoted =
                      visualAccept
                      && visualCandidateBeatsBest(visualCandidateForBest);
                    logger?.info(
                      `[visual-cross polish:${visualVariant}] candidate done in `
                      + `${Date.now() - visualStart}ms · `
                      + `round=${visualRound}/${visualRounds} · `
                      + `visual=${visualBaseVisual}->${visualCandidateVisual} · `
                      + `visualGain=${visualGain}`
                      + `/${visualMinGain} `
                      + `(${visualGainRatio.toFixed(3)}`
                      + `/${visualMinGainRatio.toFixed(3)}) · `
                      + `edgeCross=${visualBaseEdgeCross}->${visualCandidateEdgeCross} · `
                      + `edgeNode=${visualBaseEdgeNode}->${visualCandidateEdgeNode} · `
                      + `bundleEdge=${visualBaseBundleEdge}->${visualCandidateBundleEdge} · `
                      + `overlappingEdges=${visualBaseOverlappingEdges}`
                      + `->${visualCandidateOverlappingEdges} · `
                      + `edgeSegmentOverlaps=${visualBaseEdgeSegmentOverlaps}`
                      + `->${visualCandidateEdgeSegmentOverlaps} · `
                      + `nodeBbox=${visualBaseNodeAreaB.toFixed(2)}B->`
                      + `${visualCandidateNodeAreaB.toFixed(2)}B · `
                      + `nodeBboxGrowth=${visualNodeBboxGrowth.toFixed(3)}`
                      + `/${visualNodeBboxGrowthLimit.toFixed(3)} · `
                      + `nodeBboxHeadroomOk=${visualNodeBboxHeadroomOk}`
                      + `/${visualPolishNodeBboxCeilingB.toFixed(2)}B · `
                      + `routeBbox=${visualBaseRouteAreaB.toFixed(2)}B->`
                      + `${visualCandidateRouteAreaB.toFixed(2)}B · `
                      + `routeBboxGrowth=${visualRouteBboxGrowth.toFixed(3)}`
                      + `/${visualRouteBboxGrowthLimit.toFixed(3)} · `
                      + `routeBboxHeadroomOk=${visualRouteBboxHeadroomOk}`
                      + `/${visualPolishRouteBboxCeilingB.toFixed(2)}B · `
                      + `routeBboxDebt=${visualRouteBboxDebt.toFixed(2)}B · `
                      + `routeBboxDebtPerGain=`
                      + `${visualRouteBboxDebtPerGain.toFixed(3)}`
                      + `/${visualMaxRouteBboxDebtPerGain.toFixed(3)} · `
                      + `routeBboxDebtOk=${visualRouteBboxDebtOk} · `
                      + `bundleNode=${visualBaseBundleNode}->${visualCandidateBundleNode}`
                      + `/${visualBundleNodeCeiling}`
                      + `(max=${visualMaxBundleNode}) · `
                      + `nodeOverlaps=${visualCandidateNodeOverlaps} · `
                      + `nodeSpacing=${visualBaseSpacing}->${visualCandidateSpacing} · `
                      + `spacingDebt=${visualSpacingDebt.toFixed(1)}`
                      + `/${visualMaxSpacingDebt.toFixed(1)} · `
                      + `spacingDebtPerGain=${visualSpacingDebtPerGain.toFixed(3)}`
                      + `/${visualMaxSpacingDebtPerGain.toFixed(3)} · `
                      + `overlappingEdgeDebt=${visualOverlappingEdgeDebt.toFixed(1)}`
                      + `/${visualMaxOverlappingEdgeDebt.toFixed(1)} · `
                      + `overlappingEdgeDebtPerGain=`
                      + `${visualOverlappingEdgeDebtPerGain.toFixed(3)}`
                      + `/${visualMaxOverlappingEdgeDebtPerGain.toFixed(3)} · `
                      + `segmentOverlapDebt=${visualSegmentOverlapDebt.toFixed(1)}`
                      + `/${visualMaxSegmentOverlapDebt.toFixed(1)} · `
                      + `segmentOverlapDebtPerGain=`
                      + `${visualSegmentOverlapDebtPerGain.toFixed(3)}`
                      + `/${visualMaxSegmentOverlapDebtPerGain.toFixed(3)} · `
                      + `gainOk=${visualGainOk} · `
                      + `nodeBboxOk=${visualNodeBboxOk} · `
                      + `routeBboxOk=${visualRouteBboxOk} · `
                      + `spacingOk=${visualSpacingOk} · `
                      + `overlappingEdgeOk=${visualOverlappingEdgeOk} · `
                      + `segmentOverlapOk=${visualSegmentOverlapOk} · `
                      + `nodeOverlapOk=${visualNodeOverlapOk} · `
                      + `bundleNodeOk=${visualBundleNodeOk} · `
                      + `canonicalAdjacent=`
                      + `${visualCanonicalNonRegression.adjacentBase ?? "n/a"}`
                      + `->${visualCanonicalNonRegression.adjacentCandidate ?? "n/a"}`
                      + ` · canonicalAdjacentOk=`
                      + `${visualCanonicalNonRegression.adjacentOk} · `
                      + `canonicalNodeHits=`
                      + `${visualCanonicalNonRegression.nodeHitsBase ?? "n/a"}`
                      + `->${visualCanonicalNonRegression.nodeHitsCandidate ?? "n/a"}`
                      + ` · canonicalNodeHitsOk=`
                      + `${visualCanonicalNonRegression.nodeHitsOk} · `
                      + `eligible=${visualAccept} · `
                      + `bestSoFar=${visualPromoted}`,
                    );
                    if (visualPromoted) {
                      visualBest = visualCandidateForBest;
                    }

                    const visualSpacingRepairMaxDebt = readFloatEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_DEBT",
                      2,
                    );
                    const visualSpacingRepairMaxNodeOverlaps =
                      readNonNegativeIntEnv(
                        "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_NODE_OVERLAPS",
                        8,
                      );
                    const visualSpacingRepairMaxNodeOverlapSpacingDebt =
                      readFloatEnv(
                        "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MAX_NODE_OVERLAP_SPACING_DEBT",
                        64,
                      );
                    const visualSpacingRepairMinGain = readNonNegativeIntEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_MIN_GAIN",
                      32,
                    );
                    const visualSpacingRepairHasSpacingDebt =
                      !visualSpacingOk
                      && visualSpacingDebt > 0
                      && visualSpacingDebt <= visualSpacingRepairMaxDebt;
                    const visualSpacingRepairHasNodeOverlapDebt =
                      !visualNodeOverlapOk
                      && visualCandidateNodeOverlaps > 0
                      && visualCandidateNodeOverlaps
                      <= visualSpacingRepairMaxNodeOverlaps
                      && visualSpacingDebt
                      <= visualSpacingRepairMaxNodeOverlapSpacingDebt;
                    const visualSpacingRepairReason =
                      visualSpacingRepairHasNodeOverlapDebt
                        ? "node-overlap"
                        : visualSpacingRepairHasSpacingDebt
                          ? "spacing"
                          : "none";
                    const visualSpacingRepairWorthTrying =
                      readBoolEnv(
                        "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR",
                        true,
                      )
                      && !visualAccept
                      && visualGainOk
                      && visualNodeBboxOk
                      && visualRouteBboxOk
                      && (
                        visualSpacingRepairHasSpacingDebt
                        || visualSpacingRepairHasNodeOverlapDebt
                      )
                      && visualGain >= visualSpacingRepairMinGain
                      && visualOverlappingEdgeOk
                      && visualSegmentOverlapOk
                      && visualBundleNodeOk;
                    if (visualSpacingRepairWorthTrying) {
                      const visualRepairStart = Date.now();
                      const visualRepairId =
                        `${visualVariant}-r${visualRound}-${Date.now()}`;
                      const visualRepairPositionsPath = trackTransientPath(path.join(
                        os.tmpdir(),
                        `django-erd-visual-cross-polish-spacing-repair-${visualRepairId}.tsv`,
                      ));
                      const visualRepairRoutesPath = trackTransientPath(path.join(
                        os.tmpdir(),
                        `django-erd-visual-cross-polish-spacing-repair-${visualRepairId}-routes.tsv`,
                      ));
                      let visualRepairBudgetedTimeout:
                        BudgetedCandidateTimeout | undefined;
                      try {
                        await writePositionsTsvFromLayoutJson(
                          visualReroute.stdout,
                          visualRepairPositionsPath,
                        );
                        await writeRoutesTsvFromLayoutJson(
                          visualReroute.stdout,
                          visualRepairRoutesPath,
                        );
                        const visualRepairTimeoutMs = readPositiveIntEnv(
                          "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_SPACING_REPAIR_TIMEOUT_MS",
                          60_000,
                        );
                        visualRepairBudgetedTimeout = budgetCandidateTimeout(
                          postReroutePolishDeadline,
                          visualRepairTimeoutMs,
                          `visual-cross polish:${visualVariant}:spacing-repair`,
                          logger,
                        );
                        if (!visualRepairBudgetedTimeout) {
                          visualPolishBudgetStopped = true;
                          continue;
                        }
                        const visualRepair = await execFileAsync(
                          binaryPath,
                          [
                            "layout",
                            "--mode", normalizedRequestedLayoutMode,
                            "--nodes-file", nodesPath,
                            "--edges-file", edgesPath,
                            "--edge-routing", effectiveEdgeRouting,
                            "--cluster-graph", "1",
                            "--positions-tsv", visualRepairPositionsPath,
                            "--routes-tsv", visualRepairRoutesPath,
                          ],
                          {
                            cwd: extensionRootPath,
                            env: ogdfOptimizedVisualCrossPolishSpacingRepairEnv(),
                            killSignal: "SIGKILL",
                            maxBuffer: 100 * 1024 * 1024,
                            timeout: visualRepairBudgetedTimeout.timeoutMs,
                          },
                        );
                        if (visualRepair.stderr.trim().length > 0) {
                          logger?.info(
                            `[visual-cross polish:${visualVariant}:spacing-repair] `
                            + `OGDF stderr: ${visualRepair.stderr.trim()}`,
                          );
                        }
                        const visualRepairCandidate = JSON.parse(visualRepair.stdout);
                        streamIntermediateLayout("polish", visualRepairCandidate);
                        const visualRepairLayout =
                          decodeLayoutSnapshot(visualRepairCandidate, "ogdfLayout");
                        const visualRepairSummary =
                          summarizeLayout(visualRepairLayout);
                        const visualRepairNodeAreaB =
                          visualRepairSummary.nodeBBoxWidth
                          * visualRepairSummary.nodeBBoxHeight
                          / 1e9;
                        const visualRepairRouteAreaB =
                          visualRepairSummary.routeBBoxWidth
                          * visualRepairSummary.routeBBoxHeight
                          / 1e9;
                        const visualRepairMetadata =
                          visualRepairCandidate?.engineMetadata ?? {};
                        const visualRepairCanonicalNonRegression =
                          evaluateCanonicalCrossingNonRegression(
                            visualBaseMetadata,
                            visualRepairMetadata,
                          );
                        const visualRepairVisual =
                          Number(visualRepairMetadata.visualCrossings ?? 0);
                        const visualRepairEdgeCross =
                          Number(visualRepairMetadata.edgeCrossings ?? 0);
                        const visualRepairEdgeNode =
                          Number(visualRepairMetadata.edgeNodeIntersections ?? 0);
                        const visualRepairBundleEdge =
                          Number(visualRepairMetadata.bundleEdgeIntersections ?? 0);
                        const visualRepairOverlappingEdges =
                          Number(visualRepairMetadata.overlappingEdges ?? 0);
                        const visualRepairEdgeSegmentOverlaps =
                          Number(visualRepairMetadata.edgeSegmentOverlaps ?? 0);
                        const visualRepairNodeOverlaps =
                          Number(visualRepairMetadata.nodeOverlaps ?? 0);
                        const visualRepairBundleNode =
                          Number(visualRepairMetadata.bundleNodeOverlaps ?? 0);
                        const visualRepairSpacing =
                          Number(visualRepairMetadata.nodeSpacingOverlaps ?? 0);
                        const visualRepairGain = Math.max(
                          0,
                          visualBaseVisual - visualRepairVisual,
                        );
                        const visualRepairGainRatio = ratioGain(
                          visualBaseVisual,
                          visualRepairVisual,
                        );
                        const visualRepairSpacingDebt = Math.max(
                          0,
                          visualRepairSpacing - visualBaseSpacing,
                        );
                        const visualRepairSpacingDebtPerGain =
                          visualRepairSpacingDebt / Math.max(1, visualRepairGain);
                        const visualRepairOverlappingEdgeDebt = Math.max(
                          0,
                          visualRepairOverlappingEdges - visualBaseOverlappingEdges,
                        );
                        const visualRepairOverlappingEdgeDebtPerGain =
                          visualRepairOverlappingEdgeDebt
                          / Math.max(1, visualRepairGain);
                        const visualRepairSegmentOverlapDebt = Math.max(
                          0,
                          visualRepairEdgeSegmentOverlaps
                          - visualBaseEdgeSegmentOverlaps,
                        );
                        const visualRepairSegmentOverlapDebtPerGain =
                          visualRepairSegmentOverlapDebt
                          / Math.max(1, visualRepairGain);
                        const visualRepairNodeBboxGrowth =
                          visualRepairNodeAreaB
                          / Math.max(0.001, visualBaseNodeAreaB);
                        const visualRepairRouteBboxGrowth =
                          visualRepairRouteAreaB
                          / Math.max(0.001, visualBaseRouteAreaB);
                        const visualRepairRouteBboxDebt = Math.max(
                          0,
                          visualRepairRouteAreaB - visualBaseRouteAreaB,
                        );
                        const visualRepairRouteBboxDebtPerGain =
                          visualRepairRouteBboxDebt
                          / Math.max(1, visualRepairGain);
                        const visualRepairNodeBboxHeadroomOk =
                          visualAllowSavedBboxHeadroom
                          && visualRepairNodeAreaB <= visualPolishNodeBboxCeilingB;
                        const visualRepairRouteBboxHeadroomOk =
                          visualAllowSavedBboxHeadroom
                          && visualRepairRouteAreaB <= visualPolishRouteBboxCeilingB;
                        const visualRepairGainOk =
                          visualRepairGain >= visualMinGain
                          && visualRepairGainRatio >= visualMinGainRatio;
                        const visualRepairNodeBboxOk =
                          visualRepairNodeBboxGrowth <= visualNodeBboxGrowthLimit
                          || visualRepairNodeBboxHeadroomOk;
                        const visualRepairRouteBboxDebtOk =
                          visualRepairRouteBboxDebtPerGain
                          <= visualMaxRouteBboxDebtPerGain;
                        const visualRepairRouteBboxOk =
                          (
                            visualRepairRouteBboxGrowth <= visualRouteBboxGrowthLimit
                            || visualRepairRouteBboxHeadroomOk
                          )
                          && visualRepairRouteBboxDebtOk;
                        const visualRepairSpacingOk =
                          visualRepairSpacingDebt <= visualMaxSpacingDebt
                          && visualRepairSpacingDebtPerGain
                          <= visualMaxSpacingDebtPerGain;
                        const visualRepairSegmentOverlapOk =
                          visualRepairSegmentOverlapDebt
                          <= visualMaxSegmentOverlapDebt
                          && visualRepairSegmentOverlapDebtPerGain
                          <= visualMaxSegmentOverlapDebtPerGain;
                        const visualRepairOverlappingEdgeOk =
                          visualRepairOverlappingEdgeDebt
                          <= visualMaxOverlappingEdgeDebt
                          && visualRepairOverlappingEdgeDebtPerGain
                          <= visualMaxOverlappingEdgeDebtPerGain;
                        const visualRepairNodeOverlapOk =
                          visualRepairNodeOverlaps === 0;
                        const visualRepairBundleNodeOk =
                          visualRepairBundleNode <= visualBundleNodeCeiling;
                        const visualRepairAccept =
                          visualRepairGainOk
                          && visualRepairNodeBboxOk
                          && visualRepairRouteBboxOk
                          && visualRepairSpacingOk
                          && visualRepairOverlappingEdgeOk
                          && visualRepairSegmentOverlapOk
                          && visualRepairNodeOverlapOk
                          && visualRepairBundleNodeOk
                          && visualRepairCanonicalNonRegression.ok;
                        const visualRepairCandidateForBest:
                          VisualCrossPolishCandidate = {
                            bundleEdge: visualRepairBundleEdge,
                            bundleNode: visualRepairBundleNode,
                            canonicalAdjacent:
                              visualRepairCanonicalNonRegression.adjacentCandidate,
                            canonicalNodeHits:
                              visualRepairCanonicalNonRegression.nodeHitsCandidate,
                            edgeCross: visualRepairEdgeCross,
                            edgeNode: visualRepairEdgeNode,
                            edgeSegmentOverlap:
                              visualRepairEdgeSegmentOverlaps,
                            gain: visualRepairGain,
                            gainRatio: visualRepairGainRatio,
                            layout: visualRepairCandidate,
                            overlappingEdges: visualRepairOverlappingEdges,
                            routeAreaB: visualRepairRouteAreaB,
                            stdout: visualRepair.stdout,
                            variant: visualVariant,
                            visual: visualRepairVisual,
                          };
                        const visualRepairPromoted =
                          visualRepairAccept
                          && visualCandidateBeatsBest(visualRepairCandidateForBest);
                        logger?.info(
                          `[visual-cross polish:${visualVariant}:spacing-repair] `
                          + `candidate done in ${Date.now() - visualRepairStart}ms · `
                          + `round=${visualRound}/${visualRounds} · `
                          + `repairReason=${visualSpacingRepairReason} · `
                          + `sourceNodeOverlaps=${visualCandidateNodeOverlaps} · `
                          + `sourceSpacingDebt=${visualSpacingDebt.toFixed(1)} · `
                          + `visual=${visualBaseVisual}->${visualRepairVisual} · `
                          + `visualGain=${visualRepairGain}`
                          + `/${visualMinGain} `
                          + `(${visualRepairGainRatio.toFixed(3)}`
                          + `/${visualMinGainRatio.toFixed(3)}) · `
                          + `edgeCross=${visualBaseEdgeCross}->${visualRepairEdgeCross} · `
                          + `edgeNode=${visualBaseEdgeNode}->${visualRepairEdgeNode} · `
                          + `bundleEdge=${visualBaseBundleEdge}->${visualRepairBundleEdge} · `
                          + `overlappingEdges=${visualBaseOverlappingEdges}`
                          + `->${visualRepairOverlappingEdges} · `
                          + `edgeSegmentOverlaps=${visualBaseEdgeSegmentOverlaps}`
                          + `->${visualRepairEdgeSegmentOverlaps} · `
                          + `nodeBbox=${visualBaseNodeAreaB.toFixed(2)}B->`
                          + `${visualRepairNodeAreaB.toFixed(2)}B · `
                          + `nodeBboxGrowth=${visualRepairNodeBboxGrowth.toFixed(3)}`
                          + `/${visualNodeBboxGrowthLimit.toFixed(3)} · `
                          + `routeBbox=${visualBaseRouteAreaB.toFixed(2)}B->`
                          + `${visualRepairRouteAreaB.toFixed(2)}B · `
                          + `routeBboxGrowth=${visualRepairRouteBboxGrowth.toFixed(3)}`
                          + `/${visualRouteBboxGrowthLimit.toFixed(3)} · `
                          + `routeBboxDebtPerGain=`
                          + `${visualRepairRouteBboxDebtPerGain.toFixed(3)}`
                          + `/${visualMaxRouteBboxDebtPerGain.toFixed(3)} · `
                          + `bundleNode=${visualBaseBundleNode}->`
                          + `${visualRepairBundleNode}/${visualBundleNodeCeiling} · `
                          + `nodeOverlaps=${visualRepairNodeOverlaps} · `
                          + `nodeSpacing=${visualBaseSpacing}->${visualRepairSpacing} · `
                          + `spacingDebt=${visualRepairSpacingDebt.toFixed(1)}`
                          + `/${visualMaxSpacingDebt.toFixed(1)} · `
                          + `spacingDebtPerGain=`
                          + `${visualRepairSpacingDebtPerGain.toFixed(3)}`
                          + `/${visualMaxSpacingDebtPerGain.toFixed(3)} · `
                          + `overlappingEdgeDebt=`
                          + `${visualRepairOverlappingEdgeDebt.toFixed(1)}`
                          + `/${visualMaxOverlappingEdgeDebt.toFixed(1)} · `
                          + `segmentOverlapDebt=`
                          + `${visualRepairSegmentOverlapDebt.toFixed(1)}`
                          + `/${visualMaxSegmentOverlapDebt.toFixed(1)} · `
                          + `gainOk=${visualRepairGainOk} · `
                          + `nodeBboxOk=${visualRepairNodeBboxOk} · `
                          + `routeBboxOk=${visualRepairRouteBboxOk} · `
                          + `spacingOk=${visualRepairSpacingOk} · `
                          + `overlappingEdgeOk=${visualRepairOverlappingEdgeOk} · `
                          + `segmentOverlapOk=${visualRepairSegmentOverlapOk} · `
                          + `nodeOverlapOk=${visualRepairNodeOverlapOk} · `
                          + `bundleNodeOk=${visualRepairBundleNodeOk} · `
                          + `canonicalAdjacent=`
                          + `${visualRepairCanonicalNonRegression.adjacentBase ?? "n/a"}`
                          + `->`
                          + `${visualRepairCanonicalNonRegression.adjacentCandidate ?? "n/a"}`
                          + ` · canonicalAdjacentOk=`
                          + `${visualRepairCanonicalNonRegression.adjacentOk} · `
                          + `canonicalNodeHits=`
                          + `${visualRepairCanonicalNonRegression.nodeHitsBase ?? "n/a"}`
                          + `->`
                          + `${visualRepairCanonicalNonRegression.nodeHitsCandidate ?? "n/a"}`
                          + ` · canonicalNodeHitsOk=`
                          + `${visualRepairCanonicalNonRegression.nodeHitsOk} · `
                          + `eligible=${visualRepairAccept} · `
                          + `bestSoFar=${visualRepairPromoted}`,
                        );
                        if (visualRepairPromoted) {
                          visualBest = visualRepairCandidateForBest;
                        }
                      } catch (err) {
                        const budgetFailure = isBudgetCausedCandidateFailure(
                          err,
                          visualRepairBudgetedTimeout,
                        );
                        if (budgetFailure) {
                          visualPolishBudgetStopped = true;
                          logger?.info(
                            `[post-reroute polish budget] visual-cross polish `
                            + `stopped; budget-limited spacing-repair timed out · `
                            + `variant=${visualVariant}`,
                          );
                        } else {
                          const msg = err instanceof Error
                            ? err.message
                            : String(err);
                          logger?.warn(
                            `[visual-cross polish:${visualVariant}:spacing-repair] `
                            + `candidate failed in `
                            + `${Date.now() - visualRepairStart}ms: ${msg}`,
                          );
                        }
                      }
                    }
                  } catch (err) {
                    const budgetFailure = isBudgetCausedCandidateFailure(
                      err,
                      visualBudgetedTimeout,
                    );
                    if (budgetFailure) {
                      visualPolishBudgetStopped = true;
                      logger?.info(
                        `[post-reroute polish budget] visual-cross polish stopped; `
                        + `budget-limited candidate timed out · `
                        + `variant=${visualVariant}`,
                      );
                      break;
                    }
                    const msg = err instanceof Error ? err.message : String(err);
                    logger?.warn(
                      `[visual-cross polish:${visualVariant}] candidate failed in `
                      + `${Date.now() - visualStart}ms: ${msg}`,
                    );
                  }
                }

                if (!visualBest) {
                  logger?.info(
                    `[visual-cross polish] round ${visualRound}/${visualRounds} `
                    + (visualPolishBudgetStopped
                      ? `stopped by post-reroute budget from visual=${visualBaseVisual}`
                      : `no eligible candidate from visual=${visualBaseVisual}`),
                  );
                  break;
                }

                acceptedStdout = visualBest.stdout;
                acceptedLayout = visualBest.layout;
                logger?.info(
                  `[visual-cross polish] round ${visualRound}/${visualRounds} `
                  + `accepted variant=${visualBest.variant} · `
                  + `visual=${visualBaseVisual}->${visualBest.visual} · `
                  + `visualGain=${visualBest.gain} `
                  + `(${visualBest.gainRatio.toFixed(3)}) · `
                  + `edgeCross=${Number(visualBaseMetadata.edgeCrossings ?? 0)}`
                  + `->${visualBest.edgeCross} · `
                  + `edgeNode=${Number(visualBaseMetadata.edgeNodeIntersections ?? 0)}`
                  + `->${visualBest.edgeNode} · `
                  + `bundleEdge=${Number(visualBaseMetadata.bundleEdgeIntersections ?? 0)}`
                  + `->${visualBest.bundleEdge} · `
                  + `bundleNode=${Number(visualBaseMetadata.bundleNodeOverlaps ?? 0)}`
                  + `->${visualBest.bundleNode} · `
                  + `overlappingEdges=${Number(visualBaseMetadata.overlappingEdges ?? 0)}`
                  + `->${visualBest.overlappingEdges} · `
                  + `edgeSegmentOverlaps=${Number(visualBaseMetadata.edgeSegmentOverlaps ?? 0)}`
                  + `->${visualBest.edgeSegmentOverlap} · `
                  + `canonicalAdjacent=`
                  + `${visualBaseMetadata.canonicalCrossing?.adjacentEdgeIntersections ?? "n/a"}`
                  + `->${visualBest.canonicalAdjacent ?? "n/a"} · `
                  + `canonicalNodeHits=`
                  + `${visualBaseMetadata.canonicalCrossing?.nonIncidentNodeHits ?? "n/a"}`
                  + `->${visualBest.canonicalNodeHits ?? "n/a"} · `
                  + `routeBbox=${visualBaseRouteAreaB.toFixed(2)}B`
                  + `->${visualBest.routeAreaB.toFixed(2)}B`,
	                );
	              }
	            }

            if (
              readBoolEnv("DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT", true)
              && preVisualPolishVisual > 0
              && preVisualPolishNodeAreaB > 0
            ) {
              const recompactBaseLayout =
                decodeLayoutSnapshot(acceptedLayout, "ogdfLayout");
              const recompactBaseSummary = summarizeLayout(recompactBaseLayout);
              const recompactBaseNodeAreaB =
                recompactBaseSummary.nodeBBoxWidth
                * recompactBaseSummary.nodeBBoxHeight
                / 1e9;
              const recompactBaseRouteAreaB =
                recompactBaseSummary.routeBBoxWidth
                * recompactBaseSummary.routeBBoxHeight
                / 1e9;
              const recompactBaseMetadata =
                acceptedLayout?.engineMetadata ?? {};
              const recompactBaseVisual =
                Number(recompactBaseMetadata.visualCrossings ?? 0);
              const recompactBaseEdgeCross =
                Number(recompactBaseMetadata.edgeCrossings ?? 0);
              const recompactBaseEdgeNode =
                Number(recompactBaseMetadata.edgeNodeIntersections ?? 0);
              const recompactBaseBundleEdge =
                Number(recompactBaseMetadata.bundleEdgeIntersections ?? 0);
              const recompactBaseBundleNode =
                Number(recompactBaseMetadata.bundleNodeOverlaps ?? 0);
              const recompactBaseOverlappingEdges =
                Number(recompactBaseMetadata.overlappingEdges ?? 0);
              const recompactBaseEdgeSegmentOverlaps =
                Number(recompactBaseMetadata.edgeSegmentOverlaps ?? 0);
              const recompactBaseSpacing =
                Number(recompactBaseMetadata.nodeSpacingOverlaps ?? 0);
              const recompactGrowthTrigger = readFloatEnv(
                "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_GROWTH_TRIGGER",
                1.08,
              );
              const recompactTargetB = readFloatEnv(
                "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_B",
                readFloatEnv(
                  "DJERD_OPTIMIZED_BBOX_TARGET_B",
                  DEFAULT_OPTIMIZED_BBOX_TARGET_B,
                ),
              );
              const recompactTargetTolerance = readFloatEnv(
                "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_TOLERANCE",
                1.02,
              );
              const recompactVisualGain =
                Math.max(0, preVisualPolishVisual - recompactBaseVisual);
              const recompactAboveTarget =
                recompactTargetB > 0
                && recompactBaseNodeAreaB
                > recompactTargetB * recompactTargetTolerance;
              const recompactWorthTrying =
                recompactVisualGain > 0
                && (
                  recompactBaseNodeAreaB
                  > preVisualPolishNodeAreaB * recompactGrowthTrigger
                  || recompactAboveTarget
                );

              if (recompactWorthTrying) {
                const recompactTargetGrowth = readFloatEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_GROWTH",
                  1.03,
                );
                const recompactFloorTargetB = Math.max(
                  0.05,
                  Math.min(
                    recompactTargetB > 0
                      ? recompactTargetB
                      : Number.POSITIVE_INFINITY,
                    preVisualPolishNodeAreaB * recompactTargetGrowth,
                    recompactBaseNodeAreaB * 0.97,
                  ),
                );
                const recompactMinBboxGain = readFloatEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_MIN_BBOX_GAIN",
                  0.015,
                );
                const recompactTargetRatios = readFloatListEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGET_RATIOS",
                  [0.95, 0.90, 0.85, 0.75, 0.65],
                );
                const recompactExplicitTargetsB = readFloatListEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TARGETS_B",
                  [],
                );
                const recompactTargetsB = buildVisualCrossRecompactTargets(
                  recompactBaseNodeAreaB,
                  recompactFloorTargetB,
                  recompactTargetRatios,
                  recompactExplicitTargetsB,
                );
                const recompactMaxVisualDebt = readNonNegativeIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_MAX_VISUAL_DEBT",
                  Math.max(
                    3,
                    Math.min(8, Math.floor(recompactVisualGain * 0.08)),
                  ),
                );
                const recompactMaxEdgeNodeDebt = readNonNegativeIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_MAX_EDGE_NODE_DEBT",
                  8,
                );
                const recompactRouteBboxGrowthLimit = readFloatEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_ROUTE_BBOX_GROWTH_LIMIT",
                  1.01,
                );
                const recompactTimeoutMs = readPositiveIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_TIMEOUT_MS",
                  60_000,
                );
                const recompactPreserveRoutes = readBoolEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_PRESERVE_ROUTES",
                  true,
                );
                const recompactSafeties = readBboxTargetSafeties().slice(
                  0,
                  readPositiveIntEnv(
                    "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_SAFETY_LIMIT",
                    1,
                  ),
                );
                const recompactStrategies =
                  readBboxTargetPositionStrategies().slice(
                    0,
                    readPositiveIntEnv(
                      "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_STRATEGY_LIMIT",
                      1,
                    ),
                  );
                const recompactVariantLimit = readNonNegativeIntEnv(
                  "DJERD_OPTIMIZED_VISUAL_CROSS_RECOMPACT_VARIANT_LIMIT",
                  2,
                );
                const recompactVariants = readBboxTargetVariants().slice(
                  0,
                  recompactVariantLimit,
                );
                type VisualCrossRecompactCandidate = {
                  bboxGain: number;
                  edgeCross: number;
                  edgeNode: number;
                  layout: typeof acceptedLayout;
                  nodeAreaB: number;
                  routeAreaB: number;
                  safety: number;
                  stdout: string;
                  strategy: BboxTargetPositionStrategy;
                  targetAreaB: number;
                  variant: string;
                  visual: number;
                };
                let recompactBest:
                  VisualCrossRecompactCandidate | undefined;
                const recompactCandidateBeatsBest = (
                  candidate: VisualCrossRecompactCandidate,
                ): boolean =>
                  recompactBest === undefined
                  || candidate.nodeAreaB < recompactBest.nodeAreaB
                  || (
                    candidate.nodeAreaB === recompactBest.nodeAreaB
                    && candidate.visual < recompactBest.visual
                  )
                  || (
                    candidate.nodeAreaB === recompactBest.nodeAreaB
                    && candidate.visual === recompactBest.visual
                    && candidate.edgeNode < recompactBest.edgeNode
                  );
                logger?.info(
                  `[visual-cross recompact] targets=${recompactTargetsB
                    .map((target) => `${target.toFixed(2)}B`)
                    .join(",")} · `
                  + `floor=${recompactFloorTargetB.toFixed(2)}B · `
                  + `preVisual=${preVisualPolishVisual} · `
                  + `baseVisual=${recompactBaseVisual} · `
                  + `visualGain=${recompactVisualGain} · `
                  + `goal=${recompactTargetB.toFixed(2)}B`
                  + `/${recompactTargetTolerance.toFixed(3)} · `
                  + `aboveTarget=${recompactAboveTarget} · `
                  + `nodeBbox=${preVisualPolishNodeAreaB.toFixed(2)}B->`
                  + `${recompactBaseNodeAreaB.toFixed(2)}B · `
                  + `routeBbox=${preVisualPolishRouteAreaB.toFixed(2)}B->`
                  + `${recompactBaseRouteAreaB.toFixed(2)}B`,
                );
                let recompactBudgetStopped = false;
                recompactSearch:
                for (const recompactTargetB of recompactTargetsB) {
                  for (const recompactSafety of recompactSafeties) {
                    for (const recompactStrategy of recompactStrategies) {
                      const recompactStart = Date.now();
                      try {
                        const recompactTarget =
                          await writePositionsTsvForBBoxTarget(
                            acceptedLayout,
                            path.join(
                              os.tmpdir(),
                              `django-erd-visual-cross-recompact-${Date.now()}`
                              + `-${Math.round(recompactTargetB * 1000)}b`
                              + `-${Math.round(recompactSafety * 1000)}`
                              + `-${recompactStrategy}.tsv`,
                            ),
                            recompactTargetB,
                            recompactSafety,
                            recompactStrategy,
                          );
                        if (recompactTarget === undefined) {
                          continue;
                        }
                        transientPaths.add(recompactTarget.positionsPath);
                        const recompactSpecs: Array<{
                          env: Record<string, string | undefined>;
                          routesPath?: string;
                          variant: string;
                        }> = [];
                        if (
                          recompactPreserveRoutes
                          && recompactTarget.transformPoint
                        ) {
                          const recompactRoutesPath = trackTransientPath(
                            path.join(
                              os.tmpdir(),
                              `django-erd-visual-cross-recompact-${Date.now()}`
                              + `-${Math.round(recompactTargetB * 1000)}b`
                              + `-${Math.round(recompactSafety * 1000)}`
                              + `-${recompactStrategy}-routes.tsv`,
                            ),
                          );
                          await writeTransformedRoutesTsvFromLayoutJson(
                            JSON.stringify(acceptedLayout),
                            recompactRoutesPath,
                            recompactTarget.transformPoint,
                          );
                          recompactSpecs.push({
                            env: ogdfOptimizedVisualCrossRecompactPreserveEnv(),
                            routesPath: recompactRoutesPath,
                            variant: "preserve-routes",
                          });
                        }
                        for (const recompactVariant of recompactVariants) {
                          recompactSpecs.push({
                            env:
                              ogdfOptimizedBboxTargetEnvForVariant(
                                recompactVariant,
                              ),
                            variant: recompactVariant,
                          });
                        }
                        for (const recompactSpec of recompactSpecs) {
                        if (
                          remainingPostReroutePolishBudgetMs(
                            postReroutePolishDeadline,
                          ) <= 0
                        ) {
                          recompactBudgetStopped = true;
                          logger?.info(
                            `[post-reroute polish budget] visual-cross `
                            + `recompact stopped; budget exhausted · `
                            + `nextVariant=${recompactSpec.variant}`,
                          );
                          break recompactSearch;
                        }
                        const recompactVariantStart = Date.now();
                        let recompactBudgetedTimeout:
                          BudgetedCandidateTimeout | undefined;
                        try {
                          const recompactArgs = [
                            "layout",
                            "--mode", normalizedRequestedLayoutMode,
                            "--nodes-file", nodesPath,
                            "--edges-file", edgesPath,
                            "--edge-routing", effectiveEdgeRouting,
                            "--cluster-graph", "1",
                            "--positions-tsv", recompactTarget.positionsPath,
                          ];
                          if (recompactSpec.routesPath) {
                            recompactArgs.push(
                              "--routes-tsv",
                              recompactSpec.routesPath,
                            );
                          }
                          recompactBudgetedTimeout = budgetCandidateTimeout(
                            postReroutePolishDeadline,
                            recompactTimeoutMs,
                            `visual-cross recompact:${recompactSpec.variant}`,
                            logger,
                          );
                          if (!recompactBudgetedTimeout) {
                            recompactBudgetStopped = true;
                            break recompactSearch;
                          }
                          const recompactReroute = await execFileAsync(
                            binaryPath,
                            recompactArgs,
                            {
                              cwd: extensionRootPath,
                              env: recompactSpec.env,
                              killSignal: "SIGKILL",
                              maxBuffer: 100 * 1024 * 1024,
                              timeout: recompactBudgetedTimeout.timeoutMs,
                            },
                          );
                          if (recompactReroute.stderr.trim().length > 0) {
                            logger?.info(
                              `[visual-cross recompact:${recompactSpec.variant}] `
                              + `OGDF stderr: `
                              + recompactReroute.stderr.trim(),
                            );
                          }
                          const recompactCandidate =
                            JSON.parse(recompactReroute.stdout);
                          streamIntermediateLayout(
                            "bbox",
                            recompactCandidate,
                          );
                          const recompactCandidateLayout =
                            decodeLayoutSnapshot(
                              recompactCandidate,
                              "ogdfLayout",
                            );
                          const recompactCandidateSummary =
                            summarizeLayout(recompactCandidateLayout);
                          const recompactCandidateNodeAreaB =
                            recompactCandidateSummary.nodeBBoxWidth
                            * recompactCandidateSummary.nodeBBoxHeight
                            / 1e9;
                          const recompactCandidateRouteAreaB =
                            recompactCandidateSummary.routeBBoxWidth
                            * recompactCandidateSummary.routeBBoxHeight
                            / 1e9;
                          const recompactCandidateMetadata =
                            recompactCandidate?.engineMetadata ?? {};
                          const recompactCandidateVisual =
                            Number(
                              recompactCandidateMetadata.visualCrossings ?? 0,
                            );
                          const recompactCandidateEdgeCross =
                            Number(
                              recompactCandidateMetadata.edgeCrossings ?? 0,
                            );
                          const recompactCandidateEdgeNode =
                            Number(
                              recompactCandidateMetadata
                                .edgeNodeIntersections ?? 0,
                            );
                          const recompactCandidateBundleEdge =
                            Number(
                              recompactCandidateMetadata
                                .bundleEdgeIntersections ?? 0,
                            );
                          const recompactCandidateBundleNode =
                            Number(
                              recompactCandidateMetadata.bundleNodeOverlaps ?? 0,
                            );
                          const recompactCandidateOverlappingEdges =
                            Number(
                              recompactCandidateMetadata.overlappingEdges ?? 0,
                            );
                          const recompactCandidateEdgeSegmentOverlaps =
                            Number(
                              recompactCandidateMetadata.edgeSegmentOverlaps
                              ?? 0,
                            );
                          const recompactCandidateNodeOverlaps =
                            Number(
                              recompactCandidateMetadata.nodeOverlaps ?? 0,
                            );
                          const recompactCandidateSpacing =
                            Number(
                              recompactCandidateMetadata.nodeSpacingOverlaps
                              ?? 0,
                            );
                          const recompactBboxGain = ratioGain(
                            recompactBaseNodeAreaB,
                            recompactCandidateNodeAreaB,
                          );
                          const recompactVisualDebt = Math.max(
                            0,
                            recompactCandidateVisual - recompactBaseVisual,
                          );
                          const recompactEdgeNodeDebt = Math.max(
                            0,
                            recompactCandidateEdgeNode
                            - recompactBaseEdgeNode,
                          );
                          const recompactRouteBboxGrowth =
                            recompactCandidateRouteAreaB
                            / Math.max(0.001, recompactBaseRouteAreaB);
                          const recompactBboxOk =
                            recompactCandidateNodeAreaB
                            < recompactBaseNodeAreaB
                            && recompactBboxGain >= recompactMinBboxGain;
                          const recompactVisualOk =
                            recompactCandidateVisual
                            <= recompactBaseVisual + recompactMaxVisualDebt
                            && recompactCandidateVisual < preVisualPolishVisual;
                          const recompactEdgeNodeOk =
                            recompactEdgeNodeDebt <= recompactMaxEdgeNodeDebt;
                          const recompactRouteBboxOk =
                            recompactRouteBboxGrowth
                            <= recompactRouteBboxGrowthLimit;
                          const recompactDebtOk =
                            recompactCandidateNodeOverlaps === 0
                            && recompactCandidateSpacing
                            <= recompactBaseSpacing
                            && recompactCandidateBundleNode
                            <= recompactBaseBundleNode
                            && recompactCandidateOverlappingEdges
                            <= recompactBaseOverlappingEdges
                            && recompactCandidateEdgeSegmentOverlaps
                            <= recompactBaseEdgeSegmentOverlaps;
                          const recompactAccept =
                            recompactBboxOk
                            && recompactVisualOk
                            && recompactEdgeNodeOk
                            && recompactRouteBboxOk
                            && recompactDebtOk;
                          const recompactCandidateForBest:
                            VisualCrossRecompactCandidate = {
                              bboxGain: recompactBboxGain,
                              edgeCross: recompactCandidateEdgeCross,
                              edgeNode: recompactCandidateEdgeNode,
                              layout: recompactCandidate,
                              nodeAreaB: recompactCandidateNodeAreaB,
	                              routeAreaB: recompactCandidateRouteAreaB,
	                              safety: recompactSafety,
	                              stdout: recompactReroute.stdout,
	                              strategy: recompactStrategy,
	                              targetAreaB: recompactTargetB,
	                              variant: recompactSpec.variant,
	                              visual: recompactCandidateVisual,
	                            };
                          const recompactPromoted =
                            recompactAccept
                            && recompactCandidateBeatsBest(
                              recompactCandidateForBest,
                            );
                          logger?.info(
                            `[visual-cross recompact:${recompactSpec.variant}] `
	                            + `candidate done in `
	                            + `${Date.now() - recompactVariantStart}ms · `
	                            + `target=${recompactTargetB.toFixed(2)}B · `
	                            + `safety=${recompactSafety.toFixed(3)} · `
	                            + `strategy=${recompactStrategy} · `
                            + `bbox=${recompactBaseNodeAreaB.toFixed(2)}B->`
                            + `${recompactCandidateNodeAreaB.toFixed(2)}B · `
                            + `bboxGain=${recompactBboxGain.toFixed(3)}`
                            + `/${recompactMinBboxGain.toFixed(3)} · `
                            + `routeBbox=${recompactBaseRouteAreaB.toFixed(2)}B->`
                            + `${recompactCandidateRouteAreaB.toFixed(2)}B · `
                            + `routeBboxGrowth=`
                            + `${recompactRouteBboxGrowth.toFixed(3)}`
                            + `/${recompactRouteBboxGrowthLimit.toFixed(3)} · `
                            + `visual=${recompactBaseVisual}`
                            + `->${recompactCandidateVisual} · `
                            + `visualDebt=${recompactVisualDebt}`
                            + `/${recompactMaxVisualDebt} · `
                            + `edgeCross=${recompactBaseEdgeCross}`
                            + `->${recompactCandidateEdgeCross} · `
                            + `edgeNode=${recompactBaseEdgeNode}`
                            + `->${recompactCandidateEdgeNode} · `
                            + `edgeNodeDebt=${recompactEdgeNodeDebt}`
                            + `/${recompactMaxEdgeNodeDebt} · `
                            + `bundleEdge=${recompactBaseBundleEdge}`
                            + `->${recompactCandidateBundleEdge} · `
                            + `bundleNode=${recompactBaseBundleNode}`
                            + `->${recompactCandidateBundleNode} · `
                            + `overlappingEdges=`
                            + `${recompactBaseOverlappingEdges}`
                            + `->${recompactCandidateOverlappingEdges} · `
                            + `edgeSegmentOverlaps=`
                            + `${recompactBaseEdgeSegmentOverlaps}`
                            + `->${recompactCandidateEdgeSegmentOverlaps} · `
                            + `nodeOverlaps=`
                            + `${recompactCandidateNodeOverlaps} · `
                            + `nodeSpacing=${recompactBaseSpacing}`
                            + `->${recompactCandidateSpacing} · `
                            + `bboxOk=${recompactBboxOk} · `
                            + `visualOk=${recompactVisualOk} · `
                            + `edgeNodeOk=${recompactEdgeNodeOk} · `
                            + `routeBboxOk=${recompactRouteBboxOk} · `
                            + `debtOk=${recompactDebtOk} · `
                            + `eligible=${recompactAccept} · `
                            + `bestSoFar=${recompactPromoted}`,
                          );
                          if (recompactPromoted) {
                            recompactBest = recompactCandidateForBest;
                          }
                        } catch (err) {
                          const budgetFailure = isBudgetCausedCandidateFailure(
                            err,
                            recompactBudgetedTimeout,
                          );
                          if (budgetFailure) {
                            recompactBudgetStopped = true;
                            logger?.info(
                              `[post-reroute polish budget] visual-cross `
                              + `recompact stopped; budget-limited candidate `
                              + `timed out · variant=${recompactSpec.variant}`,
                            );
                            break recompactSearch;
                          }
                          const msg = err instanceof Error
                            ? err.message
                            : String(err);
                          logger?.warn(
                            `[visual-cross recompact:${recompactSpec.variant}] `
                            + `candidate failed in `
                            + `${Date.now() - recompactVariantStart}ms · `
                            + `target=${recompactTargetB.toFixed(2)}B · `
                            + `safety=${recompactSafety.toFixed(3)} · `
                            + `strategy=${recompactStrategy}: ${msg}`,
                          );
                        }
                      }
                    } catch (err) {
                      const msg =
                        err instanceof Error ? err.message : String(err);
                      logger?.warn(
	                        `[visual-cross recompact] candidate failed in `
	                        + `${Date.now() - recompactStart}ms · `
	                        + `target=${recompactTargetB.toFixed(2)}B · `
	                        + `safety=${recompactSafety.toFixed(3)} · `
	                        + `strategy=${recompactStrategy}: ${msg}`,
	                      );
	                    }
	                  }
	                }
	                }
                if (recompactBest) {
	                  acceptedStdout = recompactBest.stdout;
	                  acceptedLayout = recompactBest.layout;
	                  logger?.info(
	                    `[visual-cross recompact] accepted `
	                    + `variant=${recompactBest.variant} · `
	                    + `target=${recompactBest.targetAreaB.toFixed(2)}B · `
	                    + `safety=${recompactBest.safety.toFixed(3)} · `
                    + `strategy=${recompactBest.strategy} · `
                    + `visual=${recompactBaseVisual}`
                    + `->${recompactBest.visual} · `
                    + `edgeCross=${recompactBaseEdgeCross}`
                    + `->${recompactBest.edgeCross} · `
                    + `edgeNode=${recompactBaseEdgeNode}`
                    + `->${recompactBest.edgeNode} · `
                    + `nodeBbox=${recompactBaseNodeAreaB.toFixed(2)}B`
                    + `->${recompactBest.nodeAreaB.toFixed(2)}B · `
                    + `routeBbox=${recompactBaseRouteAreaB.toFixed(2)}B`
                    + `->${recompactBest.routeAreaB.toFixed(2)}B · `
                    + `bboxGain=${recompactBest.bboxGain.toFixed(3)}`,
                  );
                } else {
                  logger?.info(
                    `[visual-cross recompact] no eligible candidate from `
                    + `visual=${recompactBaseVisual} · `
                    + `nodeBbox=${recompactBaseNodeAreaB.toFixed(2)}B`
                    + (recompactBudgetStopped ? " · stoppedByBudget=true" : ""),
                  );
                }
              }
            }
	          }

	          const reroutedMetadata = acceptedLayout?.engineMetadata ?? {};
          const reroutedNodeOverlaps = Number(reroutedMetadata.nodeOverlaps ?? 0);
          if (
            reroutedNodeOverlaps > 0
            && process.env.DJERD_REJECT_OPTIMIZED_NODE_OVERLAPS === "1"
          ) {
            logger?.warn(
              `[ML] v36 reroute rejected because nodeOverlaps=${reroutedNodeOverlaps}; unset DJERD_REJECT_OPTIMIZED_NODE_OVERLAPS to inspect anyway`,
            );
            mlOk = false;
          } else {
            if (reroutedNodeOverlaps > 0) {
              logger?.warn(
                `[ML] v36 reroute kept with nodeOverlaps=${reroutedNodeOverlaps}`,
              );
            }
            stdout = acceptedStdout;
          }
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          logger?.warn(`[ML] reroute failed, keeping ML-less stdout: ${msg}`);
          mlOk = false;
        }
      }

      if (!mlOk) {
        // Fallback: Y-axis compression on the baseline positions.
        try {
          const firstPass = JSON.parse(stdout);
          const ndArr = Array.isArray(firstPass.nodes) ? firstPass.nodes : [];
          if (ndArr.length > 0) {
            let cySum = 0;
            for (const n of ndArr) {
              if (n && n.position && typeof n.position.y === "number") {
                cySum += n.position.y + (n.size?.height ?? 0) / 2;
              }
            }
            const cy = cySum / ndArr.length;
            const Y_SCALE = 0.62;
            const tsvLines: string[] = ["modelId\tx\ty"];
            for (const n of ndArr) {
              if (!n || !n.modelId || !n.position) continue;
              const cx = n.position.x + (n.size?.width ?? 0) / 2;
              const ny = cy + (n.position.y + (n.size?.height ?? 0) / 2 - cy) * Y_SCALE;
              tsvLines.push(`${n.modelId}\t${cx.toFixed(3)}\t${ny.toFixed(3)}`);
            }
            const tsvPath = trackTransientPath(path.join(
              os.tmpdir(),
              `django-erd-optimized-positions-${Date.now()}.tsv`,
            ));
            await writeFile(tsvPath, tsvLines.join("\n"), "utf8");
            logger?.info(
              `[optimized fallback] Y-scale=${Y_SCALE} applied to ${ndArr.length} nodes; rerouting via positions-tsv`,
            );
            const reroute = await execFileAsync(
              binaryPath,
              [
                "layout",
                "--mode", normalizedRequestedLayoutMode,
                "--nodes-file", nodesPath,
                "--edges-file", edgesPath,
                "--edge-routing", effectiveEdgeRouting,
                ...(clusterGraphLayout ? ["--cluster-graph", "1"] : []),
                ...(bubbleLayout ? ["--bubble", "1"] : []),
                "--positions-tsv", tsvPath,
              ],
              {
                cwd: extensionRootPath,
                env: ogdfOptimizedRerouteEnv(),
                maxBuffer: 100 * 1024 * 1024,
                timeout: OGDF_LAYOUT_TIMEOUT_MS,
              },
            );
            if (reroute.stderr.trim().length > 0) {
              logger?.info(`[optimized fallback] OGDF stderr: ${reroute.stderr.trim()}`);
            }
            stdout = reroute.stdout;
          }
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          logger?.warn(`[optimized fallback] Y-scale failed: ${msg}`);
        }
      }
    }

    if (optimizedLayout && !loadedOptimizedFinalFromCache) {
      postReroutePolishDeadline ??=
        startPostReroutePolishDeadline(logger);
    }

    const visualCrossTarget =
      readOptionalPositiveIntEnv("DJERD_OPTIMIZED_VISUAL_CROSS_TARGET")
      ?? DEFAULT_VISUAL_CROSS_TARGET;
    const preFinalVisualCrossings = readVisualCrossingsFromLayoutJson(stdout);
    const runExplicitFinalExportRetouch =
      readBoolEnv("DJERD_FINAL_EXPORT_RETOUCH", false);
    const skipTargetSatisfiedFinalRetouch =
      optimizedLayout
      && !loadedOptimizedFinalFromCache
      && !runExplicitFinalExportRetouch
      && preFinalVisualCrossings !== undefined
      && preFinalVisualCrossings <= visualCrossTarget;
    if (skipTargetSatisfiedFinalRetouch) {
      logger?.info(
        `[final-export retouch] skipped because visualCrossings=${preFinalVisualCrossings}`
        + ` <= target=${visualCrossTarget}`,
      );
    } else if (
      (optimizedLayout && !loadedOptimizedFinalFromCache)
      || runExplicitFinalExportRetouch
    ) {
      if (
        postReroutePolishDeadline
        && remainingPostReroutePolishBudgetMs(
          postReroutePolishDeadline,
        ) <= 0
      ) {
        logger?.info(
          `[post-reroute polish budget] final-export retouch skipped; `
          + `budget exhausted after `
          + `${Date.now() - postReroutePolishDeadline.startedMs}ms`
          + `/${postReroutePolishDeadline.budgetMs}ms`,
        );
      } else {
        stdout = await runFinalExportRetouch({
          binaryPath,
          clusterGraphLayout,
          cwd: extensionRootPath,
          edgeRouting: effectiveEdgeRouting,
          edgesPath,
          logger,
          mode: normalizedRequestedLayoutMode,
          nodesPath,
          polishDeadline: postReroutePolishDeadline,
          stdout,
        });
      }
    }

    if (optimizedCachePath && optimizedLayout && !loadedOptimizedFinalFromCache) {
      try {
        const selection = await preserveBestOptimizedLayoutCache(
          optimizedCachePath,
          stdout,
        );
        stdout = selection.json;
        if (selection.source === "existing") {
          logger?.info(
            "OGDF optimized layout cache preserved better existing result"
            + ` · reason=${selection.preservationReason ?? "quality"}`
            + ` · existingVisual=${selection.existingVisualCrossings ?? "absent"}`
            + ` · candidateVisual=${selection.candidateVisualCrossings ?? "absent"}`
            + ` · cache=${optimizedCachePath}`,
          );
        } else {
          logger?.info(`OGDF optimized layout cached to ${optimizedCachePath}`);
        }
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        logger?.warn(`OGDF optimized layout cache save failed: ${msg}`);
      }
    }

    let layout: LayoutSnapshot;
    try {
      layout = decodeLayoutSnapshot(JSON.parse(stdout), "ogdfLayout");
    } catch (error) {
      const reason = error instanceof Error ? error.message : String(error);
      throw new Error(`invalid JSON from native layout: ${reason}`);
    }

    const summary = summarizeLayout(layout);
    const metadata = layout.engineMetadata;
    const visualCrossings = metadata?.visualCrossings;
    const visualCrossStatus =
      optimizedLayout
      && typeof visualCrossings === "number"
      && Number.isFinite(visualCrossings)
        ? visualCrossings <= visualCrossTarget ? "pass" : "miss"
        : undefined;
    const visualCrossOver =
      visualCrossStatus !== undefined
      && typeof visualCrossings === "number"
        ? Math.max(0, Math.ceil(visualCrossings - visualCrossTarget))
        : undefined;
    const qualityDegraded = visualCrossStatus === "miss";
    const qualityReason =
      qualityDegraded && visualCrossOver !== undefined
        ? `Optimized visual crossings ${visualCrossings} exceed target `
          + `${visualCrossTarget} by ${visualCrossOver}.`
        : undefined;
    logger?.info(
      [
        `OGDF layout completed in ${Date.now() - started}ms`,
        `leafBundles=${metadata?.leafBundles?.length ?? "absent"}`,
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `requested=${metadata?.requestedMode ?? normalizedRequestedLayoutMode}`,
        `reported=${layout.mode}`,
        `actual=${metadata?.actualMode ?? layout.mode}`,
        `requestedAlgorithm=${metadata?.requestedAlgorithm ?? layoutDefinition.ogdfClass}`,
        `actualAlgorithm=${metadata?.actualAlgorithm ?? getOgdfLayoutDefinition(layout.mode).ogdfClass}`,
        `strategy=${metadata?.strategy ?? "exact"}`,
        ...(metadata?.strategyReason ? [`strategyReason=${metadata.strategyReason}`] : []),
        `nodes=${layout.nodes.length}`,
        `routedEdges=${layout.routedEdges.length}`,
        `unroutedInputEdges=${Math.max(0, layoutEdges.length - layout.routedEdges.length)}`,
        `routePoints=${summary.routePointCount}`,
        ...(metadata?.routeSegments !== undefined ? [`routeSegments=${metadata.routeSegments}`] : []),
        ...(metadata?.nodeOverlaps !== undefined ? [`nodeOverlaps=${metadata.nodeOverlaps}`] : []),
        ...(metadata?.nodeSpacingOverlaps !== undefined ? [`nodeSpacingOverlaps=${metadata.nodeSpacingOverlaps}`] : []),
        ...(metadata?.rawRouteCrossings !== undefined ? [`rawRouteCrossings=${metadata.rawRouteCrossings}`] : []),
        ...(metadata?.edgeCrossings !== undefined ? [`edgeCrossings=${metadata.edgeCrossings}`] : []),
        ...(metadata?.edgeNodeIntersections !== undefined ? [`edgeNodeIntersections=${metadata.edgeNodeIntersections}`] : []),
        ...(metadata?.overlappingEdges !== undefined ? [`overlappingEdges=${metadata.overlappingEdges}`] : []),
        ...(metadata?.edgeSegmentOverlaps !== undefined ? [`edgeSegmentOverlaps=${metadata.edgeSegmentOverlaps}`] : []),
        ...(metadata?.bundleEdgeIntersections !== undefined ? [`bundleEdgeIntersections=${metadata.bundleEdgeIntersections}`] : []),
        ...(metadata?.bundleNodeOverlaps !== undefined ? [`bundleNodeOverlaps=${metadata.bundleNodeOverlaps}`] : []),
        ...(metadata?.visualCrossings !== undefined ? [`visualCrossings=${metadata.visualCrossings}`] : []),
        ...(metadata?.canonicalCrossing
          ? [
              `canonicalCrossingPairs=${metadata.canonicalCrossing.routeCrossingPairs}`,
              `canonicalCrossingLowerBound=${metadata.canonicalCrossing.lowerBound}`,
              `canonicalCrossingProper=${metadata.canonicalCrossing.properDrawing}`,
              ...(metadata.canonicalCrossing.gap !== undefined
                ? [`canonicalCrossingGap=${metadata.canonicalCrossing.gap}`]
                : []),
            ]
          : []),
        ...(visualCrossStatus !== undefined
          ? [
              `visualCrossTarget=${visualCrossTarget}`,
              `visualCrossStatus=${visualCrossStatus}`,
              `visualCrossOver=${visualCrossOver}`,
            ]
          : []),
        ...(metadata?.stressScore !== undefined ? [`stress=${metadata.stressScore.toFixed(4)}`] : []),
        ...(metadata?.edgeLengthCv !== undefined ? [`edgeLenCv=${metadata.edgeLengthCv.toFixed(3)}`] : []),
        ...(metadata?.crossingAngleMean !== undefined ? [`xAngMean=${(metadata.crossingAngleMean * 180 / Math.PI).toFixed(1)}`] : []),
        ...(metadata?.crossingAngleCv !== undefined ? [`xAngCv=${metadata.crossingAngleCv.toFixed(3)}`] : []),
        ...(metadata?.edgeBendTotal !== undefined ? [`edgeBend=${metadata.edgeBendTotal.toFixed(2)}`] : []),
        ...(metadata?.hubClearanceP10 !== undefined ? [`hubClearP10=${metadata.hubClearanceP10.toFixed(1)}`] : []),
        ...(metadata?.clusterCompactnessMean !== undefined ? [`clusterCompact=${metadata.clusterCompactnessMean.toFixed(3)}`] : []),
        ...(metadata?.crossingsPerEdgeP50 !== undefined ? [`xPerEdgeP50=${metadata.crossingsPerEdgeP50}`] : []),
        ...(metadata?.crossingsPerEdgeP90 !== undefined ? [`xPerEdgeP90=${metadata.crossingsPerEdgeP90}`] : []),
        ...(metadata?.cleanEdgeRatio !== undefined ? [`cleanEdgeRatio=${metadata.cleanEdgeRatio.toFixed(3)}`] : []),
        ...(metadata?.edgeCrossingsBetweenClusters !== undefined ? [`xBetweenClusters=${metadata.edgeCrossingsBetweenClusters}`] : []),
        ...(metadata?.nodeAreaCoverage !== undefined ? [`nodeAreaCov=${metadata.nodeAreaCoverage.toFixed(4)}`] : []),
        ...(metadata?.emptySpaceCv !== undefined ? [`emptySpaceCv=${metadata.emptySpaceCv.toFixed(3)}`] : []),
        ...(metadata?.topCrossEdges && metadata.topCrossEdges.length > 0
          ? [
              `topCrossEdges=${metadata.topCrossEdges.length}`,
              `worstEdge=${metadata.topCrossEdges[0].sourceModelId}↔${metadata.topCrossEdges[0].targetModelId}(${metadata.topCrossEdges[0].crossings})`,
            ]
          : []),
        ...(metadata?.compositeQuality !== undefined
          ? [
              `quality=${metadata.compositeQuality.toFixed(4)}`,
              `qSub=clean:${(metadata.subCleanQuality ?? 0).toFixed(2)}/sev:${(metadata.subSeverityQuality ?? 0).toFixed(2)}/ang:${(metadata.subAngleQuality ?? 0).toFixed(2)}/str:${(metadata.subStressQuality ?? 0).toFixed(2)}/cmp:${(metadata.subCompactQuality ?? 0).toFixed(2)}/uni:${(metadata.subUniformQuality ?? 0).toFixed(2)}/spr:${(metadata.subSpreadQuality ?? 0).toFixed(2)}`,
            ]
          : []),
        `stressPostPass=${process.env.DJERD_STRESS_POST_PASS_ITERS ?? "0"}`,
        `crossings=${layout.crossings.length}`,
        `nodeBBoxWidth=${summary.nodeBBoxWidth.toFixed(1)}`,
        `nodeBBoxHeight=${summary.nodeBBoxHeight.toFixed(1)}`,
        `routeBBoxWidth=${summary.routeBBoxWidth.toFixed(1)}`,
        `routeBBoxHeight=${summary.routeBBoxHeight.toFixed(1)}`,
        `bboxWidth=${summary.bboxWidth.toFixed(1)}`,
        `bboxHeight=${summary.bboxHeight.toFixed(1)}`,
      ].join(" · "),
    );
    if (visualCrossStatus === "miss" && visualCrossOver !== undefined) {
      logger?.warn(
        `OGDF visual crossing target missed · visualCrossings=${visualCrossings} · `
        + `target=${visualCrossTarget} · overBy=${visualCrossOver}`,
      );
    }

    return {
      applied: true,
      durationMs: Date.now() - started,
      engineMetadata: metadata,
      layout,
      qualityDegraded,
      reason: qualityReason,
      requestedLayoutMode: normalizedRequestedLayoutMode,
    };
  } catch (error) {
    const reason = formatOgdfFailureReason(error);
    preserveRequestDirectory = true;
    await writeFailureArtifacts(requestDirectory, error, reason);
    logger?.warn(
      [
        "OGDF layout failed; falling back to analyzer layout",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `requested=${normalizedRequestedLayoutMode}`,
        `fallback=${payload.layout.mode}`,
        `reason=${reason}`,
      ].join(" · "),
    );
    logger?.warn(
      [
        "OGDF layout input preserved for debugging",
        ...(requestId !== undefined ? [`requestId=${requestId}`] : []),
        `directory=${requestDirectory}`,
        `nodesFile=${nodesPath}`,
        `edgesFile=${edgesPath}`,
        `reproduce=${buildOgdfReproductionCommand(
          binaryPath,
          normalizedRequestedLayoutMode,
          nodesPath,
          edgesPath,
        )}`,
      ].join(" · "),
    );
    return {
      applied: false,
      durationMs: Date.now() - started,
      layout: payload.layout,
      reason,
      requestedLayoutMode: normalizedRequestedLayoutMode,
    };
  } finally {
    releaseOptimizedLayoutFlight?.();
    await Promise.all(
      [...transientPaths].map((filePath) => rm(filePath, { force: true })),
    );
    if (!preserveRequestDirectory) {
      await rm(requestDirectory, { force: true, recursive: true });
    }
  }
}

function startPostReroutePolishDeadline(
  logger?: Logger,
): PostReroutePolishDeadline {
  const budgetMs = readPositiveIntEnv(
    "DJERD_OPTIMIZED_POST_REROUTE_POLISH_BUDGET_MS",
    DEFAULT_POST_REROUTE_POLISH_BUDGET_MS,
  );
  const startedMs = Date.now();
  const deadline = {
    budgetMs,
    deadlineMs: startedMs + budgetMs,
    startedMs,
  };
  logger?.info(
    `[post-reroute polish budget] started · budget=${budgetMs}ms · `
    + "covers=edge-node,visual-cross,recompact,final-export-retouch",
  );
  return deadline;
}

function remainingPostReroutePolishBudgetMs(
  deadline: PostReroutePolishDeadline,
): number {
  return Math.max(0, deadline.deadlineMs - Date.now());
}

function budgetCandidateTimeout(
  deadline: PostReroutePolishDeadline | undefined,
  configuredTimeoutMs: number,
  stage: string,
  logger?: Logger,
): BudgetedCandidateTimeout | undefined {
  if (!deadline) {
    return { budgetLimited: false, timeoutMs: configuredTimeoutMs };
  }
  const remainingMs = remainingPostReroutePolishBudgetMs(deadline);
  if (remainingMs <= 0) {
    logger?.info(
      `[post-reroute polish budget] ${stage} skipped; `
      + `budget exhausted after ${Date.now() - deadline.startedMs}ms`
      + `/${deadline.budgetMs}ms`,
    );
    return undefined;
  }
  const timeoutMs = Math.max(1, Math.min(configuredTimeoutMs, remainingMs));
  const budgetLimited = timeoutMs < configuredTimeoutMs;
  if (budgetLimited) {
    logger?.info(
      `[post-reroute polish budget] ${stage} timeout capped · `
      + `configured=${configuredTimeoutMs}ms · timeout=${timeoutMs}ms · `
      + `remaining=${remainingMs}ms`,
    );
  }
  return { budgetLimited, timeoutMs };
}

function isBudgetCausedCandidateFailure(
  error: unknown,
  timeout: BudgetedCandidateTimeout | undefined,
): boolean {
  return timeout?.budgetLimited === true
    && error instanceof OgdfExecError
    && error.details.timedOut;
}

function readVisualCrossingsFromLayoutJson(layoutJson: string): number | undefined {
  try {
    const parsed = JSON.parse(layoutJson) as {
      engineMetadata?: { visualCrossings?: unknown };
    };
    const visualCrossings = parsed.engineMetadata?.visualCrossings;
    return typeof visualCrossings === "number" && Number.isFinite(visualCrossings)
      ? visualCrossings
      : undefined;
  } catch {
    return undefined;
  }
}

async function runFinalExportRetouch(options: {
  binaryPath: string;
  clusterGraphLayout: boolean;
  cwd: string;
  edgeRouting: EdgeRoutingStyle;
  edgesPath: string;
  logger?: Logger;
  mode: LayoutMode;
  nodesPath: string;
  polishDeadline?: PostReroutePolishDeadline;
  stdout: string;
}): Promise<string> {
  if (!readBoolEnv("DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH", true)) {
    return options.stdout;
  }

  const retouchStart = Date.now();
  const retouchTempPaths: string[] = [];
  const trackRetouchTempPath = (filePath: string): string => {
    retouchTempPaths.push(filePath);
    return filePath;
  };
  try {
    const baseJson = JSON.parse(options.stdout);
    const baseLayout = decodeLayoutSnapshot(baseJson, "ogdfLayout");
    const baseSummary = summarizeLayout(baseLayout);
    const baseMetadata = baseJson?.engineMetadata ?? {};
    const baseVisual = Number(baseMetadata.visualCrossings ?? 0);
    const baseEdgeCross = Number(baseMetadata.edgeCrossings ?? baseVisual);
    const baseEdgeNode = Number(baseMetadata.edgeNodeIntersections ?? 0);
    const baseBundleEdge = Number(baseMetadata.bundleEdgeIntersections ?? 0);
    const baseNodeOverlaps = Number(baseMetadata.nodeOverlaps ?? 0);
    const baseBundleNode = Number(baseMetadata.bundleNodeOverlaps ?? 0);
    const baseNodeSpacing = Number(baseMetadata.nodeSpacingOverlaps ?? 0);
    const baseOverlappingEdges = Number(baseMetadata.overlappingEdges ?? 0);
    const baseEdgeSegmentOverlaps =
      Number(baseMetadata.edgeSegmentOverlaps ?? 0);
    const baseRawRouteCross =
      Number(baseMetadata.rawRouteCrossings ?? baseEdgeCross);
    const retouchClusterGraph =
      options.clusterGraphLayout
      || baseMetadata.strategy === "cluster_graph"
      || String(baseMetadata.actualAlgorithm ?? "").includes("ClusterGraphLayout");
    if (baseVisual <= 0 && baseEdgeCross <= 0 && baseRawRouteCross <= 0) {
      return options.stdout;
    }

    const retouchPositionsPath = trackRetouchTempPath(path.join(
      os.tmpdir(),
      `django-erd-final-export-retouch-${Date.now()}.tsv`,
    ));
    const retouchRoutesPath = trackRetouchTempPath(path.join(
      os.tmpdir(),
      `django-erd-final-export-retouch-${Date.now()}-routes.tsv`,
    ));
    await writePositionsTsvFromLayoutJson(options.stdout, retouchPositionsPath);
    await writeRoutesTsvFromLayoutJson(options.stdout, retouchRoutesPath);

    const retouchTimeoutMs = readPositiveIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_TIMEOUT_MS",
      180_000,
    );
    const retouchBudgetedTimeout = budgetCandidateTimeout(
      options.polishDeadline,
      retouchTimeoutMs,
      "final-export retouch",
      options.logger,
    );
    if (!retouchBudgetedTimeout) {
      return options.stdout;
    }
    const retouchReroute = await execFileAsync(
      options.binaryPath,
      [
        "layout",
        "--mode", options.mode,
        "--nodes-file", options.nodesPath,
        "--edges-file", options.edgesPath,
        "--edge-routing", options.edgeRouting,
        ...(retouchClusterGraph ? ["--cluster-graph", "1"] : []),
        "--positions-tsv", retouchPositionsPath,
        "--routes-tsv", retouchRoutesPath,
      ],
      {
        cwd: options.cwd,
        env: ogdfFinalExportRetouchEnv(),
        killSignal: "SIGKILL",
        maxBuffer: 100 * 1024 * 1024,
        timeout: retouchBudgetedTimeout.timeoutMs,
      },
    );
    if (retouchReroute.stderr.trim().length > 0) {
      options.logger?.info(
        `[final-export retouch] OGDF stderr: `
        + retouchReroute.stderr.trim(),
      );
    }

    const baseNodeAreaB =
      baseSummary.nodeBBoxWidth * baseSummary.nodeBBoxHeight / 1e9;
    const baseRouteAreaB =
      baseSummary.routeBBoxWidth * baseSummary.routeBBoxHeight / 1e9;
    const retouchMinGain = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MIN_GAIN",
      1,
    );
    const retouchNodeBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_NODE_BBOX_GROWTH_LIMIT",
      1.01,
    );
    const retouchRouteBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_ROUTE_BBOX_GROWTH_LIMIT",
      1.01,
    );
    const retouchMaxSpacingDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SPACING_DEBT",
      0,
    );
    const retouchMaxSpacingDebtPerGain = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SPACING_DEBT_PER_GAIN",
      0,
    );
    const retouchMaxOverlappingEdgeDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_OVERLAPPING_EDGE_DEBT",
      0,
    );
    const retouchMaxOverlappingEdgeDebtPerGain = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_OVERLAPPING_EDGE_DEBT_PER_GAIN",
      0.5,
    );
    const retouchMaxSegmentOverlapDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SEGMENT_OVERLAP_DEBT",
      0,
    );
    const retouchMaxSegmentOverlapDebtPerGain = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_MAX_SEGMENT_OVERLAP_DEBT_PER_GAIN",
      0.5,
    );
    const retouchSalvage = readBoolEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE",
      true,
    );
    const retouchSalvageMinGain = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_MIN_GAIN",
      40,
    );
    const retouchSalvageAcceptMaxBundleNodeDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_ACCEPT_MAX_BUNDLE_NODE_DEBT",
      2,
    );
    const retouchSalvageAcceptMaxOverlappingEdgeDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_ACCEPT_MAX_OVERLAPPING_EDGE_DEBT",
      0,
    );
    const retouchSalvageAcceptMaxSegmentOverlapDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_ACCEPT_MAX_SEGMENT_OVERLAP_DEBT",
      0,
    );

    const evaluateRetouchCandidate = (
      label: string,
      candidateStdout: string,
      elapsedMs: number,
    ) => {
      const candidateJson = JSON.parse(candidateStdout);
      const candidateLayout = decodeLayoutSnapshot(candidateJson, "ogdfLayout");
      const candidateSummary = summarizeLayout(candidateLayout);
      const candidateMetadata = candidateJson?.engineMetadata ?? {};
      const candidateVisual =
        Number(candidateMetadata.visualCrossings ?? 0);
      const candidateEdgeCross =
        Number(candidateMetadata.edgeCrossings ?? candidateVisual);
      const candidateEdgeNode =
        Number(candidateMetadata.edgeNodeIntersections ?? 0);
      const candidateBundleEdge =
        Number(candidateMetadata.bundleEdgeIntersections ?? 0);
      const candidateNodeOverlaps = Number(candidateMetadata.nodeOverlaps ?? 0);
      const candidateBundleNode =
        Number(candidateMetadata.bundleNodeOverlaps ?? 0);
      const candidateNodeSpacing =
        Number(candidateMetadata.nodeSpacingOverlaps ?? 0);
      const candidateOverlappingEdges =
        Number(candidateMetadata.overlappingEdges ?? 0);
      const candidateEdgeSegmentOverlaps =
        Number(candidateMetadata.edgeSegmentOverlaps ?? 0);
      const candidateRawRouteCross =
        Number(candidateMetadata.rawRouteCrossings ?? candidateEdgeCross);
      const visualGain = Math.max(0, baseVisual - candidateVisual);
      const edgeCrossGain = Math.max(0, baseEdgeCross - candidateEdgeCross);
      const rawRouteGain =
        Math.max(0, baseRawRouteCross - candidateRawRouteCross);
      const visualDelta = candidateVisual - baseVisual;
      const edgeCrossDelta = candidateEdgeCross - baseEdgeCross;
      const rawRouteDelta = candidateRawRouteCross - baseRawRouteCross;

      const candidateNodeAreaB =
        candidateSummary.nodeBBoxWidth * candidateSummary.nodeBBoxHeight / 1e9;
      const candidateRouteAreaB =
        candidateSummary.routeBBoxWidth * candidateSummary.routeBBoxHeight / 1e9;
      const retouchNodeBboxGrowth =
        candidateNodeAreaB / Math.max(0.001, baseNodeAreaB);
      const retouchRouteBboxGrowth =
        candidateRouteAreaB / Math.max(0.001, baseRouteAreaB);
      const retouchSpacingDebt = Math.max(
        0,
        candidateNodeSpacing - baseNodeSpacing,
      );
      const retouchSpacingDebtPerGain =
        retouchSpacingDebt / Math.max(1, visualGain);
      const retouchOverlappingEdgeDebt = Math.max(
        0,
        candidateOverlappingEdges - baseOverlappingEdges,
      );
      const retouchOverlappingEdgeDebtPerGain =
        retouchOverlappingEdgeDebt / Math.max(1, visualGain);
      const retouchSegmentOverlapDebt = Math.max(
        0,
        candidateEdgeSegmentOverlaps - baseEdgeSegmentOverlaps,
      );
      const retouchSegmentOverlapDebtPerGain =
        retouchSegmentOverlapDebt / Math.max(1, visualGain);
      const retouchGainOk = visualGain >= retouchMinGain;
      const retouchNodeBboxOk =
        retouchNodeBboxGrowth <= retouchNodeBboxGrowthLimit;
      const retouchRouteBboxOk =
        retouchRouteBboxGrowth <= retouchRouteBboxGrowthLimit;
      const retouchNodeOverlapOk = candidateNodeOverlaps <= baseNodeOverlaps;
      const retouchBundleNodeDebt = Math.max(
        0,
        candidateBundleNode - baseBundleNode,
      );
      const retouchBundleEdgeDebt = Math.max(
        0,
        candidateBundleEdge - baseBundleEdge,
      );
      const retouchEdgeNodeDebt = Math.max(
        0,
        candidateEdgeNode - baseEdgeNode,
      );
      const retouchBundleNodeOk = candidateBundleNode <= baseBundleNode;
      const retouchSpacingOk =
        retouchSpacingDebt <= retouchMaxSpacingDebt
        && retouchSpacingDebtPerGain <= retouchMaxSpacingDebtPerGain;
      const retouchOverlappingEdgeOk =
        retouchOverlappingEdgeDebt <= retouchMaxOverlappingEdgeDebt
        && retouchOverlappingEdgeDebtPerGain
        <= retouchMaxOverlappingEdgeDebtPerGain;
      const retouchSegmentOverlapOk =
        retouchSegmentOverlapDebt <= retouchMaxSegmentOverlapDebt
        && retouchSegmentOverlapDebtPerGain
        <= retouchMaxSegmentOverlapDebtPerGain;
      const retouchStrictAccept =
        retouchGainOk
        && retouchNodeBboxOk
        && retouchRouteBboxOk
        && retouchNodeOverlapOk
        && retouchBundleNodeOk
        && retouchSpacingOk
        && retouchOverlappingEdgeOk
        && retouchSegmentOverlapOk;
      const retouchSalvageAccept =
        retouchSalvage
        && visualGain >= retouchSalvageMinGain
        && retouchGainOk
        && retouchNodeBboxOk
        && retouchRouteBboxOk
        && retouchNodeOverlapOk
        && retouchSpacingOk
        && retouchBundleNodeDebt <= retouchSalvageAcceptMaxBundleNodeDebt
        && retouchOverlappingEdgeDebt
        <= retouchSalvageAcceptMaxOverlappingEdgeDebt
        && retouchOverlappingEdgeDebtPerGain
        <= retouchMaxOverlappingEdgeDebtPerGain
        && retouchSegmentOverlapDebt
        <= retouchSalvageAcceptMaxSegmentOverlapDebt
        && retouchSegmentOverlapDebtPerGain
        <= retouchMaxSegmentOverlapDebtPerGain;
      const retouchAccept = retouchStrictAccept || retouchSalvageAccept;
      const retouchAcceptMode = retouchStrictAccept
        ? "strict"
        : retouchSalvageAccept
          ? "salvage"
          : "no";
      const rejectionReasons = [
        !retouchGainOk
          ? `visualGain ${visualGain}<${retouchMinGain}`
          : undefined,
        !retouchNodeBboxOk
          ? `nodeBboxGrowth ${retouchNodeBboxGrowth.toFixed(3)}`
            + `>${retouchNodeBboxGrowthLimit.toFixed(3)}`
          : undefined,
        !retouchRouteBboxOk
          ? `routeBboxGrowth ${retouchRouteBboxGrowth.toFixed(3)}`
            + `>${retouchRouteBboxGrowthLimit.toFixed(3)}`
          : undefined,
        !retouchNodeOverlapOk
          ? `nodeOverlaps ${baseNodeOverlaps}->${candidateNodeOverlaps}`
          : undefined,
        !retouchBundleNodeOk
          ? `bundleNode ${baseBundleNode}->${candidateBundleNode}`
          : undefined,
        !retouchSpacingOk
          ? `spacingDebt ${retouchSpacingDebt}`
            + `/${retouchMaxSpacingDebt}`
          : undefined,
        !retouchOverlappingEdgeOk
          ? `overlappingEdgeDebt ${retouchOverlappingEdgeDebt}`
            + `/${retouchMaxOverlappingEdgeDebt}`
          : undefined,
        !retouchSegmentOverlapOk
          ? `segmentOverlapDebt ${retouchSegmentOverlapDebt}`
            + `/${retouchMaxSegmentOverlapDebt}`
          : undefined,
      ].filter((reason): reason is string => reason !== undefined);
      options.logger?.info(
        `[${label}] deterministic done in `
        + `${elapsedMs}ms · `
        + `accepted=${retouchAccept} · `
        + `acceptMode=${retouchAcceptMode} · `
        + `visual=${baseVisual}->${candidateVisual} · `
        + `visualGain=${visualGain} · `
        + `visualDelta=${visualDelta} · `
        + `edgeCross=${baseEdgeCross}->${candidateEdgeCross} · `
        + `crossGain=${edgeCrossGain} · `
        + `crossDelta=${edgeCrossDelta} · `
        + `rawRouteCross=${baseRawRouteCross}->${candidateRawRouteCross} · `
        + `rawRouteGain=${rawRouteGain} · `
        + `rawRouteDelta=${rawRouteDelta} · `
        + `edgeNode=${baseEdgeNode}->${candidateEdgeNode} · `
        + `bundleEdge=${baseBundleEdge}->${candidateBundleEdge} · `
        + `nodeOverlaps=${baseNodeOverlaps}->${candidateNodeOverlaps} · `
        + `bundleNode=${baseBundleNode}->${candidateBundleNode} · `
        + `nodeSpacing=${baseNodeSpacing}->${candidateNodeSpacing} · `
        + `overlappingEdges=${baseOverlappingEdges}->${candidateOverlappingEdges} · `
        + `edgeSegmentOverlaps=${baseEdgeSegmentOverlaps}`
        + `->${candidateEdgeSegmentOverlaps} · `
        + `nodeBbox=${baseNodeAreaB.toFixed(2)}B`
        + `->${candidateNodeAreaB.toFixed(2)}B · `
        + `nodeBboxGrowth=${retouchNodeBboxGrowth.toFixed(3)}`
        + `/${retouchNodeBboxGrowthLimit.toFixed(3)} · `
        + `routeBbox=${baseRouteAreaB.toFixed(2)}B`
        + `->${candidateRouteAreaB.toFixed(2)}B · `
        + `routeBboxGrowth=${retouchRouteBboxGrowth.toFixed(3)}`
        + `/${retouchRouteBboxGrowthLimit.toFixed(3)} · `
        + `spacingDebt=${retouchSpacingDebt.toFixed(1)}`
        + `/${retouchMaxSpacingDebt.toFixed(1)} · `
        + `spacingDebtPerGain=${retouchSpacingDebtPerGain.toFixed(3)}`
        + `/${retouchMaxSpacingDebtPerGain.toFixed(3)} · `
        + `overlappingEdgeDebt=${retouchOverlappingEdgeDebt.toFixed(1)}`
        + `/${retouchMaxOverlappingEdgeDebt.toFixed(1)} · `
        + `overlappingEdgeDebtPerGain=${retouchOverlappingEdgeDebtPerGain.toFixed(3)}`
        + `/${retouchMaxOverlappingEdgeDebtPerGain.toFixed(3)} · `
        + `segmentOverlapDebt=${retouchSegmentOverlapDebt.toFixed(1)}`
        + `/${retouchMaxSegmentOverlapDebt.toFixed(1)} · `
        + `segmentOverlapDebtPerGain=${retouchSegmentOverlapDebtPerGain.toFixed(3)}`
        + `/${retouchMaxSegmentOverlapDebtPerGain.toFixed(3)}`
        + ` · salvageGain=${visualGain}/${retouchSalvageMinGain}`
        + ` · salvageBundleDebt=${retouchBundleNodeDebt}`
        + `/${retouchSalvageAcceptMaxBundleNodeDebt}`
        + ` · salvageOverlapDebt=${retouchOverlappingEdgeDebt.toFixed(1)}`
        + `/${retouchSalvageAcceptMaxOverlappingEdgeDebt.toFixed(1)}`
        + ` · salvageSegmentDebt=${retouchSegmentOverlapDebt.toFixed(1)}`
        + `/${retouchSalvageAcceptMaxSegmentOverlapDebt.toFixed(1)}`
        + (retouchAccept ? "" : ` · rejected=${rejectionReasons.join(",")}`),
      );

      return {
        accepted: retouchAccept,
        bundleEdgeDebt: retouchBundleEdgeDebt,
        bundleNodeDebt: retouchBundleNodeDebt,
        bundleNodeOk: retouchBundleNodeOk,
        edgeCrossGain,
        edgeNodeDebt: retouchEdgeNodeDebt,
        gainOk: retouchGainOk,
        nodeBboxGrowth: retouchNodeBboxGrowth,
        nodeBboxOk: retouchNodeBboxOk,
        nodeOverlapOk: retouchNodeOverlapOk,
        overlappingEdgeDebt: retouchOverlappingEdgeDebt,
        overlappingEdgeOk: retouchOverlappingEdgeOk,
        routeBboxGrowth: retouchRouteBboxGrowth,
        routeBboxOk: retouchRouteBboxOk,
        salvageAccepted: retouchSalvageAccept,
        segmentOverlapDebt: retouchSegmentOverlapDebt,
        segmentOverlapOk: retouchSegmentOverlapOk,
        spacingDebt: retouchSpacingDebt,
        spacingOk: retouchSpacingOk,
        rawRouteGain,
        visualDelta,
        visualGain,
      };
    };

    const retouchEvaluation = evaluateRetouchCandidate(
      "final-export retouch",
      retouchReroute.stdout,
      Date.now() - retouchStart,
    );
    if (retouchEvaluation.accepted) {
      return retouchReroute.stdout;
    }

    type RetouchEvaluation = ReturnType<typeof evaluateRetouchCandidate>;
    const isSpacingOnlyRetouchFailure = (evaluation: RetouchEvaluation) =>
      evaluation.gainOk
      && evaluation.nodeBboxOk
      && evaluation.routeBboxOk
      && evaluation.nodeOverlapOk
      && evaluation.bundleNodeOk
      && evaluation.overlappingEdgeOk
      && evaluation.segmentOverlapOk
      && !evaluation.spacingOk
      && evaluation.spacingDebt > 0;

    const retouchDebtRepairMaxBundleNodeDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_MAX_BUNDLE_NODE_DEBT",
      8,
    );
    const retouchDebtRepairMaxOverlappingEdgeDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_MAX_OVERLAPPING_EDGE_DEBT",
      18,
    );
    const retouchDebtRepairMaxSegmentOverlapDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_MAX_SEGMENT_OVERLAP_DEBT",
      12,
    );
    const retouchDebtRepairSalvageAccept = readBoolEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT",
      true,
    );
    const retouchDebtRepairSalvageAcceptMinVisualGain =
      readNonNegativeIntEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT_MIN_VISUAL_GAIN",
        80,
      );
    const retouchDebtRepairSalvageAcceptNodeBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT_NODE_BBOX_GROWTH_LIMIT",
      1.35,
    );
    const retouchDebtRepairSalvageAcceptRouteBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SALVAGE_ACCEPT_ROUTE_BBOX_GROWTH_LIMIT",
      1.20,
    );
    const retouchDebtRepairHubCorridor = readBoolEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR",
      true,
    );
    const retouchDebtRepairHubCorridorMinAdditionalVisualGain =
      readNonNegativeIntEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_MIN_ADDITIONAL_VISUAL_GAIN",
        32,
      );
    const retouchDebtRepairHubCorridorNodeBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_NODE_BBOX_GROWTH_LIMIT",
      1.45,
    );
    const retouchDebtRepairHubCorridorRouteBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_ROUTE_BBOX_GROWTH_LIMIT",
      1.35,
    );
    const retouchSalvageRepairMaxBundleNodeDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_MAX_BUNDLE_NODE_DEBT",
      16,
    );
    const retouchSalvageRepairMaxOverlappingEdgeDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_MAX_OVERLAPPING_EDGE_DEBT",
      12,
    );
    const retouchSalvageRepairMaxSegmentOverlapDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_MAX_SEGMENT_OVERLAP_DEBT",
      8,
    );
    const retouchSalvageRepairNodeBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_NODE_BBOX_GROWTH_LIMIT",
      1.35,
    );
    const retouchSalvageRepairRouteBboxGrowthLimit = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SALVAGE_REPAIR_ROUTE_BBOX_GROWTH_LIMIT",
      1.08,
    );
    const retouchCrossRepair = readBoolEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR",
      true,
    );
    const retouchCrossRepairMinEdgeCrossGain = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MIN_EDGE_CROSS_GAIN",
      80,
    );
    const retouchCrossRepairMinRawRouteGain = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MIN_RAW_ROUTE_GAIN",
      200,
    );
    const retouchCrossRepairMaxVisualDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_VISUAL_DEBT",
      96,
    );
    const retouchCrossRepairMaxEdgeNodeDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_EDGE_NODE_DEBT",
      220,
    );
    const retouchCrossRepairMaxBundleEdgeDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_BUNDLE_EDGE_DEBT",
      16,
    );
    const retouchCrossRepairMaxBundleNodeDebt = readNonNegativeIntEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_BUNDLE_NODE_DEBT",
      8,
    );
    const retouchCrossRepairMaxOverlappingEdgeDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_OVERLAPPING_EDGE_DEBT",
      8,
    );
    const retouchCrossRepairMaxSegmentOverlapDebt = readFloatEnv(
      "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CROSS_REPAIR_MAX_SEGMENT_OVERLAP_DEBT",
      4,
    );
    const isCrossReductionRepairableRetouchFailure = (
      evaluation: RetouchEvaluation,
    ) => {
      const visualDebt = Math.max(0, evaluation.visualDelta);
      const crossGainOk =
        evaluation.edgeCrossGain >= retouchCrossRepairMinEdgeCrossGain
        || evaluation.rawRouteGain >= retouchCrossRepairMinRawRouteGain;
      return (
        retouchCrossRepair
        && crossGainOk
        && visualDebt <= retouchCrossRepairMaxVisualDebt
        && evaluation.edgeNodeDebt <= retouchCrossRepairMaxEdgeNodeDebt
        && evaluation.bundleEdgeDebt <= retouchCrossRepairMaxBundleEdgeDebt
        && evaluation.bundleNodeDebt <= retouchCrossRepairMaxBundleNodeDebt
        && evaluation.overlappingEdgeDebt
        <= retouchCrossRepairMaxOverlappingEdgeDebt
        && evaluation.segmentOverlapDebt
        <= retouchCrossRepairMaxSegmentOverlapDebt
        && evaluation.nodeBboxOk
        && evaluation.routeBboxOk
        && evaluation.nodeOverlapOk
        && evaluation.spacingOk
      );
    };
    const isDebtRepairableRetouchFailure = (
      evaluation: RetouchEvaluation,
    ) => {
      const hasRepairableTopologyDebt =
        !evaluation.bundleNodeOk
        || !evaluation.overlappingEdgeOk
        || !evaluation.segmentOverlapOk;
      const hasRepairableBboxDebt =
        !evaluation.nodeBboxOk || !evaluation.routeBboxOk;
      const crossRepairable =
        isCrossReductionRepairableRetouchFailure(evaluation);
      if (
        (!evaluation.gainOk && !crossRepairable)
        || !evaluation.nodeOverlapOk
        || !evaluation.spacingOk
        || (!hasRepairableTopologyDebt && !hasRepairableBboxDebt
          && !crossRepairable)
      ) {
        return false;
      }
      const standardRepairable =
        evaluation.nodeBboxOk
        && evaluation.routeBboxOk
        && hasRepairableTopologyDebt
        && evaluation.bundleNodeDebt <= retouchDebtRepairMaxBundleNodeDebt
        && evaluation.overlappingEdgeDebt
        <= retouchDebtRepairMaxOverlappingEdgeDebt
        && evaluation.segmentOverlapDebt
        <= retouchDebtRepairMaxSegmentOverlapDebt;
      const salvageNodeBboxRepairable =
        evaluation.nodeBboxOk
        || evaluation.nodeBboxGrowth
        <= retouchSalvageRepairNodeBboxGrowthLimit;
      const salvageRouteBboxRepairable =
        evaluation.routeBboxOk
        || evaluation.routeBboxGrowth
        <= retouchSalvageRepairRouteBboxGrowthLimit;
      const salvageRepairable =
        retouchSalvage
        && evaluation.visualGain >= retouchSalvageMinGain
        && salvageNodeBboxRepairable
        && salvageRouteBboxRepairable
        && evaluation.bundleNodeDebt
        <= retouchSalvageRepairMaxBundleNodeDebt
        && evaluation.overlappingEdgeDebt
        <= retouchSalvageRepairMaxOverlappingEdgeDebt
        && evaluation.segmentOverlapDebt
        <= retouchSalvageRepairMaxSegmentOverlapDebt;
      return standardRepairable || salvageRepairable || crossRepairable;
    };
    const isDebtRepairSalvageAcceptable = (
      evaluation: RetouchEvaluation,
    ) =>
      retouchDebtRepairSalvageAccept
      && !evaluation.accepted
      && evaluation.visualGain
        >= retouchDebtRepairSalvageAcceptMinVisualGain
      && evaluation.nodeBboxGrowth
        <= retouchDebtRepairSalvageAcceptNodeBboxGrowthLimit
      && evaluation.routeBboxGrowth
        <= retouchDebtRepairSalvageAcceptRouteBboxGrowthLimit
      && evaluation.nodeOverlapOk
      && evaluation.bundleNodeDebt <= 0
      && evaluation.overlappingEdgeDebt <= 0
      && evaluation.segmentOverlapDebt <= 0
      && evaluation.spacingDebt <= 0
      && evaluation.edgeNodeDebt <= 0
      && evaluation.bundleEdgeDebt <= 0;
    const isDebtRepairHubCorridorAcceptable = (
      evaluation: RetouchEvaluation,
      sourceEvaluation: RetouchEvaluation,
    ) =>
      retouchDebtRepairHubCorridor
      && evaluation.visualGain
        >= sourceEvaluation.visualGain
          + retouchDebtRepairHubCorridorMinAdditionalVisualGain
      && evaluation.nodeBboxGrowth
        <= retouchDebtRepairHubCorridorNodeBboxGrowthLimit
      && evaluation.routeBboxGrowth
        <= retouchDebtRepairHubCorridorRouteBboxGrowthLimit
      && evaluation.nodeOverlapOk
      && evaluation.bundleNodeDebt <= 0
      && evaluation.overlappingEdgeDebt <= 0
      && evaluation.segmentOverlapDebt <= 0
      && evaluation.spacingDebt <= 0
      && evaluation.edgeNodeDebt <= 0
      && evaluation.bundleEdgeDebt <= 0;
    const tryDebtRepairHubCorridor = async (
      sourceStdout: string,
      sourceEvaluation: RetouchEvaluation,
    ): Promise<string | undefined> => {
      if (!retouchDebtRepairHubCorridor) {
        return undefined;
      }
      const corridorStart = Date.now();
      const corridorId = `${Date.now()}-debt-repair-hub-corridor`;
      const corridorPositionsPath = trackRetouchTempPath(path.join(
        os.tmpdir(),
        `django-erd-final-export-retouch-debt-hub-corridor-${corridorId}.tsv`,
      ));
      const corridorRoutesPath = trackRetouchTempPath(path.join(
        os.tmpdir(),
        `django-erd-final-export-retouch-debt-hub-corridor-${corridorId}-routes.tsv`,
      ));
      options.logger?.info(
        `[final-export retouch:debt-repair-hub-corridor] `
        + `retrying accepted debt-repair source · `
        + `sourceVisualGain=${sourceEvaluation.visualGain} · `
        + `targetAdditionalVisualGain=`
        + `${retouchDebtRepairHubCorridorMinAdditionalVisualGain} · `
        + `nodeBboxLimit=`
        + `${retouchDebtRepairHubCorridorNodeBboxGrowthLimit.toFixed(3)} · `
        + `routeBboxLimit=`
        + `${retouchDebtRepairHubCorridorRouteBboxGrowthLimit.toFixed(3)}`,
      );
      await writePositionsTsvFromLayoutJson(
        sourceStdout,
        corridorPositionsPath,
      );
      await writeRoutesTsvFromLayoutJson(
        sourceStdout,
        corridorRoutesPath,
      );
      const corridorTimeoutMs = readPositiveIntEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_HUB_CORRIDOR_TIMEOUT_MS",
        120_000,
      );
      const corridorBudgetedTimeout = budgetCandidateTimeout(
        options.polishDeadline,
        corridorTimeoutMs,
        "final-export retouch:debt-repair-hub-corridor",
        options.logger,
      );
      if (!corridorBudgetedTimeout) {
        return undefined;
      }
      const hubCorridor = await execFileAsync(
        options.binaryPath,
        [
          "layout",
          "--mode", options.mode,
          "--nodes-file", options.nodesPath,
          "--edges-file", options.edgesPath,
          "--edge-routing", options.edgeRouting,
          ...(retouchClusterGraph ? ["--cluster-graph", "1"] : []),
          "--positions-tsv", corridorPositionsPath,
          "--routes-tsv", corridorRoutesPath,
        ],
        {
          cwd: options.cwd,
          env: ogdfFinalExportRetouchDebtRepairHubCorridorEnv(),
          killSignal: "SIGKILL",
          maxBuffer: 100 * 1024 * 1024,
          timeout: corridorBudgetedTimeout.timeoutMs,
        },
      );
      if (hubCorridor.stderr.trim().length > 0) {
        options.logger?.info(
          `[final-export retouch:debt-repair-hub-corridor] OGDF stderr: `
          + hubCorridor.stderr.trim(),
        );
      }
      const corridorEvaluation = evaluateRetouchCandidate(
        "final-export retouch:debt-repair-hub-corridor",
        hubCorridor.stdout,
        Date.now() - corridorStart,
      );
      const additionalVisualGain =
        corridorEvaluation.visualGain - sourceEvaluation.visualGain;
      if (isDebtRepairHubCorridorAcceptable(
        corridorEvaluation,
        sourceEvaluation,
      )) {
        options.logger?.info(
          `[final-export retouch:debt-repair-hub-corridor] accepted · `
          + `visualGain=${corridorEvaluation.visualGain} · `
          + `additionalVisualGain=${additionalVisualGain}`
          + `/${retouchDebtRepairHubCorridorMinAdditionalVisualGain} · `
          + `nodeBboxGrowth=${corridorEvaluation.nodeBboxGrowth.toFixed(3)}`
          + `/${retouchDebtRepairHubCorridorNodeBboxGrowthLimit.toFixed(3)} · `
          + `routeBboxGrowth=${corridorEvaluation.routeBboxGrowth.toFixed(3)}`
          + `/${retouchDebtRepairHubCorridorRouteBboxGrowthLimit.toFixed(3)}`,
        );
        return hubCorridor.stdout;
      }
      options.logger?.info(
        `[final-export retouch:debt-repair-hub-corridor] rejected · `
        + `visualGain=${corridorEvaluation.visualGain} · `
        + `additionalVisualGain=${additionalVisualGain}`
        + `/${retouchDebtRepairHubCorridorMinAdditionalVisualGain} · `
        + `nodeBboxGrowth=${corridorEvaluation.nodeBboxGrowth.toFixed(3)}`
        + `/${retouchDebtRepairHubCorridorNodeBboxGrowthLimit.toFixed(3)} · `
        + `routeBboxGrowth=${corridorEvaluation.routeBboxGrowth.toFixed(3)}`
        + `/${retouchDebtRepairHubCorridorRouteBboxGrowthLimit.toFixed(3)} · `
        + `edgeNodeDebt=${corridorEvaluation.edgeNodeDebt} · `
        + `bundleEdgeDebt=${corridorEvaluation.bundleEdgeDebt} · `
        + `bundleNodeDebt=${corridorEvaluation.bundleNodeDebt} · `
        + `spacingDebt=${corridorEvaluation.spacingDebt.toFixed(1)} · `
        + `overlappingEdgeDebt=`
        + `${corridorEvaluation.overlappingEdgeDebt.toFixed(1)} · `
        + `segmentOverlapDebt=`
        + `${corridorEvaluation.segmentOverlapDebt.toFixed(1)}`,
      );
      return undefined;
    };

    let spacingRepairSourceStdout = retouchReroute.stdout;
    let spacingRepairSourceEvaluation = retouchEvaluation;
    const shouldTryClearanceRetry =
      readBoolEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY",
        true,
      )
      && isSpacingOnlyRetouchFailure(retouchEvaluation);
    if (shouldTryClearanceRetry) {
      const retryStart = Date.now();
      const retryTimeoutMs = readPositiveIntEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_CLEARANCE_RETRY_TIMEOUT_MS",
        120_000,
      );
      const retryBudgetedTimeout = budgetCandidateTimeout(
        options.polishDeadline,
        retryTimeoutMs,
        "final-export retouch:clearance-retry",
        options.logger,
      );
      if (!retryBudgetedTimeout) {
        return options.stdout;
      }
      const clearanceRetry = await execFileAsync(
        options.binaryPath,
        [
          "layout",
          "--mode", options.mode,
          "--nodes-file", options.nodesPath,
          "--edges-file", options.edgesPath,
          "--edge-routing", options.edgeRouting,
          ...(retouchClusterGraph ? ["--cluster-graph", "1"] : []),
          "--positions-tsv", retouchPositionsPath,
          "--routes-tsv", retouchRoutesPath,
        ],
        {
          cwd: options.cwd,
          env: ogdfFinalExportRetouchClearanceRetryEnv(),
          killSignal: "SIGKILL",
          maxBuffer: 100 * 1024 * 1024,
          timeout: retryBudgetedTimeout.timeoutMs,
        },
      );
      if (clearanceRetry.stderr.trim().length > 0) {
        options.logger?.info(
          `[final-export retouch:clearance-retry] OGDF stderr: `
          + clearanceRetry.stderr.trim(),
        );
      }
      const retryEvaluation = evaluateRetouchCandidate(
        "final-export retouch:clearance-retry",
        clearanceRetry.stdout,
        Date.now() - retryStart,
      );
      if (retryEvaluation.accepted) {
        return clearanceRetry.stdout;
      }
      if (isSpacingOnlyRetouchFailure(retryEvaluation)) {
        spacingRepairSourceStdout = clearanceRetry.stdout;
        spacingRepairSourceEvaluation = retryEvaluation;
      }
    }

    const shouldTrySpacingRepair =
      readBoolEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR",
        true,
      )
      && isSpacingOnlyRetouchFailure(spacingRepairSourceEvaluation);
    if (shouldTrySpacingRepair) {
      const repairStart = Date.now();
      const repairId = `${Date.now()}-repair`;
      const repairPositionsPath = trackRetouchTempPath(path.join(
        os.tmpdir(),
        `django-erd-final-export-retouch-spacing-repair-${repairId}.tsv`,
      ));
      const repairRoutesPath = trackRetouchTempPath(path.join(
        os.tmpdir(),
        `django-erd-final-export-retouch-spacing-repair-${repairId}-routes.tsv`,
      ));
      await writePositionsTsvFromLayoutJson(
        spacingRepairSourceStdout,
        repairPositionsPath,
      );
      await writeRoutesTsvFromLayoutJson(
        spacingRepairSourceStdout,
        repairRoutesPath,
      );
      const repairTimeoutMs = readPositiveIntEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_TIMEOUT_MS",
        120_000,
      );
      const repairBudgetedTimeout = budgetCandidateTimeout(
        options.polishDeadline,
        repairTimeoutMs,
        "final-export retouch:spacing-repair",
        options.logger,
      );
      if (!repairBudgetedTimeout) {
        return options.stdout;
      }
      const spacingRepair = await execFileAsync(
        options.binaryPath,
        [
          "layout",
          "--mode", options.mode,
          "--nodes-file", options.nodesPath,
          "--edges-file", options.edgesPath,
          "--edge-routing", options.edgeRouting,
          ...(retouchClusterGraph ? ["--cluster-graph", "1"] : []),
          "--positions-tsv", repairPositionsPath,
          "--routes-tsv", repairRoutesPath,
        ],
        {
          cwd: options.cwd,
          env: ogdfFinalExportRetouchSpacingRepairEnv(),
          killSignal: "SIGKILL",
          maxBuffer: 100 * 1024 * 1024,
          timeout: repairBudgetedTimeout.timeoutMs,
        },
      );
      if (spacingRepair.stderr.trim().length > 0) {
        options.logger?.info(
          `[final-export retouch:spacing-repair] OGDF stderr: `
          + spacingRepair.stderr.trim(),
        );
      }
      const repairEvaluation = evaluateRetouchCandidate(
        "final-export retouch:spacing-repair",
        spacingRepair.stdout,
        Date.now() - repairStart,
      );
      if (repairEvaluation.accepted) {
        return spacingRepair.stdout;
      }
    }

    const shouldTryDebtRepair =
      readBoolEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR",
        true,
      )
      && isDebtRepairableRetouchFailure(retouchEvaluation);
    if (shouldTryDebtRepair) {
      const repairStart = Date.now();
      const repairId = `${Date.now()}-debt-repair`;
      const crossRepairable =
        isCrossReductionRepairableRetouchFailure(retouchEvaluation);
      options.logger?.info(
        `[final-export retouch:debt-repair] retrying repairable source · `
        + `reason=${crossRepairable ? "cross-reduction" : "visual-debt"} · `
        + `visualGain=${retouchEvaluation.visualGain} · `
        + `visualDelta=${retouchEvaluation.visualDelta} · `
        + `edgeCrossGain=${retouchEvaluation.edgeCrossGain}`
        + `/${retouchCrossRepairMinEdgeCrossGain} · `
        + `rawRouteGain=${retouchEvaluation.rawRouteGain}`
        + `/${retouchCrossRepairMinRawRouteGain} · `
        + `edgeNodeDebt=${retouchEvaluation.edgeNodeDebt}`
        + `/${retouchCrossRepairMaxEdgeNodeDebt} · `
        + `bundleEdgeDebt=${retouchEvaluation.bundleEdgeDebt}`
        + `/${retouchCrossRepairMaxBundleEdgeDebt} · `
        + `nodeBboxGrowth=${retouchEvaluation.nodeBboxGrowth.toFixed(3)}`
        + `/${retouchSalvageRepairNodeBboxGrowthLimit.toFixed(3)} · `
        + `routeBboxGrowth=${retouchEvaluation.routeBboxGrowth.toFixed(3)}`
        + `/${retouchSalvageRepairRouteBboxGrowthLimit.toFixed(3)} · `
        + `bundleNodeDebt=${retouchEvaluation.bundleNodeDebt}`
        + `/${retouchSalvageRepairMaxBundleNodeDebt} · `
        + `overlappingEdgeDebt=${retouchEvaluation.overlappingEdgeDebt.toFixed(1)}`
        + `/${retouchSalvageRepairMaxOverlappingEdgeDebt.toFixed(1)} · `
        + `segmentOverlapDebt=${retouchEvaluation.segmentOverlapDebt.toFixed(1)}`
        + `/${retouchSalvageRepairMaxSegmentOverlapDebt.toFixed(1)}`,
      );
      const repairPositionsPath = trackRetouchTempPath(path.join(
        os.tmpdir(),
        `django-erd-final-export-retouch-debt-repair-${repairId}.tsv`,
      ));
      const repairRoutesPath = trackRetouchTempPath(path.join(
        os.tmpdir(),
        `django-erd-final-export-retouch-debt-repair-${repairId}-routes.tsv`,
      ));
      await writePositionsTsvFromLayoutJson(
        retouchReroute.stdout,
        repairPositionsPath,
      );
      await writeRoutesTsvFromLayoutJson(
        retouchReroute.stdout,
        repairRoutesPath,
      );
      const repairTimeoutMs = readPositiveIntEnv(
        "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_TIMEOUT_MS",
        120_000,
      );
      const repairBudgetedTimeout = budgetCandidateTimeout(
        options.polishDeadline,
        repairTimeoutMs,
        "final-export retouch:debt-repair",
        options.logger,
      );
      if (!repairBudgetedTimeout) {
        return options.stdout;
      }
      const debtRepair = await execFileAsync(
        options.binaryPath,
        [
          "layout",
          "--mode", options.mode,
          "--nodes-file", options.nodesPath,
          "--edges-file", options.edgesPath,
          "--edge-routing", options.edgeRouting,
          ...(retouchClusterGraph ? ["--cluster-graph", "1"] : []),
          "--positions-tsv", repairPositionsPath,
          "--routes-tsv", repairRoutesPath,
        ],
        {
          cwd: options.cwd,
          env: ogdfFinalExportRetouchDebtRepairEnv(),
          killSignal: "SIGKILL",
          maxBuffer: 100 * 1024 * 1024,
          timeout: repairBudgetedTimeout.timeoutMs,
        },
      );
      if (debtRepair.stderr.trim().length > 0) {
        options.logger?.info(
          `[final-export retouch:debt-repair] OGDF stderr: `
          + debtRepair.stderr.trim(),
        );
      }
      const repairEvaluation = evaluateRetouchCandidate(
        "final-export retouch:debt-repair",
        debtRepair.stdout,
        Date.now() - repairStart,
      );
      const repairSalvageAccepted =
        isDebtRepairSalvageAcceptable(repairEvaluation);
      if (repairEvaluation.accepted || repairSalvageAccepted) {
        if (repairSalvageAccepted) {
          options.logger?.info(
            `[final-export retouch:debt-repair] salvage accepted · `
            + `visualGain=${repairEvaluation.visualGain}`
            + `/${retouchDebtRepairSalvageAcceptMinVisualGain} · `
            + `nodeBboxGrowth=${repairEvaluation.nodeBboxGrowth.toFixed(3)}`
            + `/${retouchDebtRepairSalvageAcceptNodeBboxGrowthLimit.toFixed(3)} · `
            + `routeBboxGrowth=${repairEvaluation.routeBboxGrowth.toFixed(3)}`
            + `/${retouchDebtRepairSalvageAcceptRouteBboxGrowthLimit.toFixed(3)} · `
            + `edgeNodeDebt=${repairEvaluation.edgeNodeDebt} · `
            + `bundleEdgeDebt=${repairEvaluation.bundleEdgeDebt} · `
            + `bundleNodeDebt=${repairEvaluation.bundleNodeDebt} · `
            + `spacingDebt=${repairEvaluation.spacingDebt.toFixed(1)} · `
            + `overlappingEdgeDebt=`
            + `${repairEvaluation.overlappingEdgeDebt.toFixed(1)} · `
            + `segmentOverlapDebt=`
            + `${repairEvaluation.segmentOverlapDebt.toFixed(1)}`,
          );
        }
        try {
          const hubCorridorStdout = await tryDebtRepairHubCorridor(
            debtRepair.stdout,
            repairEvaluation,
          );
          return hubCorridorStdout ?? debtRepair.stdout;
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err);
          options.logger?.warn(
            `[final-export retouch:debt-repair-hub-corridor] failed; `
            + `keeping accepted debt-repair candidate: ${msg}`,
          );
          return debtRepair.stdout;
        }
      }
      const shouldTryDebtSpacingRepair =
        readBoolEnv(
          "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SPACING_REPAIR",
          true,
        )
        && isSpacingOnlyRetouchFailure(repairEvaluation);
      if (shouldTryDebtSpacingRepair) {
        const spacingRepairStart = Date.now();
        const spacingRepairId = `${Date.now()}-debt-spacing-repair`;
        options.logger?.info(
          `[final-export retouch:debt-repair-spacing-repair] `
          + `retrying spacing-only debt-repair result · `
          + `visualGain=${repairEvaluation.visualGain} · `
          + `spacingDebt=${repairEvaluation.spacingDebt.toFixed(1)}`,
        );
        const spacingRepairPositionsPath = trackRetouchTempPath(path.join(
          os.tmpdir(),
          `django-erd-final-export-retouch-debt-spacing-repair-${spacingRepairId}.tsv`,
        ));
        const spacingRepairRoutesPath = trackRetouchTempPath(path.join(
          os.tmpdir(),
          `django-erd-final-export-retouch-debt-spacing-repair-${spacingRepairId}-routes.tsv`,
        ));
        await writePositionsTsvFromLayoutJson(
          debtRepair.stdout,
          spacingRepairPositionsPath,
        );
        await writeRoutesTsvFromLayoutJson(
          debtRepair.stdout,
          spacingRepairRoutesPath,
        );
        const spacingRepairTimeoutMs = readPositiveIntEnv(
          "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_DEBT_REPAIR_SPACING_REPAIR_TIMEOUT_MS",
          readPositiveIntEnv(
            "DJERD_OPTIMIZED_VISUAL_CROSS_FINAL_RETOUCH_SPACING_REPAIR_TIMEOUT_MS",
            120_000,
          ),
        );
        const spacingRepairBudgetedTimeout = budgetCandidateTimeout(
          options.polishDeadline,
          spacingRepairTimeoutMs,
          "final-export retouch:debt-repair-spacing-repair",
          options.logger,
        );
        if (!spacingRepairBudgetedTimeout) {
          return options.stdout;
        }
        const debtSpacingRepair = await execFileAsync(
          options.binaryPath,
          [
            "layout",
            "--mode", options.mode,
            "--nodes-file", options.nodesPath,
            "--edges-file", options.edgesPath,
            "--edge-routing", options.edgeRouting,
            ...(retouchClusterGraph ? ["--cluster-graph", "1"] : []),
            "--positions-tsv", spacingRepairPositionsPath,
            "--routes-tsv", spacingRepairRoutesPath,
          ],
          {
            cwd: options.cwd,
            env: ogdfFinalExportRetouchSpacingRepairEnv(),
            killSignal: "SIGKILL",
            maxBuffer: 100 * 1024 * 1024,
            timeout: spacingRepairBudgetedTimeout.timeoutMs,
          },
        );
        if (debtSpacingRepair.stderr.trim().length > 0) {
          options.logger?.info(
            `[final-export retouch:debt-repair-spacing-repair] OGDF stderr: `
            + debtSpacingRepair.stderr.trim(),
          );
        }
        const spacingRepairEvaluation = evaluateRetouchCandidate(
          "final-export retouch:debt-repair-spacing-repair",
          debtSpacingRepair.stdout,
          Date.now() - spacingRepairStart,
        );
        if (spacingRepairEvaluation.accepted) {
          return debtSpacingRepair.stdout;
        }
      }
    }

    return options.stdout;
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    const budgetExhausted =
      options.polishDeadline
      && remainingPostReroutePolishBudgetMs(options.polishDeadline) <= 0;
    if (budgetExhausted) {
      options.logger?.info(
        `[post-reroute polish budget] final-export retouch stopped; `
        + "budget-limited candidate consumed the remaining deadline; "
        + "skipping sibling repairs",
      );
    } else {
      options.logger?.warn(
        `[final-export retouch] failed in ${Date.now() - retouchStart}ms: ${msg}`,
      );
    }
    return options.stdout;
  } finally {
    await Promise.all(
      retouchTempPaths.map((filePath) => rm(filePath, { force: true })),
    );
  }
}

function execFileAsync(
  filePath: string,
  args: string[],
  options: {
    cwd: string;
    env?: Record<string, string | undefined>;
    killSignal?: "SIGTERM" | "SIGKILL";
    maxBuffer: number;
    timeout: number;
  },
): Promise<{ stderr: string; stdout: string }> {
  return new Promise((resolve, reject) => {
    // execFile's `timeout` sends `killSignal` (default SIGTERM) after the
    // configured duration, but a misbehaving native binary can ignore
    // SIGTERM and keep running for minutes while Node waits on its
    // stdout/stderr drain. Track a hard wall-clock deadline ourselves and
    // escalate to SIGKILL after a short grace period to guarantee the
    // process actually exits.
    let forceKillTimer: { unref?: () => void } | undefined;
    const child: {
      killed?: boolean;
      pid?: number;
      kill: (signal?: string) => boolean;
    } = execFile(
      filePath,
      args,
      options,
      (error, stdout, stderr) => {
        if (forceKillTimer !== undefined) {
          clearTimeout(forceKillTimer as unknown as Parameters<typeof clearTimeout>[0]);
          forceKillTimer = undefined;
        }
        if (error) {
          const signal =
            "signal" in error && typeof error.signal === "string"
              ? error.signal
              : undefined;
          const killed = "killed" in error ? Boolean(error.killed) : false;
          reject(
            new OgdfExecError(
              buildExecFailureMessage(error, stderr),
              {
                code: readExecErrorCode(error),
                killed,
                signal,
                stderr,
                stdout,
                timedOut:
                  error.message.includes("timed out")
                  || (killed
                    && (signal === "SIGTERM" || signal === "SIGKILL")),
              },
            ),
          );
          return;
        }

        resolve({ stderr, stdout });
      },
    ) as unknown as {
      killed?: boolean;
      pid?: number;
      kill: (signal?: string) => boolean;
    };
    if (options.timeout > 0 && child.pid !== undefined) {
      const timer = setTimeout(() => {
        if (!child.killed) {
          try {
            child.kill("SIGKILL");
          } catch {
            // Process already gone; ignore.
          }
        }
      }, options.timeout + 5_000);
      forceKillTimer = timer as unknown as { unref?: () => void };
      if (typeof forceKillTimer.unref === "function") {
        forceKillTimer.unref();
      }
    }
  });
}

function buildV36ScorerArgs(
  scorerScript: string,
  baselinePath: string,
  baselinePositionsPath: string,
  positionsPath: string,
  ckptPath: string,
  familyPriorPath: string,
): string[] {
  const args = [
    scorerScript,
    "--layout", baselinePath,
    "--positions", baselinePositionsPath,
    "--out-tsv", positionsPath,
    "--ckpt", ckptPath,
    "--family-prior-ckpt", familyPriorPath,
    "--family-prior-top-actions", readStringEnv("DJERD_V37_FAMILY_PRIOR_TOP_ACTIONS", "8"),
    "--family-prior-min-candidates", readStringEnv("DJERD_V37_FAMILY_PRIOR_MIN_CANDIDATES", "120"),
    "--rounds", readStringEnv("DJERD_V35_ROUNDS", "1"),
    "--ml-top-k", readStringEnv("DJERD_V35_ML_TOP_K", "60"),
    "--per-action-k", readStringEnv("DJERD_V35_PER_ACTION_K", "12"),
    "--score-batch-size", readStringEnv("DJERD_V35_SCORE_BATCH_SIZE", "4096"),
    "--min-gain", readStringEnv("DJERD_V35_MIN_GAIN", "1"),
    "--max-cross-regression", readStringEnv("DJERD_V35_MAX_CROSS_REGRESSION", "0"),
    "--max-overlap-regression", readStringEnv("DJERD_V35_MAX_OVERLAP_REGRESSION", "0"),
    "--max-edge-node-regression", readStringEnv("DJERD_V35_MAX_EDGE_NODE_REGRESSION", "0"),
    "--max-bbox-growth", readStringEnv("DJERD_V35_MAX_BBOX_GROWTH", "1.05"),
    "--overlap-margin", readStringEnv("DJERD_V35_OVERLAP_MARGIN", "16"),
    "--overlap-weight", readStringEnv("DJERD_V35_OVERLAP_WEIGHT", "5000"),
    "--bbox-weight", readStringEnv("DJERD_V35_BBOX_WEIGHT", "800"),
    "--bbox-target-b", readStringEnv("DJERD_V35_BBOX_TARGET_B", "4.0"),
    "--cross-hotspot-bypass-pressure-top-cells", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_PRESSURE_TOP_CELLS", "8"),
    "--cross-hotspot-bypass-max-pressure-growth", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_MAX_PRESSURE_GROWTH", "-1"),
    "--edge-node-margin", readStringEnv("DJERD_V35_EDGE_NODE_MARGIN", "0"),
    "--edge-node-weight", readStringEnv("DJERD_V35_EDGE_NODE_WEIGHT", "0.5"),
    "--advance", "ml",
    "--top-nodes", readStringEnv("DJERD_V35_TOP_NODES", "40"),
    "--top-edges", readStringEnv("DJERD_V35_TOP_EDGES", "40"),
    "--top-clusters", readStringEnv("DJERD_V35_TOP_CLUSTERS", "50"),
    "--steps", readStringEnv("DJERD_V35_STEPS", "75,150,300,600,1000,1800"),
    "--anchor-radii", readStringEnv("DJERD_V35_ANCHOR_RADII", "75,150,300,600,1000,1800,3000"),
    "--cross-pair-candidates", readStringEnv("DJERD_V35_CROSS_PAIR_CANDIDATES", "80"),
    "--cross-pair-steps", readStringEnv("DJERD_V35_CROSS_PAIR_STEPS", "75,150,300,600,1000,1800"),
    "--cross-fan-edges", readStringEnv("DJERD_V35_CROSS_FAN_EDGES", "40"),
    "--cross-fan-steps", readStringEnv("DJERD_V35_CROSS_FAN_STEPS", "75,150,300,600,1000,1800"),
    "--cross-group-fan-groups", readStringEnv("DJERD_V35_CROSS_GROUP_FAN_GROUPS", "50"),
    "--cross-group-fan-max-size", readStringEnv("DJERD_V35_CROSS_GROUP_FAN_MAX_SIZE", "120"),
    "--cross-group-fan-steps", readStringEnv("DJERD_V35_CROSS_GROUP_FAN_STEPS", "75,150,300,600,1000,1800"),
    "--cross-carrier-pair-candidates", readStringEnv("DJERD_V35_CROSS_CARRIER_PAIR_CANDIDATES", "0"),
    "--cross-carrier-pair-max-size", readStringEnv("DJERD_V35_CROSS_CARRIER_PAIR_MAX_SIZE", "120"),
    "--cross-carrier-pair-steps", readStringEnv("DJERD_V35_CROSS_CARRIER_PAIR_STEPS", "75,150,300,600,1000,1800,3000,5000"),
    "--cross-hotspot-bypass-hotspots", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_HOTSPOTS", "0"),
    "--cross-hotspot-bypass-edges", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_EDGES", "8"),
    "--cross-hotspot-bypass-max-size", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_MAX_SIZE", "80"),
    "--cross-hotspot-bypass-cell-size", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_CELL_SIZE", "5000"),
    "--cross-hotspot-bypass-steps", readStringEnv("DJERD_V35_CROSS_HOTSPOT_BYPASS_STEPS", "75,150,300,600,1000,1800"),
    "--cross-partner-orbit-edges", readStringEnv("DJERD_V35_CROSS_PARTNER_ORBIT_EDGES", "0"),
    "--cross-partner-radii", readStringEnv("DJERD_V35_CROSS_PARTNER_RADII", "150,300,600,1000,1800,3000,5000,8000"),
    "--cross-endpoint-swap-pairs", readStringEnv("DJERD_V35_CROSS_ENDPOINT_SWAP_PAIRS", "0"),
    "--swap-pairs", readStringEnv("DJERD_V35_SWAP_PAIRS", "40"),
    "--cluster-swap-pairs", readStringEnv("DJERD_V35_CLUSTER_SWAP_PAIRS", "100"),
    "--edge-node-relief-hits", readStringEnv("DJERD_V35_EDGE_NODE_RELIEF_HITS", "40"),
    "--edge-node-relief-max-group-size", readStringEnv("DJERD_V35_EDGE_NODE_RELIEF_MAX_GROUP_SIZE", "120"),
    "--edge-node-relief-steps", readStringEnv("DJERD_V35_EDGE_NODE_RELIEF_STEPS", "75,150,300,600,1000,1800"),
    "--edge-node-precise-hits", readStringEnv("DJERD_V35_EDGE_NODE_PRECISE_HITS", "50"),
    "--edge-node-precise-neighborhood-max-nodes", readStringEnv("DJERD_V35_EDGE_NODE_PRECISE_NEIGHBORHOOD_MAX_NODES", "32"),
    "--edge-node-precise-pads", readStringEnv("DJERD_V35_EDGE_NODE_PRECISE_PADS", "0,60,140"),
    "--edge-node-clear-padding", readStringEnv("DJERD_V35_EDGE_NODE_CLEAR_PADDING", "32"),
    "--edge-node-corridor-edges", readStringEnv("DJERD_V35_EDGE_NODE_CORRIDOR_EDGES", "20"),
    "--edge-node-corridor-blockers", readStringEnv("DJERD_V35_EDGE_NODE_CORRIDOR_BLOCKERS", "8"),
    "--edge-node-corridor-pads", readStringEnv("DJERD_V35_EDGE_NODE_CORRIDOR_PADS", "0,60,140"),
    "--edge-node-force-hits", readStringEnv("DJERD_V35_EDGE_NODE_FORCE_HITS", "50"),
    "--edge-node-force-nodes", readStringEnv("DJERD_V35_EDGE_NODE_FORCE_NODES", "64"),
    "--edge-node-force-scales", readStringEnv("DJERD_V35_EDGE_NODE_FORCE_SCALES", "0.25,0.5,0.85"),
    "--edge-node-force-padding", readStringEnv("DJERD_V35_EDGE_NODE_FORCE_PADDING", "55"),
    "--overlap-candidates", readStringEnv("DJERD_V35_OVERLAP_CANDIDATES", "120"),
    "--overlap-spread-components", readStringEnv("DJERD_V35_OVERLAP_SPREAD_COMPONENTS", "60"),
    "--loose-pack-groups", readStringEnv("DJERD_V35_LOOSE_PACK_GROUPS", "12"),
    "--loose-anchor-nodes", readStringEnv("DJERD_V35_LOOSE_ANCHOR_NODES", "40"),
    "--loose-anchor-radii", readStringEnv("DJERD_V35_LOOSE_ANCHOR_RADII", "150,300,600,1000,1800,3000,5000,8000"),
    "--semantic-anchor-nodes", readStringEnv("DJERD_V35_SEMANTIC_ANCHOR_NODES", "12"),
    "--group-anchor-groups", readStringEnv("DJERD_V35_GROUP_ANCHOR_GROUPS", "70"),
    "--group-anchor-max-size", readStringEnv("DJERD_V35_GROUP_ANCHOR_MAX_SIZE", "120"),
    "--component-anchor-components", readStringEnv("DJERD_V35_COMPONENT_ANCHOR_COMPONENTS", "30"),
    "--component-anchor-max-group-size", readStringEnv("DJERD_V35_COMPONENT_ANCHOR_MAX_GROUP_SIZE", "10"),
    "--component-anchor-max-total-nodes", readStringEnv("DJERD_V35_COMPONENT_ANCHOR_MAX_TOTAL_NODES", "100"),
    "--bundle-orbit-groups", readStringEnv("DJERD_V35_BUNDLE_ORBIT_GROUPS", "12"),
    "--final-max-candidates", readStringEnv("DJERD_V35_FINAL_MAX_CANDIDATES", "400"),
    "--final-per-action-candidates", readStringEnv("DJERD_V35_FINAL_PER_ACTION_CANDIDATES", "80"),
  ];

  if (process.env.DJERD_V35_COMPARE_FULL !== "1") {
    args.push("--no-compare-full");
  }

  return args;
}

async function writePositionsTsvFromLayoutJson(
  layoutJson: string,
  outputPath: string,
): Promise<void> {
  const parsed = JSON.parse(layoutJson) as {
    nodes?: Array<{
      modelId?: unknown;
      position?: { x?: unknown; y?: unknown };
      size?: { height?: unknown; width?: unknown };
    }>;
  };
  const lines: string[] = [];
  for (const node of parsed.nodes ?? []) {
    if (typeof node.modelId !== "string" || !node.position) {
      continue;
    }
    const x = numericValue(node.position.x) + numericValue(node.size?.width) / 2;
    const y = numericValue(node.position.y) + numericValue(node.size?.height) / 2;
    lines.push(`${tsvCell(node.modelId)}\t${x.toFixed(3)}\t${y.toFixed(3)}`);
  }
  await writeFile(outputPath, lines.join("\n"), "utf8");
}

async function writeRoutesTsvFromLayoutJson(
  layoutJson: string,
  outputPath: string,
): Promise<void> {
  const parsed = JSON.parse(layoutJson) as {
    routedEdges?: Array<{
      edgeId?: unknown;
      points?: Array<{ x?: unknown; y?: unknown }>;
    }>;
  };
  const lines: string[] = [];
  for (const edge of parsed.routedEdges ?? []) {
    if (typeof edge.edgeId !== "string" || !Array.isArray(edge.points)) {
      continue;
    }
    const cells = [tsvCell(edge.edgeId)];
    for (const point of edge.points) {
      const x = numericValue(point.x);
      const y = numericValue(point.y);
      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        continue;
      }
      cells.push(x.toFixed(3), y.toFixed(3));
    }
    if (cells.length >= 5) {
      lines.push(cells.join("\t"));
    }
  }
  await writeFile(outputPath, lines.join("\n"), "utf8");
}

async function writeTransformedRoutesTsvFromLayoutJson(
  layoutJson: string,
  outputPath: string,
  transformPoint: (x: number, y: number) => { x: number; y: number },
): Promise<void> {
  const parsed = JSON.parse(layoutJson) as {
    routedEdges?: Array<{
      edgeId?: unknown;
      points?: Array<{ x?: unknown; y?: unknown }>;
    }>;
  };
  const lines: string[] = [];
  for (const edge of parsed.routedEdges ?? []) {
    if (typeof edge.edgeId !== "string" || !Array.isArray(edge.points)) {
      continue;
    }
    const cells = [tsvCell(edge.edgeId)];
    for (const point of edge.points) {
      const x = numericValue(point.x);
      const y = numericValue(point.y);
      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        continue;
      }
      const transformed = transformPoint(x, y);
      if (!Number.isFinite(transformed.x) || !Number.isFinite(transformed.y)) {
        continue;
      }
      cells.push(transformed.x.toFixed(3), transformed.y.toFixed(3));
    }
    if (cells.length >= 5) {
      lines.push(cells.join("\t"));
    }
  }
  await writeFile(outputPath, lines.join("\n"), "utf8");
}

type BboxTargetPositionsResult = {
  beforeAreaB: number;
  description: string;
  positionsPath: string;
  scale?: number;
  strategy: BboxTargetPositionStrategy;
  targetAreaB: number;
  transformPoint?: (x: number, y: number) => { x: number; y: number };
};

async function writePositionsTsvForBBoxTarget(
  layout: unknown,
  outputPath: string,
  targetAreaB: number,
  safety: number,
  strategy: BboxTargetPositionStrategy,
): Promise<BboxTargetPositionsResult | undefined> {
  if (strategy === "gap") {
    return writeGapCompressedPositionsTsvForBBoxTarget(
      layout,
      outputPath,
      targetAreaB,
      safety,
    );
  }
  if (strategy === "density-scale") {
    return writeDensityScaledPositionsTsvForBBoxTarget(
      layout,
      outputPath,
      targetAreaB,
      safety,
    );
  }
  return writeScaledPositionsTsvForBBoxTarget(
    layout,
    outputPath,
    targetAreaB,
    safety,
  );
}

async function writeScaledPositionsTsvForBBoxTarget(
  layout: unknown,
  outputPath: string,
  targetAreaB: number,
  safety: number,
): Promise<BboxTargetPositionsResult | undefined> {
  const parsed = layout as {
    nodes?: Array<{
      modelId?: unknown;
      position?: { x?: unknown; y?: unknown };
      size?: { height?: unknown; width?: unknown };
    }>;
  };
  const nodes = (parsed.nodes ?? []).filter(
    (node) => typeof node.modelId === "string" && node.position,
  );
  if (nodes.length === 0 || targetAreaB <= 0) {
    return undefined;
  }

  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  for (const node of nodes) {
    const x = numericValue(node.position?.x);
    const y = numericValue(node.position?.y);
    const width = numericValue(node.size?.width);
    const height = numericValue(node.size?.height);
    minX = Math.min(minX, x);
    minY = Math.min(minY, y);
    maxX = Math.max(maxX, x + width);
    maxY = Math.max(maxY, y + height);
  }

  const width = maxX - minX;
  const height = maxY - minY;
  const beforeArea = width * height;
  if (!Number.isFinite(beforeArea) || beforeArea <= 1) {
    return undefined;
  }
  const targetArea = targetAreaB * 1e9 * safety;
  const minScale = readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_MIN_SCALE", 0.35);
  const maxScale = readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_MAX_SCALE", 0.995);
  const scale = Math.max(
    minScale,
    Math.min(maxScale, Math.sqrt(targetArea / beforeArea)),
  );
  if (!Number.isFinite(scale) || scale >= maxScale) {
    return undefined;
  }

  const centerX = (minX + maxX) / 2;
  const centerY = (minY + maxY) / 2;
  const lines: string[] = [];
  for (const node of nodes) {
    if (typeof node.modelId !== "string") {
      continue;
    }
    const nodeCenterX =
      numericValue(node.position?.x) + numericValue(node.size?.width) / 2;
    const nodeCenterY =
      numericValue(node.position?.y) + numericValue(node.size?.height) / 2;
    const scaledX = centerX + (nodeCenterX - centerX) * scale;
    const scaledY = centerY + (nodeCenterY - centerY) * scale;
    lines.push(`${tsvCell(node.modelId)}\t${scaledX.toFixed(3)}\t${scaledY.toFixed(3)}`);
  }
  await writeFile(outputPath, lines.join("\n"), "utf8");
  return {
    beforeAreaB: beforeArea / 1e9,
    description:
      `scale positions by ${scale.toFixed(3)} for `
      + `${(beforeArea / 1e9).toFixed(2)}B -> ${(targetArea / 1e9).toFixed(2)}B`,
    positionsPath: outputPath,
    scale,
    strategy: "scale",
    targetAreaB: targetArea / 1e9,
    transformPoint: (x, y) => ({
      x: centerX + (x - centerX) * scale,
      y: centerY + (y - centerY) * scale,
    }),
  };
}

async function writeGapCompressedPositionsTsvForBBoxTarget(
  layout: unknown,
  outputPath: string,
  targetAreaB: number,
  safety: number,
): Promise<BboxTargetPositionsResult | undefined> {
  const parsed = layout as {
    nodes?: Array<{
      modelId?: unknown;
      position?: { x?: unknown; y?: unknown };
      size?: { height?: unknown; width?: unknown };
    }>;
  };
  const nodes = (parsed.nodes ?? [])
    .filter((node) => typeof node.modelId === "string" && node.position)
    .map((node) => {
      const x = numericValue(node.position?.x);
      const y = numericValue(node.position?.y);
      const width = numericValue(node.size?.width);
      const height = numericValue(node.size?.height);
      return {
        centerX: x + width / 2,
        centerY: y + height / 2,
        height,
        id: String(node.modelId),
        maxX: x + width,
        maxY: y + height,
        minX: x,
        minY: y,
        width,
      };
    });
  if (nodes.length === 0 || targetAreaB <= 0) {
    return undefined;
  }

  const minX = Math.min(...nodes.map((node) => node.minX));
  const minY = Math.min(...nodes.map((node) => node.minY));
  const maxX = Math.max(...nodes.map((node) => node.maxX));
  const maxY = Math.max(...nodes.map((node) => node.maxY));
  const width = maxX - minX;
  const height = maxY - minY;
  const beforeArea = width * height;
  if (!Number.isFinite(beforeArea) || beforeArea <= 1) {
    return undefined;
  }

  const targetArea = targetAreaB * 1e9 * safety;
  const targetScale = Math.max(
    readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_MIN_SCALE", 0.35),
    Math.min(
      readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_MAX_SCALE", 0.995),
      Math.sqrt(targetArea / beforeArea),
    ),
  );
  if (!Number.isFinite(targetScale) || targetScale >= 0.995) {
    return undefined;
  }

  const gapMinFactor = Math.max(
    0,
    readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_FACTOR", 0.85),
  );
  const minGapX = readOptionalFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_X")
    ?? median(nodes.map((node) => node.width)) * gapMinFactor;
  const minGapY = readOptionalFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_GAP_MIN_Y")
    ?? median(nodes.map((node) => node.height)) * gapMinFactor;
  const xPlan = buildGapCompressionAxisPlan(
    nodes.map((node) => ({ end: node.maxX, start: node.minX })),
    minX,
    maxX,
    width * targetScale,
    minGapX,
  );
  const yPlan = buildGapCompressionAxisPlan(
    nodes.map((node) => ({ end: node.maxY, start: node.minY })),
    minY,
    maxY,
    height * targetScale,
    minGapY,
  );
  const afterWidth = width - xPlan.reduction;
  const afterHeight = height - yPlan.reduction;
  const afterArea = afterWidth * afterHeight;
  if (
    !Number.isFinite(afterArea)
    || afterArea >= beforeArea * 0.999
    || xPlan.reduction + yPlan.reduction <= 1
  ) {
    return undefined;
  }

  const lines = nodes.map((node) => {
    const compressedX = xPlan.apply(node.centerX);
    const compressedY = yPlan.apply(node.centerY);
    return `${tsvCell(node.id)}\t${compressedX.toFixed(3)}\t${compressedY.toFixed(3)}`;
  });
  await writeFile(outputPath, lines.join("\n"), "utf8");
  return {
    beforeAreaB: beforeArea / 1e9,
    description:
      `gap-compress positions estimate ${(beforeArea / 1e9).toFixed(2)}B`
      + ` -> ${(afterArea / 1e9).toFixed(2)}B`
      + ` (requested ${(targetArea / 1e9).toFixed(2)}B, `
      + `xReduce=${(xPlan.reduction / Math.max(1, width)).toFixed(3)}, `
      + `yReduce=${(yPlan.reduction / Math.max(1, height)).toFixed(3)}, `
      + `gaps=${xPlan.gapCount}/${yPlan.gapCount})`,
    positionsPath: outputPath,
    scale: Math.sqrt(afterArea / beforeArea),
    strategy: "gap",
    targetAreaB: targetArea / 1e9,
    transformPoint: (x, y) => ({
      x: xPlan.apply(x),
      y: yPlan.apply(y),
    }),
  };
}

async function writeDensityScaledPositionsTsvForBBoxTarget(
  layout: unknown,
  outputPath: string,
  targetAreaB: number,
  safety: number,
): Promise<BboxTargetPositionsResult | undefined> {
  // Density-aware non-uniform 1D scaling: project node centers onto each axis,
  // bucket them into bins sized by median node size, and assign a per-bin
  // scale factor derived from local node density. Sparse bins (well below
  // median density) are compressed harder; dense bins are protected. This is
  // intentionally graph-agnostic: it uses no cluster IDs, no model names, and
  // no absolute coordinates — only the relative distribution of node centers
  // from the input layout.
  const parsed = layout as {
    nodes?: Array<{
      modelId?: unknown;
      position?: { x?: unknown; y?: unknown };
      size?: { height?: unknown; width?: unknown };
    }>;
  };
  const nodes = (parsed.nodes ?? [])
    .filter((node) => typeof node.modelId === "string" && node.position)
    .map((node) => {
      const x = numericValue(node.position?.x);
      const y = numericValue(node.position?.y);
      const width = numericValue(node.size?.width);
      const height = numericValue(node.size?.height);
      return {
        centerX: x + width / 2,
        centerY: y + height / 2,
        height,
        id: String(node.modelId),
        width,
      };
    });
  if (nodes.length === 0 || targetAreaB <= 0) {
    return undefined;
  }

  const minX = Math.min(...nodes.map((node) => node.centerX - node.width / 2));
  const minY = Math.min(...nodes.map((node) => node.centerY - node.height / 2));
  const maxX = Math.max(...nodes.map((node) => node.centerX + node.width / 2));
  const maxY = Math.max(...nodes.map((node) => node.centerY + node.height / 2));
  const width = maxX - minX;
  const height = maxY - minY;
  const beforeArea = width * height;
  if (!Number.isFinite(beforeArea) || beforeArea <= 1) {
    return undefined;
  }
  const targetArea = targetAreaB * 1e9 * safety;
  const targetScale = Math.max(
    readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_MIN_SCALE", 0.35),
    Math.min(
      readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_MAX_SCALE", 0.995),
      Math.sqrt(targetArea / beforeArea),
    ),
  );
  if (!Number.isFinite(targetScale) || targetScale >= 0.995) {
    return undefined;
  }

  // Cell size is the median node size scaled by an automatic factor — same
  // formula on small or huge graphs. The factor controls how coarse the
  // density mesh is; default 4× picks up neighborhood-scale density rather
  // than per-node noise.
  const cellFactor = Math.max(
    1,
    readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_CELL_FACTOR", 4),
  );
  const medianW = median(nodes.map((node) => node.width));
  const medianH = median(nodes.map((node) => node.height));
  const cellSizeX = Math.max(1, medianW * cellFactor);
  const cellSizeY = Math.max(1, medianH * cellFactor);
  // Sparse/dense distinction is a fraction of the bin-density median, never
  // an absolute count. Default 0.5 means "below half of typical density is
  // considered sparse" — same meaning on any graph scale.
  const sparseRatio = Math.max(
    0,
    Math.min(
      1,
      readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_SPARSE_RATIO", 0.5),
    ),
  );
  // Bias controls how aggressively sparse vs. dense bins differ. 0 falls back
  // to uniform scaling; 1 means sparse bins compress fully toward the target
  // length while dense bins stay near 1. When the env knob is set to "auto"
  // (default), bias is derived from the bin-density coefficient of variation
  // so a uniformly populated graph degrades gracefully to plain scaling and
  // a heterogeneous graph (large sparse + dense patches) gets more aggressive
  // sparse compression.
  const biasRaw = (process.env.DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS ?? "auto")
    .trim()
    .toLowerCase();
  const biasMode: "auto" | "fixed" = biasRaw === "auto" || biasRaw === ""
    ? "auto"
    : "fixed";
  const biasFixed = biasMode === "fixed"
    ? Math.max(0, Math.min(1, Number.parseFloat(biasRaw)))
    : 0.7;
  // CV thresholds: below `biasCvMin` we treat the distribution as essentially
  // uniform and decline the candidate. Above `biasCvMax` we cap bias near 1.
  // Linear interpolation between the two. These knobs are graph-agnostic; CV
  // is dimensionless.
  const biasCvMin = Math.max(
    0,
    readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_CV_MIN", 0.25),
  );
  const biasCvMax = Math.max(
    biasCvMin + 1e-6,
    readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_CV_MAX", 1.5),
  );
  const biasMinAuto = Math.max(
    0,
    Math.min(
      1,
      readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_AUTO_MIN", 0.4),
    ),
  );
  const biasMaxAuto = Math.max(
    biasMinAuto,
    Math.min(
      1,
      readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_BIAS_AUTO_MAX", 0.85),
    ),
  );

  // Optional pre-balance: blend each node's center toward a rank-uniform
  // position along each axis. This relaxes hotspots before density-scale
  // measures CV and computes the per-bin scale plan. The blend factor is
  // a normalized scalar in [0, 1] — 0 keeps the input layout as-is, 1
  // produces fully equalized positions (often breaks cluster shape, so
  // small fractions like 0.2-0.4 are typical). Graph-agnostic: only uses
  // coordinate rank from the input.
  const preBalanceBlend = Math.max(
    0,
    Math.min(
      1,
      readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_DENSITY_PRE_BALANCE", 0.25),
    ),
  );
  let workingCenters = nodes.map((node) => ({
    centerX: node.centerX,
    centerY: node.centerY,
  }));
  if (preBalanceBlend > 0 && nodes.length > 1) {
    const balancedX = rankUniformBlend(
      workingCenters.map((entry) => entry.centerX),
      minX,
      maxX,
      preBalanceBlend,
    );
    const balancedY = rankUniformBlend(
      workingCenters.map((entry) => entry.centerY),
      minY,
      maxY,
      preBalanceBlend,
    );
    workingCenters = workingCenters.map((entry, index) => ({
      centerX: balancedX[index] ?? entry.centerX,
      centerY: balancedY[index] ?? entry.centerY,
    }));
  }

  const xCv = axisDensityCv(
    workingCenters.map((entry) => entry.centerX),
    minX,
    maxX,
    cellSizeX,
  );
  const yCv = axisDensityCv(
    workingCenters.map((entry) => entry.centerY),
    minY,
    maxY,
    cellSizeY,
  );
  const maxCv = Math.max(xCv, yCv);
  if (biasMode === "auto" && maxCv < biasCvMin) {
    // Distribution is essentially uniform on both axes — density-scale would
    // produce a near-uniform candidate identical to plain scale. Skip so we
    // don't waste a candidate slot.
    return undefined;
  }
  const autoBlend =
    Math.max(0, Math.min(1, (maxCv - biasCvMin) / (biasCvMax - biasCvMin)));
  const bias = biasMode === "fixed"
    ? biasFixed
    : biasMinAuto + autoBlend * (biasMaxAuto - biasMinAuto);

  const xPlan = buildDensityAxisPlan(
    workingCenters.map((entry) => entry.centerX),
    minX,
    maxX,
    width * targetScale,
    cellSizeX,
    sparseRatio,
    bias,
  );
  const yPlan = buildDensityAxisPlan(
    workingCenters.map((entry) => entry.centerY),
    minY,
    maxY,
    height * targetScale,
    cellSizeY,
    sparseRatio,
    bias,
  );
  const afterWidth = width - xPlan.reduction;
  const afterHeight = height - yPlan.reduction;
  const afterArea = afterWidth * afterHeight;
  if (
    !Number.isFinite(afterArea)
    || afterArea >= beforeArea * 0.999
    || xPlan.reduction + yPlan.reduction <= 1
  ) {
    return undefined;
  }

  const lines = nodes.map((node, index) => {
    const center = workingCenters[index] ?? {
      centerX: node.centerX,
      centerY: node.centerY,
    };
    const compressedX = xPlan.apply(center.centerX);
    const compressedY = yPlan.apply(center.centerY);
    return `${tsvCell(node.id)}\t${compressedX.toFixed(3)}\t${compressedY.toFixed(3)}`;
  });
  await writeFile(outputPath, lines.join("\n"), "utf8");
  return {
    beforeAreaB: beforeArea / 1e9,
    description:
      `density-scale positions estimate ${(beforeArea / 1e9).toFixed(2)}B`
      + ` -> ${(afterArea / 1e9).toFixed(2)}B`
      + ` (requested ${(targetArea / 1e9).toFixed(2)}B, `
      + `xReduce=${(xPlan.reduction / Math.max(1, width)).toFixed(3)}, `
      + `yReduce=${(yPlan.reduction / Math.max(1, height)).toFixed(3)}, `
      + `xBins=${xPlan.binCount} sparse=${xPlan.sparseBins}/`
      + `dense=${xPlan.denseBins}, `
      + `yBins=${yPlan.binCount} sparse=${yPlan.sparseBins}/`
      + `dense=${yPlan.denseBins}, `
      + `cv=${xCv.toFixed(2)}/${yCv.toFixed(2)} bias=${bias.toFixed(2)}`
      + `${biasMode === "auto" ? " auto" : ""}`
      + (preBalanceBlend > 0
        ? `, preBalance=${preBalanceBlend.toFixed(2)}`
        : "")
      + ")",
    positionsPath: outputPath,
    scale: Math.sqrt(afterArea / beforeArea),
    strategy: "density-scale",
    targetAreaB: targetArea / 1e9,
    transformPoint: (x, y) => ({
      x: xPlan.apply(x),
      y: yPlan.apply(y),
    }),
  };
}

function rankUniformBlend(
  values: number[],
  minBound: number,
  maxBound: number,
  blend: number,
): number[] {
  // Each input value is blended toward its rank-uniform position along
  // [minBound, maxBound]. Rank is computed from the input order, so callers
  // operate on independent axes without touching the other dimension. Ties
  // are broken by the original index, preserving deterministic output for
  // identical inputs. Generic and graph-agnostic: no node identifiers used.
  if (values.length === 0 || !Number.isFinite(blend) || blend <= 0) {
    return values.slice();
  }
  if (!Number.isFinite(minBound) || !Number.isFinite(maxBound)) {
    return values.slice();
  }
  const span = maxBound - minBound;
  if (!Number.isFinite(span) || span <= 0) {
    return values.slice();
  }
  const clampBlend = Math.min(1, blend);
  const indexed = values.map((value, index) => ({ index, value }));
  indexed.sort((left, right) => {
    if (left.value === right.value) {
      return left.index - right.index;
    }
    return left.value - right.value;
  });
  const result = values.slice();
  const denom = Math.max(1, indexed.length - 1);
  for (let rank = 0; rank < indexed.length; rank += 1) {
    const entry = indexed[rank];
    const uniformValue = minBound + (rank / denom) * span;
    result[entry.index] =
      entry.value * (1 - clampBlend) + uniformValue * clampBlend;
  }
  return result;
}

function axisDensityCv(
  centers: number[],
  minBound: number,
  maxBound: number,
  cellSize: number,
): number {
  const length = maxBound - minBound;
  if (length <= 0 || !Number.isFinite(length) || cellSize <= 0) {
    return 0;
  }
  const binCount = Math.max(1, Math.ceil(length / cellSize));
  if (binCount < 2) {
    return 0;
  }
  const realCellSize = length / binCount;
  const counts = new Array<number>(binCount).fill(0);
  for (const coordinate of centers) {
    if (!Number.isFinite(coordinate)) continue;
    const offset = Math.min(
      binCount - 1,
      Math.max(0, Math.floor((coordinate - minBound) / realCellSize)),
    );
    counts[offset] += 1;
  }
  const mean = counts.reduce((sum, value) => sum + value, 0) / binCount;
  if (mean <= 0) {
    return 0;
  }
  const variance =
    counts.reduce((sum, value) => sum + (value - mean) ** 2, 0) / binCount;
  return Math.sqrt(variance) / mean;
}

function buildDensityAxisPlan(
  centers: number[],
  minBound: number,
  maxBound: number,
  targetLength: number,
  cellSize: number,
  sparseRatio: number,
  bias: number,
): {
  apply: (coordinate: number) => number;
  binCount: number;
  denseBins: number;
  reduction: number;
  sparseBins: number;
} {
  const length = maxBound - minBound;
  if (length <= 0 || !Number.isFinite(length) || cellSize <= 0) {
    return {
      apply: (coordinate) => coordinate,
      binCount: 0,
      denseBins: 0,
      reduction: 0,
      sparseBins: 0,
    };
  }
  const binCount = Math.max(1, Math.ceil(length / cellSize));
  const realCellSize = length / binCount;
  const counts = new Array<number>(binCount).fill(0);
  for (const coordinate of centers) {
    if (!Number.isFinite(coordinate)) {
      continue;
    }
    const offset = Math.min(
      binCount - 1,
      Math.max(0, Math.floor((coordinate - minBound) / realCellSize)),
    );
    counts[offset] += 1;
  }
  // Median density is the per-bin reference. Bins with zero or near-zero
  // counts get the most compression; bins above median keep their span.
  const medianCount = median(counts.filter((value) => value > 0));
  const sparseThreshold = medianCount > 0 ? medianCount * sparseRatio : 0;
  const denseThreshold = medianCount > 0 ? medianCount : 1;
  // Each bin gets a relative scale factor in [biasMinScale, 1]. The bias
  // parameter controls how much sparse bins shrink relative to dense bins.
  // To meet the requested total target length, we then renormalize all
  // factors uniformly so the sum equals targetLength.
  const minBinScale = Math.max(
    0.05,
    Math.min(1, 1 - bias),
  );
  const factors = counts.map((count) => {
    if (count <= sparseThreshold) {
      return minBinScale;
    }
    if (count >= denseThreshold) {
      return 1;
    }
    const t = (count - sparseThreshold)
      / Math.max(1e-6, denseThreshold - sparseThreshold);
    return minBinScale + t * (1 - minBinScale);
  });
  let sparseBins = 0;
  let denseBins = 0;
  for (const count of counts) {
    if (count <= sparseThreshold) sparseBins += 1;
    else if (count >= denseThreshold) denseBins += 1;
  }
  const totalFactor = factors.reduce((sum, value) => sum + value, 0);
  if (totalFactor <= 0 || !Number.isFinite(totalFactor)) {
    return {
      apply: (coordinate) => coordinate,
      binCount,
      denseBins,
      reduction: 0,
      sparseBins,
    };
  }
  // Per-bin width budget: each bin's new width is `scaled[i] * realCellSize`,
  // and we need their sum to equal `targetLength`. Solving for the proportional
  // factor: scaled[i] = factor[i] * targetLength / (realCellSize * totalFactor).
  const renorm = targetLength / (realCellSize * totalFactor);
  const scaled = factors.map((factor) => Math.max(0, factor * renorm));
  // Build cumulative offsets so we can map any input coordinate to its new
  // axis position in O(log bins) via binary search.
  const binStarts = new Array<number>(binCount);
  const binNewStarts = new Array<number>(binCount);
  let acc = 0;
  for (let index = 0; index < binCount; index += 1) {
    binStarts[index] = minBound + index * realCellSize;
    binNewStarts[index] = minBound + acc;
    acc += scaled[index] * realCellSize;
  }
  const newLength = acc;
  const reduction = Math.max(0, length - newLength);
  return {
    apply: (coordinate) => {
      if (!Number.isFinite(coordinate)) return coordinate;
      const offset = Math.min(
        binCount - 1,
        Math.max(0, Math.floor((coordinate - minBound) / realCellSize)),
      );
      const within = coordinate - binStarts[offset];
      return binNewStarts[offset] + within * scaled[offset];
    },
    binCount,
    denseBins,
    reduction,
    sparseBins,
  };
}

function buildGapCompressionAxisPlan(
  spans: Array<{ end: number; start: number }>,
  minBound: number,
  maxBound: number,
  targetLength: number,
  minGap: number,
): {
  apply: (coordinate: number) => number;
  gapCount: number;
  reduction: number;
} {
  const length = maxBound - minBound;
  const desiredReduction = Math.max(0, length - targetLength);
  const validSpans = spans
    .filter(
      (span) =>
        Number.isFinite(span.start)
        && Number.isFinite(span.end)
        && span.end >= span.start,
    )
    .sort((left, right) => left.start - right.start);
  const merged: Array<{ end: number; start: number }> = [];
  for (const span of validSpans) {
    const last = merged.at(-1);
    if (!last || span.start > last.end) {
      merged.push({ end: span.end, start: span.start });
    } else {
      last.end = Math.max(last.end, span.end);
    }
  }

  const gaps: Array<{ end: number; reducible: number; reduction: number }> = [];
  for (let index = 0; index + 1 < merged.length; index++) {
    const gapStart = merged[index].end;
    const gapEnd = merged[index + 1].start;
    const reducible = gapEnd - gapStart - Math.max(0, minGap);
    if (reducible > 1) {
      gaps.push({ end: gapEnd, reducible, reduction: 0 });
    }
  }
  const capacity = gaps.reduce((sum, gap) => sum + gap.reducible, 0);
  const targetReduction = Math.min(desiredReduction, capacity);
  if (targetReduction <= 1 || capacity <= 1) {
    return {
      apply: (coordinate) => coordinate,
      gapCount: gaps.length,
      reduction: 0,
    };
  }

  const reductionRatio = targetReduction / capacity;
  let actualReduction = 0;
  for (const gap of gaps) {
    gap.reduction = Math.min(gap.reducible, gap.reducible * reductionRatio);
    actualReduction += gap.reduction;
  }
  return {
    apply: (coordinate) => {
      let shift = 0;
      for (const gap of gaps) {
        if (coordinate >= gap.end) {
          shift += gap.reduction;
        }
      }
      return coordinate - shift;
    },
    gapCount: gaps.length,
    reduction: actualReduction,
  };
}

function median(values: number[]): number {
  const sorted = values
    .filter((value) => Number.isFinite(value) && value >= 0)
    .sort((left, right) => left - right);
  if (sorted.length === 0) {
    return 0;
  }
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

function numericValue(value: unknown): number {
  return typeof value === "number" && Number.isFinite(value) ? value : 0;
}

function readStringEnv(name: string, fallback: string): string {
  const value = process.env[name]?.trim();
  return value && value.length > 0 ? value : fallback;
}

function readBoolEnv(name: string, fallback: boolean): boolean {
  const value = process.env[name]?.trim().toLowerCase();
  if (!value) {
    return fallback;
  }
  return value !== "0" && value !== "false" && value !== "no";
}

function readEdgeNodePolishVariants(): EdgeNodePolishVariant[] {
  const raw = readStringEnv(
    "DJERD_OPTIMIZED_EDGE_NODE_POLISH_VARIANTS",
    "cheap,local,holistic",
  );
  const variants: EdgeNodePolishVariant[] = [];
  for (const part of raw.split(",")) {
    const value = part.trim().toLowerCase();
    const variant: EdgeNodePolishVariant | undefined =
      value === "cheap" || value === "narrow" || value === "safe"
        ? "cheap"
        : value === "local" || value === "minimal" || value === "pure"
          ? "local"
          : value === "holistic" || value === "broad" || value === "poststack"
            ? "holistic"
            : undefined;
    if (variant && !variants.includes(variant)) {
      variants.push(variant);
    }
  }
  return variants.length > 0 ? variants : ["cheap", "local", "holistic"];
}

function readVisualCrossPolishVariants(): VisualCrossPolishVariant[] {
  const raw = readStringEnv(
    "DJERD_OPTIMIZED_VISUAL_CROSS_POLISH_VARIANTS",
    DEFAULT_VISUAL_CROSS_POLISH_VARIANTS,
  );
  const variants: VisualCrossPolishVariant[] = [];
  for (const part of raw.split(",")) {
    const value = part.trim().toLowerCase();
    const variant = parseVisualCrossPolishVariant(value);
    if (variant && !variants.includes(variant)) {
      variants.push(variant);
    }
  }
  return variants.length > 0
    ? variants
    : [
      "route",
      "route-clear",
      "route-clear-tight",
      "route-clear-wide",
      "route-clear-short",
      "route-clear-deep",
      "route-clear-lbend",
      "route-clear-deep-lbend",
      "route-clear-periphery",
      "route-clear-deep-periphery",
      "detour",
      "detour-clear",
      "knot",
      "knot-clear",
    ];
}

function parseVisualCrossPolishVariant(
  value: string,
): VisualCrossPolishVariant | undefined {
  switch (value) {
    case "route":
    case "routes":
      return "route";
    case "route-clear":
    case "routes-clear":
    case "route-bundle-clear":
    case "route-repair":
      return "route-clear";
    case "route-clear-tight":
    case "route-tight":
    case "tight-clear":
    case "route-repair-tight":
      return "route-clear-tight";
    case "route-clear-wide":
    case "route-wide":
    case "wide-clear":
    case "route-repair-wide":
      return "route-clear-wide";
    case "route-clear-short":
    case "route-short":
    case "short-clear":
    case "route-repair-short":
      return "route-clear-short";
    case "route-clear-deep":
    case "route-deep":
    case "deep-clear":
    case "route-repair-deep":
      return "route-clear-deep";
    case "route-clear-lbend":
    case "route-lbend":
    case "route-l-bend":
    case "lbend":
    case "l-bend":
    case "lbend-clear":
    case "l-bend-clear":
    case "route-repair-lbend":
      return "route-clear-lbend";
    case "route-clear-deep-lbend":
    case "route-clear-lbend-deep":
    case "route-deep-lbend":
    case "route-deep-l-bend":
    case "deep-lbend":
    case "deep-l-bend":
    case "lbend-deep":
    case "l-bend-deep":
    case "route-repair-deep-lbend":
      return "route-clear-deep-lbend";
    case "route-clear-periphery":
    case "route-periphery":
    case "periphery-clear":
    case "route-repair-periphery":
      return "route-clear-periphery";
    case "route-clear-deep-periphery":
    case "route-deep-periphery":
    case "deep-periphery":
    case "periphery-deep":
    case "route-repair-deep-periphery":
      return "route-clear-deep-periphery";
    case "route-retouch":
    case "retouch":
    case "diagonal-retouch":
    case "route-diagonal-retouch":
      return "route-retouch";
    case "detour":
    case "xings":
    case "xings-detour":
      return "detour";
    case "detour-clear":
    case "xings-clear":
    case "xings-detour-clear":
    case "detour-repair":
      return "detour-clear";
    case "knot":
    case "visual-knot":
    case "swap":
      return "knot";
    case "knot-clear":
    case "visual-knot-clear":
    case "swap-clear":
    case "knot-repair":
      return "knot-clear";
    default:
      return undefined;
  }
}

function readBboxTargetVariants(): BboxTargetVariant[] {
  const raw = readStringEnv(
    "DJERD_OPTIMIZED_BBOX_TARGET_VARIANTS",
    "local,holistic",
  );
  const variants: BboxTargetVariant[] = [];
  for (const part of raw.split(",")) {
    const value = part.trim().toLowerCase();
    const variant =
      value === "local" || value === "minimal" || value === "pure"
        ? "local"
        : value === "holistic" || value === "broad" || value === "poststack"
          ? "holistic"
          : undefined;
    if (variant && !variants.includes(variant)) {
      variants.push(variant);
    }
  }
  return variants.length > 0 ? variants : ["local", "holistic"];
}

function readBboxTargetPositionStrategies(): BboxTargetPositionStrategy[] {
  const raw = readStringEnv(
    "DJERD_OPTIMIZED_BBOX_TARGET_POSITION_STRATEGIES",
    "gap,density-scale,scale",
  );
  const strategies: BboxTargetPositionStrategy[] = [];
  for (const part of raw.split(",")) {
    const value = part.trim().toLowerCase();
    const strategy: BboxTargetPositionStrategy | undefined =
      value === "gap" || value === "bands" || value === "empty-gaps"
        ? "gap"
        : value === "density-scale"
            || value === "density"
            || value === "non-uniform"
          ? "density-scale"
          : value === "scale" || value === "uniform"
            ? "scale"
            : undefined;
    if (strategy && !strategies.includes(strategy)) {
      strategies.push(strategy);
    }
  }
  return strategies.length > 0 ? strategies : ["gap", "density-scale", "scale"];
}

function readPositiveIntEnv(name: string, fallback: number): number {
  const parsed = Number.parseInt(process.env[name] ?? "", 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

function readNonNegativeIntEnv(name: string, fallback: number): number {
  const parsed = Number.parseInt(process.env[name] ?? "", 10);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : fallback;
}

function readOptionalPositiveIntEnv(name: string): number | undefined {
  const value = process.env[name]?.trim();
  if (!value || value.toLowerCase() === "auto") {
    return undefined;
  }
  const parsed = Number.parseInt(value, 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : undefined;
}

function readOptionalNonNegativeIntEnv(name: string): number | undefined {
  const value = process.env[name]?.trim();
  if (!value || value.toLowerCase() === "auto") {
    return undefined;
  }
  const parsed = Number.parseInt(value, 10);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : undefined;
}

function readFloatEnv(name: string, fallback: number): number {
  const parsed = Number.parseFloat(process.env[name] ?? "");
  return Number.isFinite(parsed) ? parsed : fallback;
}

function readFloatListEnv(name: string, fallback: number[]): number[] {
  const raw = process.env[name]?.trim();
  if (!raw || raw.toLowerCase() === "auto") {
    return [...fallback];
  }
  const parsed = raw
    .split(",")
    .map((part) => Number.parseFloat(part.trim()))
    .filter((value) => Number.isFinite(value));
  return parsed.length > 0 ? parsed : [...fallback];
}

function readOptionalFloatEnv(name: string): number | undefined {
  const value = process.env[name]?.trim();
  if (!value || value.toLowerCase() === "auto") {
    return undefined;
  }
  const parsed = Number.parseFloat(value);
  return Number.isFinite(parsed) ? parsed : undefined;
}

function ratioGain(before: number, after: number): number {
  if (!Number.isFinite(before) || before <= 0 || !Number.isFinite(after)) {
    return 0;
  }
  return Math.max(0, (before - after) / before);
}

function buildVisualCrossRecompactTargets(
  baseAreaB: number,
  floorTargetB: number,
  targetRatios: number[],
  explicitTargetsB: number[],
): number[] {
  const candidates = [
    ...explicitTargetsB,
    ...targetRatios
      .filter((ratio) => Number.isFinite(ratio) && ratio > 0 && ratio < 1)
      .map((ratio) => baseAreaB * ratio),
    floorTargetB,
  ].filter(
    (target) =>
      Number.isFinite(target)
      && target > 0
      && target < baseAreaB * 0.999,
  );
  const sorted = candidates.sort((left, right) => right - left);
  const unique: number[] = [];
  for (const target of sorted) {
    if (!unique.some((existing) => Math.abs(existing - target) < 0.001)) {
      unique.push(target);
    }
  }
  return unique.length > 0 ? unique : [Math.max(0.05, floorTargetB)];
}

function positiveDeltaRatio(
  after: number,
  before: number,
  denominator: number,
): number {
  if (!Number.isFinite(after) || !Number.isFinite(before)) {
    return 0;
  }
  const delta = after - before;
  if (delta <= 0) {
    return 0;
  }
  const safeDenominator =
    Number.isFinite(denominator) && denominator > 0
      ? denominator
      : Math.max(1, before);
  return delta / safeDenominator;
}

function readBboxTargetSafeties(): number[] {
  const list = process.env.DJERD_OPTIMIZED_BBOX_TARGET_SAFETIES;
  if (list !== undefined && list.trim().length > 0) {
    const parsed = list
      .split(",")
      .map((part) => Number.parseFloat(part.trim()))
      .filter((value) => Number.isFinite(value) && value > 0);
    if (parsed.length > 0) {
      return parsed;
    }
  }

  if (process.env.DJERD_OPTIMIZED_BBOX_TARGET_SAFETY !== undefined) {
    const parsed = [readFloatEnv("DJERD_OPTIMIZED_BBOX_TARGET_SAFETY", 0.99)]
      .filter((value) => Number.isFinite(value) && value > 0);
    return parsed.length > 0 ? parsed : [0.99];
  }

  return [0.99, 0.94, 0.90];
}

function readBboxTargetStages(finalTargetB: number, initialAreaB: number): number[] {
  const list = process.env.DJERD_OPTIMIZED_BBOX_TARGET_STAGES_B;
  const parsed = list !== undefined && list.trim().length > 0
    ? list
      .split(",")
      .map((part) => Number.parseFloat(part.trim()))
      .filter((value) => Number.isFinite(value) && value > 0)
    : readBboxTargetRelativeStages(finalTargetB, initialAreaB);
  const stages = parsed.length > 0 ? parsed : [finalTargetB];
  const sorted = [...stages, finalTargetB]
    .filter((value) => Number.isFinite(value) && value > 0)
    .sort((left, right) => right - left);
  const unique: number[] = [];
  for (const stage of sorted) {
    if (!unique.some((existing) => Math.abs(existing - stage) < 0.001)) {
      unique.push(stage);
    }
  }
  return unique;
}

function readBboxTargetRelativeStages(finalTargetB: number, initialAreaB: number): number[] {
  if (!Number.isFinite(initialAreaB) || initialAreaB <= finalTargetB) {
    return [finalTargetB];
  }
  // 6 coarser stages (was 10). Each stage is a separate cluster_graph binary
  // call (~16s on the inheritance graph), and ~83% of candidates are rejected,
  // so halving the stage count is a ~40% perf win for a small compaction-step
  // granularity cost. The qualityDebtPerGain gate still rejects bad jumps.
  const raw = readStringEnv(
    "DJERD_OPTIMIZED_BBOX_TARGET_STAGE_RATIOS",
    "0.80,0.63,0.47,0.34,0.23,0.14",
  );
  const ratios = raw
    .split(",")
    .map((part) => Number.parseFloat(part.trim()))
    .filter((value) => Number.isFinite(value) && value > 0 && value < 0.995)
    .sort((left, right) => right - left);
  const stages = ratios
    .map((ratio) => initialAreaB * ratio)
    .filter((stage) => stage > finalTargetB * 1.03 && stage < initialAreaB * 0.995);
  return stages.length > 0 ? stages : [finalTargetB];
}

class OgdfExecError extends Error {
  constructor(
    message: string,
    readonly details: {
      code?: number | string | null;
      killed: boolean;
      signal?: string;
      stderr: string;
      stdout: string;
      timedOut: boolean;
    },
  ) {
    super(message);
    this.name = "OgdfExecError";
  }
}

function buildExecFailureMessage(error: Error, stderr: string): string {
  const stderrSummary = trimFailureText(stderr);
  return [error.message, stderrSummary].filter(Boolean).join(" · ");
}

function formatOgdfFailureReason(error: unknown): string {
  if (error instanceof OgdfExecError) {
    if (error.details.timedOut) {
      return `native layout timed out after ${OGDF_LAYOUT_TIMEOUT_MS}ms`;
    }

    const fragments = ["native layout process failed"];
    if (error.details.code !== undefined && error.details.code !== null) {
      fragments.push(`exitCode=${error.details.code}`);
    }
    if (error.details.signal) {
      fragments.push(`signal=${error.details.signal}`);
    }

    const stderrSummary = trimFailureText(error.details.stderr);
    if (stderrSummary) {
      fragments.push(stderrSummary);
    }

    return fragments.join(" · ");
  }

  return error instanceof Error ? error.message : String(error);
}

async function writeFailureArtifacts(
  directory: string,
  error: unknown,
  reason: string,
): Promise<void> {
  const details = error instanceof OgdfExecError ? error.details : undefined;
  const artifactWrites = [
    writeFile(
      path.join(directory, "failure.json"),
      `${JSON.stringify(
        {
          code: details?.code,
          killed: details?.killed,
          reason,
          signal: details?.signal,
          timedOut: details?.timedOut,
        },
        null,
        2,
      )}\n`,
      "utf8",
    ),
  ];

  if (details) {
    artifactWrites.push(
      writeFile(path.join(directory, "stdout.txt"), details.stdout, "utf8"),
      writeFile(path.join(directory, "stderr.txt"), details.stderr, "utf8"),
    );
  }

  await Promise.allSettled(artifactWrites);
}

function buildOgdfReproductionCommand(
  binaryPath: string,
  mode: LayoutMode,
  nodesPath: string,
  edgesPath: string,
): string {
  return [
    shellArg(binaryPath),
    "layout",
    "--mode",
    shellArg(mode),
    "--nodes-file",
    shellArg(nodesPath),
    "--edges-file",
    shellArg(edgesPath),
  ].join(" ");
}

function shellArg(value: string): string {
  return `'${value.replace(/'/g, `'\\''`)}'`;
}

function trimFailureText(value: string): string {
  const trimmed = value.trim();
  if (!trimmed) {
    return "";
  }

  return trimmed.length > 240
    ? `${trimmed.slice(0, 237)}...`
    : trimmed;
}

function readExecErrorCode(error: Error): number | string | null | undefined {
  if (!("code" in error)) {
    return undefined;
  }

  const code = error.code;
  return typeof code === "number" || typeof code === "string" || code === null
    ? code
    : undefined;
}

function serializeNodes(payload: DiagramBootstrapPayload): string {
  const appLabelByModelId = new Map(
    payload.graph.nodes.map((node) => [node.modelId, node.appLabel]),
  );
  return payload.layout.nodes
    .map((node) =>
      [
        tsvCell(node.modelId),
        numberCell(node.size.width),
        numberCell(node.size.height),
        numberCell(node.position.x),
        numberCell(node.position.y),
        tsvCell(appLabelByModelId.get(node.modelId) ?? ""),
      ].join("\t"),
    )
    .join("\n");
}

function serializeEdges(edges: readonly StructuralGraphEdge[]): string {
  return edges
    .map((edge) =>
      [
        tsvCell(edge.id),
        tsvCell(edge.sourceModelId),
        tsvCell(edge.targetModelId),
        tsvCell(edge.kind),
        tsvCell(edge.provenance),
      ].join("\t"),
    )
    .join("\n");
}

function numberCell(value: number): string {
  return Number.isFinite(value) ? String(value) : "0";
}

function tsvCell(value: string): string {
  return value.replace(/\t/g, " ").replace(/\r?\n/g, " ");
}

function summarizeLayout(layout: LayoutSnapshot): {
  bboxHeight: number;
  bboxWidth: number;
  nodeBBoxHeight: number;
  nodeBBoxWidth: number;
  routeBBoxHeight: number;
  routeBBoxWidth: number;
  routePointCount: number;
} {
  const nodeBounds = emptyBounds();
  const routeBounds = emptyBounds();
  const combinedBounds = emptyBounds();
  let routePointCount = 0;

  for (const node of layout.nodes) {
    updateBounds(nodeBounds, node.position.x, node.position.y);
    updateBounds(nodeBounds, node.position.x + node.size.width, node.position.y + node.size.height);
    updateBounds(combinedBounds, node.position.x, node.position.y);
    updateBounds(combinedBounds, node.position.x + node.size.width, node.position.y + node.size.height);
  }

  for (const edge of layout.routedEdges) {
    for (const point of edge.points) {
      routePointCount += 1;
      updateBounds(routeBounds, point.x, point.y);
      updateBounds(combinedBounds, point.x, point.y);
    }
  }

  return {
    bboxHeight: boundsHeight(combinedBounds),
    bboxWidth: boundsWidth(combinedBounds),
    nodeBBoxHeight: boundsHeight(nodeBounds),
    nodeBBoxWidth: boundsWidth(nodeBounds),
    routeBBoxHeight: boundsHeight(routeBounds),
    routeBBoxWidth: boundsWidth(routeBounds),
    routePointCount,
  };
}

function emptyBounds(): {
  maxX: number;
  maxY: number;
  minX: number;
  minY: number;
} {
  return {
    maxX: Number.NEGATIVE_INFINITY,
    maxY: Number.NEGATIVE_INFINITY,
    minX: Number.POSITIVE_INFINITY,
    minY: Number.POSITIVE_INFINITY,
  };
}

function updateBounds(
  bounds: { maxX: number; maxY: number; minX: number; minY: number },
  x: number,
  y: number,
): void {
  bounds.minX = Math.min(bounds.minX, x);
  bounds.minY = Math.min(bounds.minY, y);
  bounds.maxX = Math.max(bounds.maxX, x);
  bounds.maxY = Math.max(bounds.maxY, y);
}

function boundsWidth(bounds: { maxX: number; minX: number }): number {
  return Number.isFinite(bounds.minX) && Number.isFinite(bounds.maxX)
    ? Math.max(0, bounds.maxX - bounds.minX)
    : 0;
}

function boundsHeight(bounds: { maxY: number; minY: number }): number {
  return Number.isFinite(bounds.minY) && Number.isFinite(bounds.maxY)
    ? Math.max(0, bounds.maxY - bounds.minY)
    : 0;
}
