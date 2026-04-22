use crate::protocol::model_identity::CanonicalModelId;
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CrossingMarkerStyle {
    Bridge,
    Marker,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum LayoutMode {
    Circular,
    Clustered,
    Hierarchical,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Size {
    pub height: f64,
    pub width: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NodeLayout {
    pub model_id: CanonicalModelId,
    pub position: Point,
    pub size: Size,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RoutedEdgePath {
    pub crossing_ids: Vec<String>,
    pub edge_id: String,
    pub points: Vec<Point>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EdgeCrossing {
    pub edge_ids: [String; 2],
    pub id: String,
    pub marker_style: CrossingMarkerStyle,
    pub position: Point,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LayoutSnapshot {
    pub crossings: Vec<EdgeCrossing>,
    pub mode: LayoutMode,
    pub nodes: Vec<NodeLayout>,
    pub routed_edges: Vec<RoutedEdgePath>,
}

impl LayoutSnapshot {
    pub fn empty(mode: LayoutMode) -> Self {
        Self {
            crossings: Vec::new(),
            mode,
            nodes: Vec::new(),
            routed_edges: Vec::new(),
        }
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).expect("layout snapshot should serialize to JSON")
    }
}
