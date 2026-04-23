mod circular;
mod clustered;
mod graphviz;
mod hierarchical;
mod location_optimizer;
mod measurement;

use crate::protocol::analysis::AnalyzerOutput;
use crate::protocol::graph::{DiagramGraph, StructuralEdgeProvenance};
use crate::protocol::layout::{LayoutMode, LayoutSnapshot, NodeLayout, Point};
use crate::protocol::model_identity::CanonicalModelId;
use crate::routing::route_structural_edges;
use location_optimizer::optimize_locations;
use measurement::{MeasuredNode, measure_visible_nodes};
use std::collections::{BTreeMap, BTreeSet};

const LAYER_SPACING_X: f64 = 120.0;
const LARGE_GRAPH_GRID_NODE_THRESHOLD: usize = 500;
const ROW_SPACING_Y: f64 = 48.0;
const GRID_GAP_X: f64 = 48.0;
const GRID_GAP_Y: f64 = 28.0;
const GRID_MARGIN: f64 = 24.0;

#[derive(Debug, Clone, PartialEq)]
pub struct ManualNodePosition {
    pub model_id: CanonicalModelId,
    pub position: Point,
}

#[derive(Debug, Clone, PartialEq)]
pub struct LayoutSettings {
    pub edge_detour: f64,
    pub node_spacing: f64,
}

impl Default for LayoutSettings {
    fn default() -> Self {
        Self {
            edge_detour: 1.35,
            node_spacing: 1.4,
        }
    }
}

pub struct LayoutRequest<'a> {
    pub analyzer: &'a AnalyzerOutput,
    pub graph: &'a DiagramGraph,
    pub hidden_model_ids: Vec<CanonicalModelId>,
    pub manual_positions: Vec<ManualNodePosition>,
    pub mode: LayoutMode,
    pub settings: LayoutSettings,
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

    if let Ok(mut snapshot) =
        graphviz::try_compute_layout(request.graph, &context, request.mode, &request.settings)
    {
        apply_manual_positions(&mut snapshot.nodes, &request.manual_positions);
        snapshot
            .nodes
            .sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));

        if !request.manual_positions.is_empty() {
            let (routed_edges, crossings) = route_structural_edges(request.graph, &snapshot.nodes);
            snapshot.routed_edges = routed_edges;
            snapshot.crossings = crossings;
        }

        return snapshot;
    }

    let mut snapshot = LayoutSnapshot::empty(request.mode);
    snapshot.nodes = if context.nodes.len() > LARGE_GRAPH_GRID_NODE_THRESHOLD {
        compute_grid_layout(&context.nodes)
    } else {
        select_strategy(request.mode).compute(&context)
    };

    snapshot.nodes = optimize_locations(request.graph, &snapshot.nodes);
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

fn compute_grid_layout(nodes: &[MeasuredNode]) -> Vec<NodeLayout> {
    let ordered = {
        let mut ordered = nodes.iter().collect::<Vec<_>>();
        ordered.sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));
        ordered
    };
    let max_width = ordered
        .iter()
        .map(|node| node.size.width)
        .fold(0.0_f64, f64::max);
    let max_height = ordered
        .iter()
        .map(|node| node.size.height)
        .fold(0.0_f64, f64::max);
    let columns = compute_grid_column_count(ordered.len(), max_width, max_height);

    ordered
        .iter()
        .enumerate()
        .map(|(index, node)| {
            let column = index % columns;
            let row = index / columns;

            NodeLayout {
                model_id: node.model_id.clone(),
                position: Point {
                    x: round2(GRID_MARGIN + column as f64 * (max_width + GRID_GAP_X)),
                    y: round2(GRID_MARGIN + row as f64 * (max_height + GRID_GAP_Y)),
                },
                size: node.size.clone(),
            }
        })
        .collect()
}

fn compute_grid_column_count(count: usize, max_width: f64, max_height: f64) -> usize {
    if count <= 1 {
        return 1;
    }

    let cell_width = (max_width + GRID_GAP_X).max(1.0);
    let cell_height = (max_height + GRID_GAP_Y).max(1.0);
    (((count as f64 * cell_height) / cell_width).sqrt())
        .ceil()
        .max(1.0) as usize
}

fn select_strategy(mode: LayoutMode) -> &'static dyn LayoutStrategy {
    match mode {
        LayoutMode::Circular => &CircularStrategy,
        LayoutMode::Clustered => &ClusteredStrategy,
        LayoutMode::Flow => &HierarchicalStrategy,
        LayoutMode::Graph => &ClusteredStrategy,
        LayoutMode::Hierarchical => &HierarchicalStrategy,
        LayoutMode::Neural => &HierarchicalStrategy,
        LayoutMode::Radial => &CircularStrategy,
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
    use super::{
        LayoutRequest, LayoutSettings, ManualNodePosition, compute_grid_column_count,
        compute_layout,
    };
    use crate::extract::{AnalysisRequest, ModuleInput, analyze_request};
    use crate::protocol::layout::{LayoutMode, LayoutSnapshot, Point};
    use crate::resolve::build_diagram_graph;
    use std::path::PathBuf;

    #[test]
    fn produces_feature_rich_layout_snapshots_for_all_modes() {
        let analyzer = feature_rich_analyzer();
        let graph = build_diagram_graph(&analyzer);

        for mode in [
            LayoutMode::Hierarchical,
            LayoutMode::Circular,
            LayoutMode::Clustered,
        ] {
            let snapshot = compute_layout(LayoutRequest {
                analyzer: &analyzer,
                graph: &graph,
                hidden_model_ids: Vec::new(),
                manual_positions: Vec::new(),
                mode,
                settings: LayoutSettings::default(),
            });

            assert_feature_rich_layout_shape(&snapshot, mode);
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
            settings: LayoutSettings::default(),
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

    #[test]
    fn balances_large_grid_layout_by_node_aspect_ratio() {
        assert_eq!(compute_grid_column_count(1_238, 372.0, 80.0), 18);
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

    fn assert_feature_rich_layout_shape(snapshot: &LayoutSnapshot, expected_mode: LayoutMode) {
        assert_eq!(snapshot.mode, expected_mode);
        assert_eq!(snapshot.nodes.len(), 3);
        assert_eq!(snapshot.routed_edges.len(), 4);
        assert!(snapshot.crossings.is_empty());

        let mut model_ids = snapshot
            .nodes
            .iter()
            .map(|node| node.model_id.as_str().to_string())
            .collect::<Vec<_>>();
        model_ids.sort();
        assert_eq!(
            model_ids,
            vec![
                "accounts.Author".to_string(),
                "blog.Post".to_string(),
                "taxonomy.Tag".to_string(),
            ]
        );

        for node in &snapshot.nodes {
            assert!(node.position.x.is_finite());
            assert!(node.position.y.is_finite());
            assert!(node.size.width > 0.0);
            assert!(node.size.height > 0.0);
        }

        for edge in &snapshot.routed_edges {
            assert!(edge.points.len() >= 2);
            assert!(edge.points.iter().all(|point| point.x.is_finite() && point.y.is_finite()));
        }
    }
}
