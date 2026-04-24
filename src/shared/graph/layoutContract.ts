import type { ModelId } from "../domain/modelIdentity";

export type CrossingMarkerStyle = "bridge" | "marker";
export const ANALYZER_LAYOUT_MODES = ["hierarchical", "circular", "clustered"] as const;
export type AnalyzerLayoutMode = (typeof ANALYZER_LAYOUT_MODES)[number];

export const OGDF_LAYOUT_TOOLBAR_MODES = [
  "hierarchical",
  "hierarchical_barycenter",
  "hierarchical_sifting",
  "hierarchical_global_sifting",
  "hierarchical_greedy_insert",
  "hierarchical_greedy_switch",
  "hierarchical_grid_sifting",
  "hierarchical_split",
  "circular",
  "linear",
  "constrained_force",
  "constrained_force_straight",
  "fmmm",
  "fast_multipole",
  "fast_multipole_multilevel",
  "stress_minimization",
  "pivot_mds",
  "davidson_harel",
  "planarization",
  "planarization_grid",
  "ortho",
  "planar_draw",
  "planar_straight",
  "schnyder",
  "upward_layer_based",
  "upward_planarization",
  "visibility",
  "cluster_planarization",
  "cluster_ortho",
  "uml_ortho",
  "uml_planarization",
  "tree",
  "radial_tree",
] as const;
export type ToolbarLayoutMode = (typeof OGDF_LAYOUT_TOOLBAR_MODES)[number];

export const OGDF_LAYOUT_MODES = [...OGDF_LAYOUT_TOOLBAR_MODES, "clustered"] as const;
export type LayoutMode = (typeof OGDF_LAYOUT_MODES)[number];
export const DEFAULT_LAYOUT_MODE: LayoutMode = "constrained_force";

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
  clustered: {
    analyzerMode: "clustered",
    family: "legacy",
    id: "clustered",
    label: "Clustered Layout (legacy alias for FM3)",
    ogdfClass: "FMMMLayout",
    shortLabel: "Clustered",
    toolbar: false,
  },
  cluster_ortho: {
    analyzerMode: "clustered",
    family: "cluster",
    id: "cluster_ortho",
    label: "Cluster Orthogonal Layout",
    ogdfClass: "ClusterPlanarizationLayout + ClusterOrthoLayout",
    shortLabel: "Cluster Ortho",
    toolbar: true,
  },
  cluster_planarization: {
    analyzerMode: "clustered",
    family: "cluster",
    id: "cluster_planarization",
    label: "Cluster Planarization Layout",
    ogdfClass: "ClusterPlanarizationLayout",
    shortLabel: "Cluster Planar",
    toolbar: true,
  },
  constrained_force: {
    analyzerMode: "clustered",
    family: "energy",
    id: "constrained_force",
    label: "Constrained Force Layout",
    ogdfClass: "ConstrainedForceDirectedLayout",
    shortLabel: "Force+",
    toolbar: true,
  },
  constrained_force_straight: {
    analyzerMode: "clustered",
    family: "energy",
    id: "constrained_force_straight",
    label: "Constrained Force Layout (Straight Edges)",
    ogdfClass: "ConstrainedForceDirectedLayout + StraightLineRouter",
    shortLabel: "Force Line",
    toolbar: true,
  },
  davidson_harel: {
    analyzerMode: "clustered",
    family: "energy",
    id: "davidson_harel",
    label: "Davidson-Harel Layout",
    ogdfClass: "DavidsonHarelLayout",
    shortLabel: "Davidson",
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
    shortLabel: "FM Multi",
    toolbar: true,
  },
  fmmm: {
    analyzerMode: "clustered",
    family: "energy",
    id: "fmmm",
    label: "FMMM Layout (FM3)",
    ogdfClass: "FMMMLayout",
    shortLabel: "FM3",
    toolbar: true,
  },
  hierarchical: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical",
    label: "Sugiyama Layout (Median)",
    ogdfClass: "SugiyamaLayout + MedianHeuristic",
    shortLabel: "Layered",
    toolbar: true,
  },
  hierarchical_barycenter: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_barycenter",
    label: "Sugiyama Layout (Barycenter)",
    ogdfClass: "SugiyamaLayout + BarycenterHeuristic",
    shortLabel: "Bary",
    toolbar: true,
  },
  hierarchical_global_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_global_sifting",
    label: "Sugiyama Layout (Global Sifting)",
    ogdfClass: "SugiyamaLayout + GlobalSifting",
    shortLabel: "Global Sift",
    toolbar: true,
  },
  hierarchical_greedy_insert: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_greedy_insert",
    label: "Sugiyama Layout (Greedy Insert)",
    ogdfClass: "SugiyamaLayout + GreedyInsertHeuristic",
    shortLabel: "Greedy Ins",
    toolbar: true,
  },
  hierarchical_greedy_switch: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_greedy_switch",
    label: "Sugiyama Layout (Greedy Switch)",
    ogdfClass: "SugiyamaLayout + GreedySwitchHeuristic",
    shortLabel: "Greedy Sw",
    toolbar: true,
  },
  hierarchical_grid_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_grid_sifting",
    label: "Sugiyama Layout (Grid Sifting)",
    ogdfClass: "SugiyamaLayout + GridSifting",
    shortLabel: "Grid Sift",
    toolbar: true,
  },
  hierarchical_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_sifting",
    label: "Sugiyama Layout (Sifting)",
    ogdfClass: "SugiyamaLayout + SiftingHeuristic",
    shortLabel: "Sifting",
    toolbar: true,
  },
  hierarchical_split: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_split",
    label: "Sugiyama Layout (Split)",
    ogdfClass: "SugiyamaLayout + SplitHeuristic",
    shortLabel: "Split",
    toolbar: true,
  },
  linear: {
    analyzerMode: "hierarchical",
    family: "linear",
    id: "linear",
    label: "Linear Layout",
    ogdfClass: "LinearLayout",
    shortLabel: "Linear",
    toolbar: true,
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
    label: "Pivot MDS",
    ogdfClass: "PivotMDS",
    shortLabel: "Pivot MDS",
    toolbar: true,
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
  planar_draw: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "planar_draw",
    label: "Planar Draw Layout",
    ogdfClass: "PlanarDrawLayout",
    shortLabel: "Planar Draw",
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
  planar_straight: {
    analyzerMode: "hierarchical",
    family: "planar",
    id: "planar_straight",
    label: "Planar Straight Layout",
    ogdfClass: "PlanarStraightLayout",
    shortLabel: "Straight",
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
    toolbar: true,
  },
  stress_minimization: {
    analyzerMode: "clustered",
    family: "energy",
    id: "stress_minimization",
    label: "Stress Minimization",
    ogdfClass: "StressMinimization",
    shortLabel: "Stress",
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
    toolbar: true,
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
    shortLabel: "Layer UPR",
    toolbar: true,
  },
  upward_planarization: {
    analyzerMode: "hierarchical",
    family: "upward",
    id: "upward_planarization",
    label: "Upward Planarization Layout",
    ogdfClass: "UpwardPlanarizationLayout",
    shortLabel: "Upward",
    toolbar: true,
  },
  visibility: {
    analyzerMode: "hierarchical",
    family: "upward",
    id: "visibility",
    label: "Visibility Layout",
    ogdfClass: "VisibilityLayout",
    shortLabel: "Visibility",
    toolbar: true,
  },
};

export const OGDF_LAYOUT_TOOLBAR_DEFINITIONS = OGDF_LAYOUT_TOOLBAR_MODES.map(
  (layoutMode) => OGDF_LAYOUT_DEFINITIONS[layoutMode],
);

export function getOgdfLayoutDefinition(layoutMode: LayoutMode): OgdfLayoutDefinition {
  return OGDF_LAYOUT_DEFINITIONS[layoutMode];
}

export function normalizeLayoutMode(layoutMode: LayoutMode): ToolbarLayoutMode {
  return layoutMode === "clustered" ? "fmmm" : layoutMode;
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
  edgeCrossings?: number;
  edgeNodeIntersections?: number;
  edgeSegmentOverlaps?: number;
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
