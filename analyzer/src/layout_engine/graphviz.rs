use super::{LayoutContext, LayoutSettings, round2};
use crate::protocol::graph::{DiagramGraph, StructuralEdgeProvenance, StructuralGraphEdge};
use crate::protocol::layout::{LayoutMode, LayoutSnapshot, NodeLayout, Point, RoutedEdgePath};
use crate::protocol::model_identity::CanonicalModelId;
use crate::routing::detect_crossings;
use std::collections::{BTreeMap, BTreeSet};
use std::io::Write;
use std::process::{Command, Stdio};

const PIXELS_PER_INCH: f64 = 72.0;

pub fn try_compute_layout(
    graph: &DiagramGraph,
    context: &LayoutContext,
    mode: LayoutMode,
    settings: &LayoutSettings,
) -> Result<LayoutSnapshot, String> {
    if context.nodes.is_empty() {
        return Ok(LayoutSnapshot::empty(mode));
    }

    let dot_source = build_dot_source(graph, context, mode, settings);
    let output = run_graphviz(dot_engine(mode), &dot_source)?;
    parse_plain_layout(&output, graph, context, mode)
}

fn build_dot_source(
    graph: &DiagramGraph,
    context: &LayoutContext,
    mode: LayoutMode,
    settings: &LayoutSettings,
) -> String {
    let node_specs = build_node_specs(context);
    let visible_model_ids = node_specs
        .iter()
        .map(|node| node.model_id.as_str().to_string())
        .collect::<BTreeSet<_>>();
    let visible_edges = visible_structural_edges(graph, &visible_model_ids);
    let mut lines = vec!["digraph DjangoErd {".to_string()];

    for line in graph_attribute_lines(mode, settings) {
        lines.push(format!("  {line}"));
    }

    lines.push(r#"  node [fixedsize="true", margin="0", shape="box"];"#.to_string());
    lines.push(r#"  edge [arrowsize="0.8"];"#.to_string());

    if matches!(mode, LayoutMode::Clustered) {
        let mut node_ids_by_app = BTreeMap::<String, Vec<String>>::new();
        for node in &node_specs {
            node_ids_by_app
                .entry(node.app_label.clone())
                .or_default()
                .push(node.safe_id.clone());
        }

        for (app_label, node_ids) in node_ids_by_app {
            lines.push(format!(
                r#"  subgraph "cluster_{}" {{"#,
                sanitize_identifier(&app_label)
            ));
            lines.push(format!(r#"    label="{}";"#, escape_dot_string(&app_label)));
            lines.push("    color=\"#35516e\";".to_string());
            lines.push(r#"    penwidth="1.2";"#.to_string());
            lines.push(r#"    style="rounded";"#.to_string());
            for node_id in node_ids {
                lines.push(format!(r#"    "{node_id}";"#));
            }
            lines.push("  }".to_string());
        }
    }

    for node in &node_specs {
        lines.push(format!(
            r#"  "{}" [height="{:.4}", label="{}", width="{:.4}"];"#,
            node.safe_id, node.height_in, node.safe_id, node.width_in,
        ));
    }

    for edge in visible_edges {
        let Some(source_node) = node_specs
            .iter()
            .find(|node| node.model_id.as_str() == edge.source_model_id.as_str())
        else {
            continue;
        };
        let Some(target_node) = node_specs
            .iter()
            .find(|node| node.model_id.as_str() == edge.target_model_id.as_str())
        else {
            continue;
        };

        let mut attributes = Vec::new();
        if edge.provenance == StructuralEdgeProvenance::DerivedReverse {
            attributes.push(r#"constraint="false""#.to_string());
            attributes.push(r#"weight="0.35""#.to_string());
        } else {
            attributes.push(r#"weight="1.0""#.to_string());
        }

        lines.push(format!(
            r#"  "{}" -> "{}" [{}];"#,
            source_node.safe_id,
            target_node.safe_id,
            attributes.join(", "),
        ));
    }

    lines.push("}".to_string());
    lines.join("\n")
}

fn graph_attribute_lines(mode: LayoutMode, settings: &LayoutSettings) -> Vec<String> {
    let node_gap_in = px_to_inches(64.0 * settings.node_spacing.max(0.6));
    let rank_gap_in = px_to_inches(108.0 * settings.node_spacing.max(0.6));
    let edge_sep_px = round2(18.0 * settings.edge_detour.max(0.6));
    let sep_px = round2(28.0 * settings.node_spacing.max(0.6));
    let spline_mode = if uses_orthogonal_splines(mode) {
        "ortho"
    } else {
        "polyline"
    };
    let overlap_mode = if matches!(
        mode,
        LayoutMode::Circular | LayoutMode::Graph | LayoutMode::Neural | LayoutMode::Radial
    ) {
        "prism"
    } else {
        "false"
    };
    let rankdir = match mode {
        LayoutMode::Flow => Some("LR"),
        LayoutMode::Hierarchical | LayoutMode::Clustered => Some("TB"),
        _ => None,
    };
    let mut lines = vec![
        format!(
            r#"graph [layout="{}", outputorder="edgesfirst", overlap="{overlap_mode}", sep="+{sep_px}", splines="{spline_mode}"];"#,
            graph_layout_name(mode)
        ),
        format!(
            r#"graph [esep="+{edge_sep_px}", nodesep="{node_gap_in:.4}", ranksep="{rank_gap_in:.4}"];"#
        ),
    ];

    if let Some(rankdir) = rankdir {
        lines.push(format!(r#"graph [rankdir="{rankdir}"];"#));
    }

    if matches!(mode, LayoutMode::Graph | LayoutMode::Neural) {
        lines.push(format!(
            r#"graph [K="{:.4}"];"#,
            round2(0.9 * settings.node_spacing.max(0.6))
        ));
    }

    lines
}

fn run_graphviz(engine: &str, dot_source: &str) -> Result<String, String> {
    let mut command = Command::new(graphviz_dot_binary());
    command
        .arg(format!("-K{engine}"))
        .arg("-Tplain")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    let mut child = command
        .spawn()
        .map_err(|error| format!("failed to start Graphviz: {error}"))?;
    child
        .stdin
        .as_mut()
        .ok_or_else(|| "failed to open Graphviz stdin".to_string())?
        .write_all(dot_source.as_bytes())
        .map_err(|error| format!("failed to write Graphviz input: {error}"))?;
    let output = child
        .wait_with_output()
        .map_err(|error| format!("failed to read Graphviz output: {error}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        return Err(if stderr.is_empty() {
            format!("Graphviz exited with status {}", output.status)
        } else {
            format!("Graphviz failed: {stderr}")
        });
    }

    String::from_utf8(output.stdout)
        .map_err(|error| format!("Graphviz output was not UTF-8: {error}"))
}

fn parse_plain_layout(
    plain_output: &str,
    graph: &DiagramGraph,
    context: &LayoutContext,
    mode: LayoutMode,
) -> Result<LayoutSnapshot, String> {
    let node_specs = build_node_specs(context);
    let node_spec_by_safe_id = node_specs
        .iter()
        .map(|node| (node.safe_id.clone(), node))
        .collect::<BTreeMap<_, _>>();
    let visible_model_ids = node_specs
        .iter()
        .map(|node| node.model_id.as_str().to_string())
        .collect::<BTreeSet<_>>();
    let visible_edges = visible_structural_edges(graph, &visible_model_ids);
    let mut graph_height_in = None::<f64>;
    let mut node_layouts_by_model_id = BTreeMap::<String, NodeLayout>::new();
    let mut routed_edges = Vec::<RoutedEdgePath>::new();
    let mut edge_index = 0usize;

    for line in plain_output.lines() {
        let tokens = tokenize_plain_line(line);
        if tokens.is_empty() {
            continue;
        }

        match tokens[0].as_str() {
            "graph" => {
                graph_height_in = Some(parse_plain_number(tokens.get(3), line, "graph height")?);
            }
            "node" => {
                let graph_height = graph_height_in
                    .ok_or_else(|| "Graphviz plain output was missing graph header".to_string())?;
                let safe_id = tokens
                    .get(1)
                    .ok_or_else(|| format!("node line is missing id: {line}"))?;
                let Some(node_spec) = node_spec_by_safe_id.get(safe_id.as_str()) else {
                    continue;
                };
                let center_x = parse_plain_number(tokens.get(2), line, "node x")?;
                let center_y = parse_plain_number(tokens.get(3), line, "node y")?;

                node_layouts_by_model_id.insert(
                    node_spec.model_id.as_str().to_string(),
                    NodeLayout {
                        model_id: node_spec.model_id.clone(),
                        position: Point {
                            x: round2((center_x - node_spec.width_in / 2.0) * PIXELS_PER_INCH),
                            y: round2(
                                (graph_height - center_y - node_spec.height_in / 2.0)
                                    * PIXELS_PER_INCH,
                            ),
                        },
                        size: node_spec.size.clone(),
                    },
                );
            }
            "edge" => {
                let graph_height = graph_height_in
                    .ok_or_else(|| "Graphviz plain output was missing graph header".to_string())?;
                let point_count = parse_plain_usize(tokens.get(3), line, "edge point count")?;
                let start_index = 4usize;
                let end_index = start_index + point_count * 2;
                if tokens.len() < end_index {
                    return Err(format!("edge line has too few spline coordinates: {line}"));
                }
                let Some(edge) = visible_edges.get(edge_index) else {
                    return Err(format!("unexpected Graphviz edge output order: {line}"));
                };
                let mut points = Vec::with_capacity(point_count);
                let mut point_index = start_index;
                while point_index < end_index {
                    let x = parse_plain_number(tokens.get(point_index), line, "edge point x")?;
                    let y = parse_plain_number(tokens.get(point_index + 1), line, "edge point y")?;
                    points.push(Point {
                        x: round2(x * PIXELS_PER_INCH),
                        y: round2((graph_height - y) * PIXELS_PER_INCH),
                    });
                    point_index += 2;
                }

                routed_edges.push(RoutedEdgePath {
                    crossing_ids: Vec::new(),
                    edge_id: edge.id.clone(),
                    points,
                });
                edge_index += 1;
            }
            "stop" => break,
            _ => {}
        }
    }

    let nodes = node_specs
        .iter()
        .map(|node| {
            node_layouts_by_model_id
                .remove(node.model_id.as_str())
                .ok_or_else(|| {
                    format!(
                        "Graphviz output did not include node {}",
                        node.model_id.as_str()
                    )
                })
        })
        .collect::<Result<Vec<_>, _>>()?;

    let crossings = if uses_orthogonal_splines(mode) {
        detect_crossings(&visible_edges, &mut routed_edges)
    } else {
        Vec::new()
    };

    Ok(LayoutSnapshot {
        crossings,
        mode,
        nodes,
        routed_edges,
    })
}

fn visible_structural_edges(
    graph: &DiagramGraph,
    visible_model_ids: &BTreeSet<String>,
) -> Vec<StructuralGraphEdge> {
    graph
        .structural_edges
        .iter()
        .filter(|edge| {
            visible_model_ids.contains(edge.source_model_id.as_str())
                && visible_model_ids.contains(edge.target_model_id.as_str())
        })
        .cloned()
        .collect()
}

fn build_node_specs(context: &LayoutContext) -> Vec<GraphvizNodeSpec> {
    context
        .nodes
        .iter()
        .enumerate()
        .map(|(index, node)| GraphvizNodeSpec {
            app_label: node.app_label.clone(),
            height_in: px_to_inches(node.size.height),
            model_id: node.model_id.clone(),
            safe_id: format!("n{index}"),
            size: node.size.clone(),
            width_in: px_to_inches(node.size.width),
        })
        .collect()
}

fn dot_engine(mode: LayoutMode) -> &'static str {
    match mode {
        LayoutMode::Circular => "circo",
        LayoutMode::Clustered | LayoutMode::Flow | LayoutMode::Hierarchical => "dot",
        LayoutMode::Graph => "fdp",
        LayoutMode::Neural => "neato",
        LayoutMode::Radial => "twopi",
    }
}

fn graph_layout_name(mode: LayoutMode) -> &'static str {
    dot_engine(mode)
}

fn uses_orthogonal_splines(mode: LayoutMode) -> bool {
    matches!(
        mode,
        LayoutMode::Clustered | LayoutMode::Flow | LayoutMode::Hierarchical
    )
}

fn graphviz_dot_binary() -> String {
    std::env::var("DJANGO_ERD_GRAPHVIZ_DOT").unwrap_or_else(|_| "dot".to_string())
}

fn px_to_inches(value: f64) -> f64 {
    round2(value.max(1.0) / PIXELS_PER_INCH)
}

fn parse_plain_number(value: Option<&String>, line: &str, label: &str) -> Result<f64, String> {
    value
        .ok_or_else(|| format!("{label} missing in Graphviz line: {line}"))?
        .parse::<f64>()
        .map_err(|error| format!("failed to parse {label} from Graphviz line '{line}': {error}"))
}

fn parse_plain_usize(value: Option<&String>, line: &str, label: &str) -> Result<usize, String> {
    value
        .ok_or_else(|| format!("{label} missing in Graphviz line: {line}"))?
        .parse::<usize>()
        .map_err(|error| format!("failed to parse {label} from Graphviz line '{line}': {error}"))
}

fn tokenize_plain_line(line: &str) -> Vec<String> {
    line.split_whitespace()
        .map(|part| part.to_string())
        .collect()
}

fn escape_dot_string(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}

fn sanitize_identifier(value: &str) -> String {
    value
        .chars()
        .map(|character| {
            if character.is_ascii_alphanumeric() {
                character
            } else {
                '_'
            }
        })
        .collect()
}

#[derive(Clone)]
struct GraphvizNodeSpec {
    app_label: String,
    height_in: f64,
    model_id: CanonicalModelId,
    safe_id: String,
    size: crate::protocol::layout::Size,
    width_in: f64,
}

#[cfg(test)]
mod tests {
    use super::{build_dot_source, parse_plain_layout, uses_orthogonal_splines};
    use crate::layout_engine::{LayoutContext, LayoutSettings};
    use crate::protocol::analysis::RelationKind;
    use crate::protocol::diagnostics::AnalyzerDiagnostic;
    use crate::protocol::graph::{
        DiagramGraph, GraphNode, StructuralEdgeProvenance, StructuralGraphEdge,
    };
    use crate::protocol::layout::{LayoutMode, Size};
    use crate::protocol::model_identity::CanonicalModelId;

    #[test]
    fn builds_cluster_subgraphs_for_clustered_layout() {
        let graph = sample_graph();
        let source = build_dot_source(
            &graph,
            &sample_context(),
            LayoutMode::Clustered,
            &LayoutSettings::default(),
        );

        assert!(source.contains(r#"subgraph "cluster_accounts""#));
        assert!(source.contains(r#"subgraph "cluster_blog""#));
        assert!(source.contains(r#"layout="dot""#));
    }

    #[test]
    fn parses_plain_graphviz_output_into_layout_snapshot() {
        let graph = sample_graph();
        let snapshot = parse_plain_layout(
            "graph 1 12 8\nnode n0 2 6 2.5 1.1 n0 solid box black lightgrey\nnode n1 8 2 2.5 1.1 n1 solid box black lightgrey\nedge n0 n1 4 3.25 6 4.5 6 5.5 2 6.75 2 solid black\nstop\n",
            &graph,
            &sample_context(),
            LayoutMode::Hierarchical,
        )
        .expect("expected graphviz plain output to parse");

        assert_eq!(snapshot.nodes.len(), 2);
        assert_eq!(snapshot.nodes[0].model_id.as_str(), "accounts.Author");
        assert_eq!(snapshot.routed_edges.len(), 1);
        assert_eq!(snapshot.routed_edges[0].edge_id, "edge:blog.Post:author");
        assert!(snapshot.crossings.is_empty());
        assert!(uses_orthogonal_splines(LayoutMode::Hierarchical));
    }

    fn sample_context() -> LayoutContext {
        LayoutContext {
            components: vec![vec!["accounts.Author".to_string(), "blog.Post".to_string()]],
            declared_edges: vec![("blog.Post".to_string(), "accounts.Author".to_string())],
            nodes: vec![
                crate::layout_engine::measurement::MeasuredNode {
                    app_label: "accounts".to_string(),
                    model_id: CanonicalModelId::new("accounts", "Author"),
                    model_name: "Author".to_string(),
                    size: Size {
                        height: 79.2,
                        width: 180.0,
                    },
                },
                crate::layout_engine::measurement::MeasuredNode {
                    app_label: "blog".to_string(),
                    model_id: CanonicalModelId::new("blog", "Post"),
                    model_name: "Post".to_string(),
                    size: Size {
                        height: 79.2,
                        width: 180.0,
                    },
                },
            ],
        }
    }

    fn sample_graph() -> DiagramGraph {
        DiagramGraph {
            diagnostics: Vec::<AnalyzerDiagnostic>::new(),
            method_associations: Vec::new(),
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
            structural_edges: vec![StructuralGraphEdge {
                id: "edge:blog.Post:author".to_string(),
                kind: RelationKind::ForeignKey,
                provenance: StructuralEdgeProvenance::Declared,
                source_model_id: CanonicalModelId::new("blog", "Post"),
                target_model_id: CanonicalModelId::new("accounts", "Author"),
            }],
        }
    }
}
