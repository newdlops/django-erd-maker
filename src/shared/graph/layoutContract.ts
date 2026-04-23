import type { ModelId } from "../domain/modelIdentity";

export type CrossingMarkerStyle = "bridge" | "marker";
export const ANALYZER_LAYOUT_MODES = ["hierarchical", "circular", "clustered"] as const;
export type AnalyzerLayoutMode = (typeof ANALYZER_LAYOUT_MODES)[number];

export const OGDF_LAYOUT_TOOLBAR_MODES = [
  "hierarchical",
  "hierarchical_barycenter",
  "hierarchical_sifting",
  "circular",
  "linear",
  "fmmm",
  "fast_multipole",
  "fast_multipole_multilevel",
  "stress_minimization",
  "pivot_mds",
  "davidson_harel",
  "planarization",
  "planarization_grid",
  "tree",
  "radial_tree",
] as const;
export type ToolbarLayoutMode = (typeof OGDF_LAYOUT_TOOLBAR_MODES)[number];

export const OGDF_LAYOUT_MODES = [...OGDF_LAYOUT_TOOLBAR_MODES, "clustered"] as const;
export type LayoutMode = (typeof OGDF_LAYOUT_MODES)[number];
export const DEFAULT_LAYOUT_MODE: LayoutMode = OGDF_LAYOUT_MODES[0];

export interface OgdfLayoutDefinition {
  analyzerMode: AnalyzerLayoutMode;
  family: "energy" | "layered" | "legacy" | "linear" | "planar" | "tree";
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
  hierarchical_sifting: {
    analyzerMode: "hierarchical",
    family: "layered",
    id: "hierarchical_sifting",
    label: "Sugiyama Layout (Sifting)",
    ogdfClass: "SugiyamaLayout + SiftingHeuristic",
    shortLabel: "Sifting",
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

export interface LayoutSnapshot {
  crossings: EdgeCrossing[];
  mode: LayoutMode;
  nodes: NodeLayout[];
  routedEdges: RoutedEdgePath[];
}
