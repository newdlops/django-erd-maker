import type { ModelId } from "../domain/modelIdentity";

export type CrossingMarkerStyle = "bridge" | "marker";
export const ANALYZER_LAYOUT_MODES = ["hierarchical", "circular", "clustered"] as const;
export type AnalyzerLayoutMode = (typeof ANALYZER_LAYOUT_MODES)[number];

export const OGDF_LAYOUT_TOOLBAR_MODES = [
  "planarization",
  "planarization_grid",
  "uml_planarization",
  "hierarchical_barycenter",
  "upward_layer_based",
  "circular",
  "fast_multipole",
] as const;
export type ToolbarLayoutMode = (typeof OGDF_LAYOUT_TOOLBAR_MODES)[number];

export const OGDF_LAYOUT_MODES = [
  ...OGDF_LAYOUT_TOOLBAR_MODES,
  "clustered",
  "hierarchical",
] as const;
export type LayoutMode = (typeof OGDF_LAYOUT_MODES)[number];
export const DEFAULT_LAYOUT_MODE: LayoutMode = "planarization";

export const EDGE_ROUTING_STYLES = ["orthogonal", "straight"] as const;
export type EdgeRoutingStyle = (typeof EDGE_ROUTING_STYLES)[number];
export const DEFAULT_EDGE_ROUTING: EdgeRoutingStyle = "orthogonal";

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
    label: "Clustered Layout (legacy alias for Fast Multipole Embedder)",
    ogdfClass: "FastMultipoleEmbedder",
    shortLabel: "Clustered",
    toolbar: false,
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
  fast_multipole: {
    analyzerMode: "clustered",
    family: "energy",
    id: "fast_multipole",
    label: "Fast Multipole Embedder",
    ogdfClass: "FastMultipoleEmbedder",
    shortLabel: "Fast MP",
    toolbar: true,
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
};

export const OGDF_LAYOUT_TOOLBAR_DEFINITIONS = OGDF_LAYOUT_TOOLBAR_MODES.map(
  (layoutMode) => OGDF_LAYOUT_DEFINITIONS[layoutMode],
);

export function getOgdfLayoutDefinition(layoutMode: LayoutMode): OgdfLayoutDefinition {
  return OGDF_LAYOUT_DEFINITIONS[layoutMode];
}

export function normalizeLayoutMode(layoutMode: LayoutMode): ToolbarLayoutMode {
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
