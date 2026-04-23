mod anchor;
mod crossing_detector;
mod orthogonal_router;

use crate::protocol::graph::{DiagramGraph, StructuralGraphEdge};
use crate::protocol::layout::{EdgeCrossing, NodeLayout, RoutedEdgePath};
use anchor::select_anchors;
use orthogonal_router::route_orthogonal;
use std::collections::BTreeMap;

pub(crate) use crossing_detector::detect_crossings;

const MAX_CROSSING_DETECTION_EDGES: usize = 500;

pub fn route_structural_edges(
    graph: &DiagramGraph,
    node_layouts: &[NodeLayout],
) -> (Vec<RoutedEdgePath>, Vec<EdgeCrossing>) {
    let nodes_by_id = node_layouts
        .iter()
        .map(|node| (node.model_id.as_str().to_string(), node))
        .collect::<BTreeMap<_, _>>();
    let lane_offsets = pair_lane_offsets(&graph.structural_edges);
    let mut routed_edges = Vec::new();

    for edge in &graph.structural_edges {
        let Some(source) = nodes_by_id.get(edge.source_model_id.as_str()) else {
            continue;
        };
        let Some(target) = nodes_by_id.get(edge.target_model_id.as_str()) else {
            continue;
        };

        let lane_offset = lane_offsets.get(&edge.id).copied().unwrap_or(0.0);
        let anchored = select_anchors(edge, source, target, lane_offset);
        routed_edges.push(route_orthogonal(&anchored));
    }

    let visible_edges = graph
        .structural_edges
        .iter()
        .filter(|edge| {
            nodes_by_id.contains_key(edge.source_model_id.as_str())
                && nodes_by_id.contains_key(edge.target_model_id.as_str())
        })
        .cloned()
        .collect::<Vec<_>>();
    let crossings = if visible_edges.len() <= MAX_CROSSING_DETECTION_EDGES {
        detect_crossings(&visible_edges, &mut routed_edges)
    } else {
        Vec::new()
    };

    routed_edges.sort_by(|left, right| left.edge_id.cmp(&right.edge_id));
    (routed_edges, crossings)
}

fn pair_lane_offsets(edges: &[StructuralGraphEdge]) -> BTreeMap<String, f64> {
    let mut by_pair = BTreeMap::<String, Vec<String>>::new();

    for edge in edges {
        by_pair
            .entry(pair_key(edge))
            .or_default()
            .push(edge.id.clone());
    }

    let mut offsets = BTreeMap::new();
    for edge_ids in by_pair.values_mut() {
        edge_ids.sort();
        let midpoint = (edge_ids.len() as f64 - 1.0) / 2.0;
        for (index, edge_id) in edge_ids.iter().enumerate() {
            offsets.insert(edge_id.clone(), (index as f64 - midpoint) * 14.0);
        }
    }

    offsets
}

fn pair_key(edge: &StructuralGraphEdge) -> String {
    if edge.source_model_id.as_str() <= edge.target_model_id.as_str() {
        format!(
            "{}|{}",
            edge.source_model_id.as_str(),
            edge.target_model_id.as_str()
        )
    } else {
        format!(
            "{}|{}",
            edge.target_model_id.as_str(),
            edge.source_model_id.as_str()
        )
    }
}

#[cfg(test)]
mod tests {
    use super::route_structural_edges;
    use crate::protocol::analysis::{MethodAssociationConfidence, RelationKind};
    use crate::protocol::diagnostics::AnalyzerDiagnostic;
    use crate::protocol::graph::{
        DiagramGraph, GraphNode, MethodAssociation, StructuralEdgeProvenance, StructuralGraphEdge,
    };
    use crate::protocol::layout::{LayoutMode, LayoutSnapshot, NodeLayout, Point, Size};
    use crate::protocol::model_identity::CanonicalModelId;
    use std::fs;
    use std::path::PathBuf;

    #[test]
    fn routes_feature_graph_edges_without_method_layers() {
        let graph = DiagramGraph {
            diagnostics: Vec::<AnalyzerDiagnostic>::new(),
            method_associations: vec![MethodAssociation {
                confidence: MethodAssociationConfidence::High,
                id: "assoc:dummy".to_string(),
                method_name: "publish".to_string(),
                provenance: "method_inference",
                source_model_id: CanonicalModelId::new("blog", "Post"),
                target_model_id: CanonicalModelId::new("accounts", "Author"),
            }],
            nodes: vec![
                GraphNode {
                    app_label: "accounts".to_string(),
                    model_id: CanonicalModelId::new("accounts", "Author"),
                    model_name: "Author".to_string(),
                },
                GraphNode {
                    app_label: "blog".to_string(),
                    model_id: CanonicalModelId::new("blog", "Post"),
                    model_name: "Post".to_string(),
                },
            ],
            structural_edges: vec![
                StructuralGraphEdge {
                    id: "edge:declared:blog.Post:author".to_string(),
                    kind: RelationKind::ForeignKey,
                    provenance: StructuralEdgeProvenance::Declared,
                    source_model_id: CanonicalModelId::new("blog", "Post"),
                    target_model_id: CanonicalModelId::new("accounts", "Author"),
                },
                StructuralGraphEdge {
                    id: "edge:reverse:blog.Post:author".to_string(),
                    kind: RelationKind::ReverseForeignKey,
                    provenance: StructuralEdgeProvenance::DerivedReverse,
                    source_model_id: CanonicalModelId::new("accounts", "Author"),
                    target_model_id: CanonicalModelId::new("blog", "Post"),
                },
            ],
        };
        let node_layouts = vec![
            NodeLayout {
                model_id: CanonicalModelId::new("accounts", "Author"),
                position: Point { x: 0.0, y: 0.0 },
                size: Size {
                    height: 120.0,
                    width: 220.0,
                },
            },
            NodeLayout {
                model_id: CanonicalModelId::new("blog", "Post"),
                position: Point { x: 420.0, y: 40.0 },
                size: Size {
                    height: 180.0,
                    width: 300.0,
                },
            },
        ];

        let (routed_edges, crossings) = route_structural_edges(&graph, &node_layouts);

        assert_eq!(routed_edges.len(), 2);
        assert!(crossings.is_empty());
        assert!(
            routed_edges
                .iter()
                .all(|path| path.edge_id.starts_with("edge:") && path.crossing_ids.is_empty())
        );
    }

    #[test]
    fn detects_crossings_for_fixed_node_positions() {
        let graph = DiagramGraph {
            diagnostics: Vec::<AnalyzerDiagnostic>::new(),
            method_associations: Vec::new(),
            nodes: vec![
                graph_node("mesh.Alpha"),
                graph_node("mesh.Beta"),
                graph_node("mesh.Gamma"),
                graph_node("mesh.Delta"),
            ],
            structural_edges: vec![
                structural_edge("edge:a", "mesh.Alpha", "mesh.Delta"),
                structural_edge("edge:b", "mesh.Beta", "mesh.Gamma"),
            ],
        };
        let node_layouts = vec![
            node_layout("mesh.Alpha", 0.0, 170.0, 220.0, 120.0),
            node_layout("mesh.Beta", 310.0, 0.0, 220.0, 120.0),
            node_layout("mesh.Gamma", 310.0, 340.0, 220.0, 120.0),
            node_layout("mesh.Delta", 620.0, 170.0, 220.0, 120.0),
        ];

        let (routed_edges, crossings) = route_structural_edges(&graph, &node_layouts);
        let snapshot = LayoutSnapshot {
            crossings,
            mode: LayoutMode::Hierarchical,
            nodes: node_layouts,
            routed_edges,
        };

        assert_eq!(
            snapshot.to_json().trim_end(),
            read_snapshot("phase7-fixed-crossing-routing.json").trim_end()
        );
    }

    fn graph_node(model_id: &str) -> GraphNode {
        let (app_label, model_name) = split_model_id(model_id);
        GraphNode {
            app_label: app_label.to_string(),
            model_id: CanonicalModelId::new(app_label, model_name),
            model_name: model_name.to_string(),
        }
    }

    fn node_layout(model_id: &str, x: f64, y: f64, width: f64, height: f64) -> NodeLayout {
        let (app_label, model_name) = split_model_id(model_id);
        NodeLayout {
            model_id: CanonicalModelId::new(app_label, model_name),
            position: Point { x, y },
            size: Size { height, width },
        }
    }

    fn read_snapshot(file_name: &str) -> String {
        fs::read_to_string(
            PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../test/fixtures/analyzer-json")
                .join(file_name),
        )
        .expect("expected routing snapshot fixture")
    }

    fn split_model_id(model_id: &str) -> (&str, &str) {
        model_id
            .split_once('.')
            .expect("expected canonical model id")
    }

    fn structural_edge(id: &str, source: &str, target: &str) -> StructuralGraphEdge {
        let (source_app, source_model) = split_model_id(source);
        let (target_app, target_model) = split_model_id(target);

        StructuralGraphEdge {
            id: id.to_string(),
            kind: RelationKind::ForeignKey,
            provenance: StructuralEdgeProvenance::Declared,
            source_model_id: CanonicalModelId::new(source_app, source_model),
            target_model_id: CanonicalModelId::new(target_app, target_model),
        }
    }
}
