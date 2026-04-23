use crate::protocol::graph::{DiagramGraph, StructuralEdgeProvenance};
use crate::protocol::layout::{NodeLayout, Point};
use django_erd_layout_core::{LayoutEdgeInput, LayoutNodeInput};
use std::collections::BTreeMap;

pub fn optimize_locations(graph: &DiagramGraph, nodes: &[NodeLayout]) -> Vec<NodeLayout> {
    let node_inputs = nodes
        .iter()
        .map(|node| LayoutNodeInput {
            height: node.size.height,
            id: node.model_id.as_str().to_string(),
            width: node.size.width,
            x: node.position.x,
            y: node.position.y,
        })
        .collect::<Vec<_>>();
    let edge_inputs = graph
        .structural_edges
        .iter()
        .filter(|edge| edge.provenance == StructuralEdgeProvenance::Declared)
        .map(|edge| LayoutEdgeInput {
            source_id: edge.source_model_id.as_str().to_string(),
            target_id: edge.target_model_id.as_str().to_string(),
        })
        .collect::<Vec<_>>();
    let optimized_by_id = django_erd_layout_core::optimize_locations(&node_inputs, &edge_inputs)
        .into_iter()
        .map(|position| (position.id.clone(), position))
        .collect::<BTreeMap<_, _>>();

    nodes
        .iter()
        .map(|node| {
            let Some(position) = optimized_by_id.get(node.model_id.as_str()) else {
                return node.clone();
            };
            let mut next = node.clone();
            next.position = Point {
                x: position.x,
                y: position.y,
            };
            next
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::optimize_locations;
    use crate::protocol::analysis::RelationKind;
    use crate::protocol::diagnostics::AnalyzerDiagnostic;
    use crate::protocol::graph::{
        DiagramGraph, GraphNode, StructuralEdgeProvenance, StructuralGraphEdge,
    };
    use crate::protocol::layout::{NodeLayout, Point, Size};
    use crate::protocol::model_identity::CanonicalModelId;

    #[test]
    fn creates_minimal_moves_for_crossed_edge_endpoints() {
        let graph = graph_with_edges(vec![
            structural_edge("edge:a", "mesh.Alpha", "mesh.Delta"),
            structural_edge("edge:b", "mesh.Beta", "mesh.Gamma"),
        ]);
        let nodes = vec![
            node_layout("mesh.Alpha", 0.0, 180.0),
            node_layout("mesh.Beta", 0.0, 0.0),
            node_layout("mesh.Gamma", 420.0, 180.0),
            node_layout("mesh.Delta", 420.0, 0.0),
        ];

        let optimized = optimize_locations(&graph, &nodes);
        let gamma_y = y_of(&optimized, "mesh.Gamma");
        let delta_y = y_of(&optimized, "mesh.Delta");

        assert!(
            gamma_y < 180.0,
            "Gamma should move upward to follow Beta's order"
        );
        assert!(
            delta_y > 0.0,
            "Delta should move downward to follow Alpha's order"
        );
        assert_eq!(x_of(&optimized, "mesh.Alpha"), 0.0);
        assert_eq!(x_of(&optimized, "mesh.Beta"), 0.0);
    }

    #[test]
    fn separates_overlapping_tables_with_bounded_moves() {
        let graph = graph_with_edges(Vec::new());
        let nodes = vec![
            node_layout("mesh.Alpha", 0.0, 0.0),
            node_layout("mesh.Beta", 40.0, 30.0),
        ];

        let optimized = optimize_locations(&graph, &nodes);

        assert!(x_of(&optimized, "mesh.Beta") > 40.0 || y_of(&optimized, "mesh.Beta") > 30.0);
        assert!(
            !strictly_overlaps(
                rect_of(&optimized, "mesh.Alpha"),
                rect_of(&optimized, "mesh.Beta")
            ),
            "tables should no longer overlap after the bounded move",
        );
    }

    fn graph_with_edges(edges: Vec<StructuralGraphEdge>) -> DiagramGraph {
        DiagramGraph {
            diagnostics: Vec::<AnalyzerDiagnostic>::new(),
            method_associations: Vec::new(),
            nodes: ["mesh.Alpha", "mesh.Beta", "mesh.Gamma", "mesh.Delta"]
                .into_iter()
                .map(graph_node)
                .collect(),
            structural_edges: edges,
        }
    }

    fn graph_node(model_id: &str) -> GraphNode {
        let (app_label, model_name) = split_model_id(model_id);
        GraphNode {
            app_label: app_label.to_string(),
            model_id: CanonicalModelId::new(app_label, model_name),
            model_name: model_name.to_string(),
        }
    }

    fn node_layout(model_id: &str, x: f64, y: f64) -> NodeLayout {
        let (app_label, model_name) = split_model_id(model_id);
        NodeLayout {
            model_id: CanonicalModelId::new(app_label, model_name),
            position: Point { x, y },
            size: Size {
                height: 100.0,
                width: 180.0,
            },
        }
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

    fn split_model_id(model_id: &str) -> (&str, &str) {
        model_id
            .split_once('.')
            .expect("expected canonical model id")
    }

    fn x_of(nodes: &[NodeLayout], model_id: &str) -> f64 {
        nodes
            .iter()
            .find(|node| node.model_id.as_str() == model_id)
            .expect("expected node")
            .position
            .x
    }

    fn y_of(nodes: &[NodeLayout], model_id: &str) -> f64 {
        nodes
            .iter()
            .find(|node| node.model_id.as_str() == model_id)
            .expect("expected node")
            .position
            .y
    }

    fn rect_of(nodes: &[NodeLayout], model_id: &str) -> (f64, f64, f64, f64) {
        let node = nodes
            .iter()
            .find(|node| node.model_id.as_str() == model_id)
            .expect("expected node");
        (
            node.position.x,
            node.position.y,
            node.position.x + node.size.width,
            node.position.y + node.size.height,
        )
    }

    fn strictly_overlaps(left: (f64, f64, f64, f64), right: (f64, f64, f64, f64)) -> bool {
        left.0 < right.2 && left.2 > right.0 && left.1 < right.3 && left.3 > right.1
    }
}
