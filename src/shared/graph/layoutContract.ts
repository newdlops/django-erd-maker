import type { ModelId } from "../domain/modelIdentity";

export type CrossingMarkerStyle = "bridge" | "marker";
export const OGDF_LAYOUT_MODES = ["hierarchical", "circular", "clustered"] as const;
export type LayoutMode = (typeof OGDF_LAYOUT_MODES)[number];
export const DEFAULT_LAYOUT_MODE: LayoutMode = OGDF_LAYOUT_MODES[0];

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
