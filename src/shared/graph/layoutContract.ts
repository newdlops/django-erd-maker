import type { ModelId } from "../domain/modelIdentity";

export type CrossingMarkerStyle = "bridge" | "marker";
export const ANALYZER_LAYOUT_MODES = ["hierarchical", "circular", "clustered"] as const;
export type AnalyzerLayoutMode = (typeof ANALYZER_LAYOUT_MODES)[number];

export const OGDF_LAYOUT_TOOLBAR_MODES = [
  "hierarchical_barycenter",
  "hierarchical_sifting",
  "planarization",
  "planarization_grid",
  "uml_planarization",
  "ortho",
  "upward_layer_based",
  "circular",
  "fast_multipole",
  "fmmm",
  "stress_minimization",
  "davidson_harel",
  "tree",
  "radial_tree",
] as const;
export type ToolbarLayoutMode = (typeof OGDF_LAYOUT_TOOLBAR_MODES)[number];

export const OGDF_LAYOUT_MODES = [
  ...OGDF_LAYOUT_TOOLBAR_MODES,
  "clustered",
  "cluster_ortho",
  "cluster_planarization",
  "constrained_force",
  "constrained_force_straight",
  "fast_multipole_multilevel",
  "hierarchical",
  "hierarchical_global_sifting",
  "hierarchical_greedy_insert",
  "hierarchical_greedy_switch",
  "hierarchical_grid_sifting",
  "hierarchical_split",
  "linear",
  "pivot_mds",
  "planar_draw",
  "planar_straight",
  "schnyder",
  "uml_ortho",
  "upward_planarization",
  "visibility",
] as const;
export type LayoutMode = (typeof OGDF_LAYOUT_MODES)[number];
export const DEFAULT_LAYOUT_MODE: LayoutMode = "hierarchical_barycenter";

export const EDGE_ROUTING_STYLES = ["orthogonal", "straight", "straight_smart"] as const;
export type EdgeRoutingStyle = (typeof EDGE_ROUTING_STYLES)[number];
export const DEFAULT_EDGE_ROUTING: EdgeRoutingStyle = "straight";

export interface OgdfLayoutDefinition {
  analyzerMode: AnalyzerLayoutMode;
  family:
    | "cluster"
    | "energy"
    | "layered"
    | "legacy"
    | "linear"
    | "orthogonal"
    | "planar"
    | "tree"
    | "uml"
    | "upward";
  id: LayoutMode;
  label: string;
  ogdfClass: string;
  shortLabel: string;
  toolbar: boolean;
}

const OGDF_LAYOUT_DEFINITIONS: Record<LayoutMode, OgdfLayoutDefinition> = {
  circular: {
    analyzerMode: "circular",
    family: "layered",
    id: "circular",
    label: "Circular Layout",
    ogdfClass: "CircularLayout",
    shortLabel: "Circular",
    toolbar: true,
  },
  cluster_ortho: {
    analyzerMode: "clustered",
    family: "cluster",
    id: "cluster_ortho",
    label: "Cluster Orthogonal Layout",
    ogdfClass: "ClusterPlanarizationLayout + ClusterOrthoLayout",
    shortLabel: "Cluster Ortho",
    toolbar: false,
  },
  cluster_planarization: {
    analyzerMode: "clustered",
    family: "cluster",
    id: "cluster_planarization",
    label: "Cluster Planarization Layout",
    ogdfClass: "ClusterPlanarizationLayout",
    shortLabel: "Cluster Planar",
    toolbar: false,
  },
  clustered: {
    analyzerMode: "clustered",
    family: "legacy",
    id: "clustered",
    label: "Clustered Layout (legacy alias for Fast Multipole Embedder)",
    ogdfClass: "FastMultipoleEmbedder",
    shortLabel: "Clustered",
    toolbar: false,
  },
  constrained_force: {
    analyzerMode: "clustered",
    family: "energy",
    id: "constrained_force",
    label: "Constrained Force-Directed Layout",
    ogdfClass: "FMMMLayout + StressMinimization",
    shortLabel: "Constrained Force",
    toolbar: false,
  },
  constrained_force_straight: {
    analyzerMode: "clustered",
    family: "energy",
    id: "constrained_force_straight",
    label: "Constrained Force-Directed (Straight)",
    ogdfClass: "FMMMLayout + StressMinimization (straight-line)",
    shortLabel: "Constr. Straight",
    toolbar: false,
  },
  davidson_harel: {
    analyzerMode: "clustered",
    family: "energy",
    id: "davidson_harel",
    label: "Davidson-Harel Layout",
    ogdfClass: "DavidsonHarelLayout",
    shortLabel: "Davidson-Harel",
    toolbar: true,
  },
  fast_multipole: {
    analyzerMode: "clustered",
    family: "energy",
    id: "fast_multipole",
    label: "Fast Multipole Embedder",
    ogdfClass: "FastMultipoleEmbedder",
    shortLabel: "Fast MP",
    toolbar: true,
  },
  fast_multipole_multilevel: {
    analyzerMode: "clustered",
    family: "energy",
    id: "fast_multipole_multilevel",
    label: "Fast Multipole Multilevel Embedder",
    ogdfClass: "FastMultipoleMultilevelEmbedder",
    shortLabel: "Fast MP ML",
    toolbar: false,
  },
  fmmm: {
    analyzerMode: "clustered",
    family: "energy",
    id: "fmmm",
    label: "FMMM Layout",
    ogdfClass: "FMMMLayout",
    shortLabel: "FMMM",
    toolbar: true,
  },
  hierarchical: {
    analyzerMode: "hierarchical",
    family: "legacy",
    id: "hierarchical",
    label: "Hierarchical Layout (analyzer family alias for Sugiyama Barycenter)",
    ogdfClass: "SugiyamaLayout + BarycenterHeuristic",
    shortLabel: "Hierarchical",
    toolbar: false,
  },
  hierarchical_barycenter: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_barycenter",
    label: "Sugiyama Layout (Barycenter)",
    ogdfClass: "SugiyamaLayout + BarycenterHeuristic",
    shortLabel: "Hier Bary",
    toolbar: true,
  },
  hierarchical_global_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_global_sifting",
    label: "Sugiyama Layout (Global Sifting)",
    ogdfClass: "SugiyamaLayout + GlobalSifting",
    shortLabel: "Hier GSift",
    toolbar: false,
  },
  hierarchical_greedy_insert: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_greedy_insert",
    label: "Sugiyama Layout (Greedy Insert)",
    ogdfClass: "SugiyamaLayout + GreedyInsertHeuristic",
    shortLabel: "Hier GIns",
    toolbar: false,
  },
  hierarchical_greedy_switch: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_greedy_switch",
    label: "Sugiyama Layout (Greedy Switch)",
    ogdfClass: "SugiyamaLayout + GreedySwitchHeuristic",
    shortLabel: "Hier GSwitch",
    toolbar: false,
  },
  hierarchical_grid_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_grid_sifting",
    label: "Sugiyama Layout (Grid Sifting)",
    ogdfClass: "SugiyamaLayout + GridSifting",
    shortLabel: "Hier GridSift",
    toolbar: false,
  },
  hierarchical_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_sifting",
    label: "Sugiyama Layout (Sifting)",
    ogdfClass: "SugiyamaLayout + SiftingHeuristic",
    shortLabel: "Hier Sift",
    toolbar: true,
  },
  hierarchical_split: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_split",
    label: "Sugiyama Layout (Split)",
    ogdfClass: "SugiyamaLayout + SplitHeuristic",
    shortLabel: "Hier Split",
    toolbar: false,
  },
  linear: {
    analyzerMode: "hierarchical",
    family: "linear",
    id: "linear",
    label: "Linear Layout",
    ogdfClass: "LinearLayout",
    shortLabel: "Linear",
    toolbar: false,
  },
  ortho: {
    analyzerMode: "hierarchical",
    family: "orthogonal",
    id: "ortho",
    label: "Orthogonal Layout",
    ogdfClass: "PlanarizationLayout + OrthoLayout",
    shortLabel: "Ortho",
    toolbar: true,
  },
  pivot_mds: {
    analyzerMode: "clustered",
    family: "energy",
    id: "pivot_mds",
    label: "Pivot MDS Layout",
    ogdfClass: "PivotMDS",
    shortLabel: "Pivot MDS",
    toolbar: false,
  },
  planar_draw: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "planar_draw",
    label: "Planar Draw Layout",
    ogdfClass: "PlanarDrawLayout",
    shortLabel: "Planar Draw",
    toolbar: false,
  },
  planar_straight: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "planar_straight",
    label: "Planar Straight Layout",
    ogdfClass: "PlanarStraightLayout",
    shortLabel: "Planar Str",
    toolbar: false,
  },
  planarization: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "planarization",
    label: "Planarization Layout",
    ogdfClass: "PlanarizationLayout",
    shortLabel: "Planar",
    toolbar: true,
  },
  planarization_grid: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "planarization_grid",
    label: "Planarization Grid Layout",
    ogdfClass: "PlanarizationGridLayout",
    shortLabel: "Planar Grid",
    toolbar: true,
  },
  radial_tree: {
    analyzerMode: "hierarchical",
    family: "tree",
    id: "radial_tree",
    label: "Radial Tree Layout",
    ogdfClass: "RadialTreeLayout",
    shortLabel: "Radial Tree",
    toolbar: true,
  },
  schnyder: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "schnyder",
    label: "Schnyder Layout",
    ogdfClass: "SchnyderLayout",
    shortLabel: "Schnyder",
    toolbar: false,
  },
  stress_minimization: {
    analyzerMode: "clustered",
    family: "energy",
    id: "stress_minimization",
    label: "Stress Minimization Layout",
    ogdfClass: "StressMinimization",
    shortLabel: "Stress Min",
    toolbar: true,
  },
  tree: {
    analyzerMode: "hierarchical",
    family: "tree",
    id: "tree",
    label: "Tree Layout",
    ogdfClass: "TreeLayout",
    shortLabel: "Tree",
    toolbar: true,
  },
  uml_ortho: {
    analyzerMode: "hierarchical",
    family: "uml",
    id: "uml_ortho",
    label: "UML Orthogonal Layout",
    ogdfClass: "PlanarizationLayoutUML + OrthoLayoutUML",
    shortLabel: "UML Ortho",
    toolbar: false,
  },
  uml_planarization: {
    analyzerMode: "hierarchical",
    family: "uml",
    id: "uml_planarization",
    label: "UML Planarization Layout",
    ogdfClass: "PlanarizationLayoutUML",
    shortLabel: "UML Planar",
    toolbar: true,
  },
  upward_layer_based: {
    analyzerMode: "hierarchical",
    family: "upward",
    id: "upward_layer_based",
    label: "Layer-Based Upward Layout",
    ogdfClass: "UpwardPlanarizationLayout + LayerBasedUPRLayout",
    shortLabel: "Upward",
    toolbar: true,
  },
  upward_planarization: {
    analyzerMode: "hierarchical",
    family: "upward",
    id: "upward_planarization",
    label: "Upward Planarization Layout",
    ogdfClass: "UpwardPlanarizationLayout",
    shortLabel: "Upward Planar",
    toolbar: false,
  },
  visibility: {
    analyzerMode: "hierarchical",
    family: "orthogonal",
    id: "visibility",
    label: "Visibility Layout",
    ogdfClass: "VisibilityLayout",
    shortLabel: "Visibility",
    toolbar: false,
  },
};

export const OGDF_LAYOUT_TOOLBAR_DEFINITIONS = OGDF_LAYOUT_TOOLBAR_MODES.map(
  (layoutMode) => OGDF_LAYOUT_DEFINITIONS[layoutMode],
);

export function getOgdfLayoutDefinition(layoutMode: LayoutMode): OgdfLayoutDefinition {
  return OGDF_LAYOUT_DEFINITIONS[layoutMode];
}

export function normalizeLayoutMode(layoutMode: LayoutMode): LayoutMode {
  if (layoutMode === "clustered") {
    return "fast_multipole";
  }
  if (layoutMode === "hierarchical") {
    return "hierarchical_barycenter";
  }
  return layoutMode;
}

export function resolveAnalyzerLayoutMode(layoutMode: LayoutMode): AnalyzerLayoutMode {
  return getOgdfLayoutDefinition(layoutMode).analyzerMode;
}

export interface Point {
  x: number;
  y: number;
}

export interface Size {
  height: number;
  width: number;
}

export interface NodeLayout {
  clusterId?: string;
  modelId: ModelId;
  position: Point;
  size: Size;
}

export interface RoutedEdgePath {
  crossingIds: string[];
  edgeId: string;
  points: Point[];
}

export interface EdgeCrossing {
  edgeIds: [string, string];
  id: string;
  markerStyle: CrossingMarkerStyle;
  position: Point;
}

export interface LayoutEngineMetadata {
  actualAlgorithm?: string;
  actualMode?: LayoutMode;
  aspectRatio?: number;
  boundingBoxArea?: number;
  edgeCrossings?: number;
  edgeLengthStddev?: number;
  edgeNodeIntersections?: number;
  edgeSegmentOverlaps?: number;
  meanEdgeLength?: number;
  nodeOverlaps?: number;
  nodeSpacingOverlaps?: number;
  overlappingEdges?: number;
  requestedAlgorithm?: string;
  requestedMode?: LayoutMode;
  routeSegments?: number;
  strategy?: string;
  strategyReason?: string;
}

export interface LayoutSnapshot {
  crossings: EdgeCrossing[];
  engineMetadata?: LayoutEngineMetadata;
  mode: LayoutMode;
  nodes: NodeLayout[];
  routedEdges: RoutedEdgePath[];
}
