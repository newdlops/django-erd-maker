mod circular;
mod clustered;
mod hierarchical;
mod measurement;

use crate::protocol::analysis::AnalyzerOutput;
use crate::protocol::graph::{DiagramGraph, StructuralEdgeProvenance};
use crate::protocol::layout::{LayoutMode, LayoutSnapshot, NodeLayout, Point};
use crate::protocol::model_identity::CanonicalModelId;
use crate::routing::route_structural_edges;
use measurement::{MeasuredNode, measure_visible_nodes};
use std::collections::{BTreeMap, BTreeSet};

const LAYER_SPACING_X: f64 = 120.0;
const ROW_SPACING_Y: f64 = 48.0;

#[derive(Debug, Clone, PartialEq)]
pub struct ManualNodePosition {
    pub model_id: CanonicalModelId,
    pub position: Point,
}

pub struct LayoutRequest<'a> {
    pub analyzer: &'a AnalyzerOutput,
    pub graph: &'a DiagramGraph,
    pub hidden_model_ids: Vec<CanonicalModelId>,
    pub manual_positions: Vec<ManualNodePosition>,
    pub mode: LayoutMode,
}

pub(crate) struct LayoutContext {
    pub components: Vec<Vec<String>>,
    pub declared_edges: Vec<(String, String)>,
    pub nodes: Vec<MeasuredNode>,
}

trait LayoutStrategy {
    fn compute(&self, context: &LayoutContext) -> Vec<NodeLayout>;
}

struct CircularStrategy;
struct ClusteredStrategy;
struct HierarchicalStrategy;

pub fn compute_layout(request: LayoutRequest<'_>) -> LayoutSnapshot {
    let hidden_model_ids = request
        .hidden_model_ids
        .iter()
        .map(|model_id| model_id.as_str().to_string())
        .collect::<BTreeSet<_>>();
    let nodes = measure_visible_nodes(request.analyzer, request.graph, &hidden_model_ids);
    let declared_edges = declared_edges(request.graph, &hidden_model_ids);
    let components = connected_components(&nodes, &declared_edges);
    let context = LayoutContext {
        components,
        declared_edges,
        nodes,
    };

    let mut snapshot = LayoutSnapshot::empty(request.mode);
    snapshot.nodes = select_strategy(request.mode).compute(&context);

    apply_manual_positions(&mut snapshot.nodes, &request.manual_positions);
    snapshot
        .nodes
        .sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));
    let (routed_edges, crossings) = route_structural_edges(request.graph, &snapshot.nodes);
    snapshot.routed_edges = routed_edges;
    snapshot.crossings = crossings;
    snapshot
}

pub(crate) fn component_edges(
    context: &LayoutContext,
    component: &[String],
) -> Vec<(String, String)> {
    let component_set = component.iter().cloned().collect::<BTreeSet<_>>();
    let mut edges = context
        .declared_edges
        .iter()
        .filter(|(source, target)| component_set.contains(source) && component_set.contains(target))
        .cloned()
        .collect::<Vec<_>>();
    edges.sort();
    edges
}

pub(crate) fn component_nodes<'a>(
    context: &'a LayoutContext,
    component: &[String],
) -> Vec<&'a MeasuredNode> {
    let component_set = component.iter().cloned().collect::<BTreeSet<_>>();
    context
        .nodes
        .iter()
        .filter(|node| component_set.contains(node.model_id.as_str()))
        .collect()
}

pub(crate) fn layer_spacing_x() -> f64 {
    LAYER_SPACING_X
}

pub(crate) fn row_spacing_y() -> f64 {
    ROW_SPACING_Y
}

pub(crate) fn round2(value: f64) -> f64 {
    (value * 100.0).round() / 100.0
}

fn apply_manual_positions(nodes: &mut [NodeLayout], manual_positions: &[ManualNodePosition]) {
    let positions = manual_positions
        .iter()
        .map(|manual| {
            (
                manual.model_id.as_str().to_string(),
                manual.position.clone(),
            )
        })
        .collect::<BTreeMap<_, _>>();

    for node in nodes {
        if let Some(position) = positions.get(node.model_id.as_str()) {
            node.position = position.clone();
        }
    }
}

fn select_strategy(mode: LayoutMode) -> &'static dyn LayoutStrategy {
    match mode {
        LayoutMode::Circular => &CircularStrategy,
        LayoutMode::Clustered => &ClusteredStrategy,
        LayoutMode::Hierarchical => &HierarchicalStrategy,
    }
}

fn connected_components(
    nodes: &[MeasuredNode],
    declared_edges: &[(String, String)],
) -> Vec<Vec<String>> {
    let adjacency = undirected_adjacency(nodes, declared_edges);
    let mut components = Vec::new();
    let mut visited = BTreeSet::new();

    for node in nodes {
        if !visited.insert(node.model_id.as_str().to_string()) {
            continue;
        }

        let mut stack = vec![node.model_id.as_str().to_string()];
        let mut component = Vec::new();
        while let Some(current) = stack.pop() {
            component.push(current.clone());
            if let Some(neighbors) = adjacency.get(&current) {
                for neighbor in neighbors.iter().rev() {
                    if visited.insert(neighbor.clone()) {
                        stack.push(neighbor.clone());
                    }
                }
            }
        }
        component.sort();
        components.push(component);
    }

    components.sort();
    components
}

fn declared_edges(
    graph: &DiagramGraph,
    hidden_model_ids: &BTreeSet<String>,
) -> Vec<(String, String)> {
    let mut edges = graph
        .structural_edges
        .iter()
        .filter(|edge| edge.provenance == StructuralEdgeProvenance::Declared)
        .filter(|edge| {
            !hidden_model_ids.contains(edge.source_model_id.as_str())
                && !hidden_model_ids.contains(edge.target_model_id.as_str())
        })
        .map(|edge| {
            (
                edge.source_model_id.as_str().to_string(),
                edge.target_model_id.as_str().to_string(),
            )
        })
        .collect::<Vec<_>>();
    edges.sort();
    edges
}

fn undirected_adjacency(
    nodes: &[MeasuredNode],
    declared_edges: &[(String, String)],
) -> BTreeMap<String, Vec<String>> {
    let mut adjacency = nodes
        .iter()
        .map(|node| (node.model_id.as_str().to_string(), Vec::<String>::new()))
        .collect::<BTreeMap<_, _>>();

    for (source, target) in declared_edges {
        adjacency
            .entry(source.clone())
            .or_default()
            .push(target.clone());
        adjacency
            .entry(target.clone())
            .or_default()
            .push(source.clone());
    }

    for neighbors in adjacency.values_mut() {
        neighbors.sort();
        neighbors.dedup();
    }

    adjacency
}

impl LayoutStrategy for CircularStrategy {
    fn compute(&self, context: &LayoutContext) -> Vec<NodeLayout> {
        circular::compute(context)
    }
}

impl LayoutStrategy for ClusteredStrategy {
    fn compute(&self, context: &LayoutContext) -> Vec<NodeLayout> {
        clustered::compute(context)
    }
}

impl LayoutStrategy for HierarchicalStrategy {
    fn compute(&self, context: &LayoutContext) -> Vec<NodeLayout> {
        hierarchical::compute(context)
    }
}

#[cfg(test)]
mod tests {
    use super::{LayoutRequest, ManualNodePosition, compute_layout};
    use crate::extract::{AnalysisRequest, ModuleInput, analyze_request};
    use crate::protocol::layout::{LayoutMode, Point};
    use crate::resolve::build_diagram_graph;
    use std::fs;
    use std::path::PathBuf;

    #[test]
    fn produces_feature_rich_layout_snapshots_for_all_modes() {
        let analyzer = feature_rich_analyzer();
        let graph = build_diagram_graph(&analyzer);

        for (mode, snapshot_name) in [
            (
                LayoutMode::Hierarchical,
                "phase6-feature-rich-hierarchical-layout.json",
            ),
            (
                LayoutMode::Circular,
                "phase6-feature-rich-circular-layout.json",
            ),
            (
                LayoutMode::Clustered,
                "phase6-feature-rich-clustered-layout.json",
            ),
        ] {
            let snapshot = compute_layout(LayoutRequest {
                analyzer: &analyzer,
                graph: &graph,
                hidden_model_ids: Vec::new(),
                manual_positions: Vec::new(),
                mode,
            });

            assert_eq!(
                snapshot.to_json().trim_end(),
                read_snapshot(snapshot_name).trim_end()
            );
        }
    }

    #[test]
    fn recomputes_after_hide_and_manual_move() {
        let analyzer = feature_rich_analyzer();
        let graph = build_diagram_graph(&analyzer);
        let snapshot = compute_layout(LayoutRequest {
            analyzer: &analyzer,
            graph: &graph,
            hidden_model_ids: vec![crate::protocol::model_identity::CanonicalModelId::new(
                "taxonomy", "Tag",
            )],
            manual_positions: vec![ManualNodePosition {
                model_id: crate::protocol::model_identity::CanonicalModelId::new("blog", "Post"),
                position: Point { x: 700.0, y: 240.0 },
            }],
            mode: LayoutMode::Hierarchical,
        });

        assert_eq!(snapshot.nodes.len(), 2);
        let post = snapshot
            .nodes
            .iter()
            .find(|node| node.model_id.as_str() == "blog.Post")
            .expect("expected manually positioned post node");
        assert_eq!(post.position.x, 700.0);
        assert_eq!(post.position.y, 240.0);
    }

    fn feature_rich_analyzer() -> crate::protocol::analysis::AnalyzerOutput {
        analyze_request(&AnalysisRequest {
            modules: vec![
                fixture_module("accounts", "feature_rich_project/accounts/models.py"),
                fixture_module("blog", "feature_rich_project/blog/models.py"),
                fixture_module("taxonomy", "feature_rich_project/taxonomy/models.py"),
            ],
            workspace_root: fixture_root("feature_rich_project"),
        })
    }

    fn fixture_module(app_label: &str, relative_path: &str) -> ModuleInput {
        ModuleInput {
            app_label: app_label.to_string(),
            file_path: PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../test/fixtures/django")
                .join(relative_path),
        }
    }

    fn fixture_root(project_name: &str) -> PathBuf {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../test/fixtures/django")
            .join(project_name)
    }

    fn read_snapshot(file_name: &str) -> String {
        fs::read_to_string(
            PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../test/fixtures/analyzer-json")
                .join(file_name),
        )
        .expect("expected layout snapshot fixture")
    }
}
