use std::collections::BTreeMap;

const CROSSING_MOVE_GAIN: f64 = 0.55;
const DEFAULT_EDGE_DETOUR: f64 = 1.35;
const DEFAULT_NODE_SPACING: f64 = 1.4;
const MAX_EDGE_DETOUR: f64 = 2.5;
const MAX_NODE_MOVE: f64 = 180.0;
const MAX_NODE_SPACING: f64 = 2.4;
const MIN_EDGE_ORDER_GAP: f64 = 56.0;
const MIN_EDGE_DETOUR: f64 = 0.8;
const MIN_NODE_SPACING: f64 = 0.8;
const NODE_PADDING_X: f64 = 44.0;
const NODE_PADDING_Y: f64 = 36.0;
const OPTIMIZER_NODE_LIMIT: usize = 420;

#[derive(Debug, Clone, PartialEq)]
pub struct LayoutNodeInput {
    pub height: f64,
    pub id: String,
    pub width: f64,
    pub x: f64,
    pub y: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct LayoutEdgeInput {
    pub source_id: String,
    pub target_id: String,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct LayoutOptimizerSettings {
    pub edge_detour: f64,
    pub node_spacing: f64,
}

impl Default for LayoutOptimizerSettings {
    fn default() -> Self {
        Self {
            edge_detour: DEFAULT_EDGE_DETOUR,
            node_spacing: DEFAULT_NODE_SPACING,
        }
    }
}

impl LayoutOptimizerSettings {
    fn normalized(self) -> Self {
        Self {
            edge_detour: normalize_factor(
                self.edge_detour,
                DEFAULT_EDGE_DETOUR,
                MIN_EDGE_DETOUR,
                MAX_EDGE_DETOUR,
            ),
            node_spacing: normalize_factor(
                self.node_spacing,
                DEFAULT_NODE_SPACING,
                MIN_NODE_SPACING,
                MAX_NODE_SPACING,
            ),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct LayoutBounds {
    pub max_x: f64,
    pub max_y: f64,
    pub min_x: f64,
    pub min_y: f64,
    pub visible_count: usize,
}

#[derive(Debug, Clone, PartialEq)]
pub struct OptimizedNodePosition {
    pub id: String,
    pub x: f64,
    pub y: f64,
}

#[derive(Debug, Clone)]
struct TableGeometry {
    bottom: f64,
    center_x: f64,
    center_y: f64,
    left: f64,
    model_id: String,
    right: f64,
    top: f64,
}

#[derive(Debug, Clone)]
struct EdgeGeometry {
    end: Point,
    source_model_id: String,
    start: Point,
    target_model_id: String,
}

#[derive(Debug, Clone)]
struct MoveCommand {
    dx: f64,
    dy: f64,
    model_id: String,
}

#[derive(Debug, Clone, Copy)]
struct Point {
    x: f64,
    y: f64,
}

pub fn optimize_locations(
    nodes: &[LayoutNodeInput],
    edges: &[LayoutEdgeInput],
) -> Vec<OptimizedNodePosition> {
    optimize_locations_with_settings(nodes, edges, LayoutOptimizerSettings::default())
}

pub fn optimize_locations_with_settings(
    nodes: &[LayoutNodeInput],
    edges: &[LayoutEdgeInput],
    settings: LayoutOptimizerSettings,
) -> Vec<OptimizedNodePosition> {
    if nodes.len() <= 1 || nodes.len() > OPTIMIZER_NODE_LIMIT {
        return nodes.iter().map(to_optimized_position).collect();
    }

    let settings = settings.normalized();
    let tables = collect_table_geometry(nodes);
    let edges = collect_edge_geometry(edges, &tables);
    let mut commands = Vec::new();

    commands.extend(untangle_crossed_edges(&edges, &tables, settings));
    commands.extend(separate_overlapping_tables(&tables, settings));

    if commands.is_empty() {
        return nodes.iter().map(to_optimized_position).collect();
    }

    apply_move_commands(nodes, &commands, settings)
}

pub fn compute_bounds(nodes: &[LayoutNodeInput]) -> Option<LayoutBounds> {
    if nodes.is_empty() {
        return None;
    }

    let mut bounds = LayoutBounds {
        max_x: f64::NEG_INFINITY,
        max_y: f64::NEG_INFINITY,
        min_x: f64::INFINITY,
        min_y: f64::INFINITY,
        visible_count: 0,
    };

    for node in nodes {
        if !is_finite_node(node) {
            continue;
        }

        bounds.min_x = bounds.min_x.min(node.x);
        bounds.min_y = bounds.min_y.min(node.y);
        bounds.max_x = bounds.max_x.max(node.x + node.width);
        bounds.max_y = bounds.max_y.max(node.y + node.height);
        bounds.visible_count += 1;
    }

    if bounds.visible_count == 0
        || !bounds.min_x.is_finite()
        || !bounds.min_y.is_finite()
        || !bounds.max_x.is_finite()
        || !bounds.max_y.is_finite()
    {
        None
    } else {
        Some(bounds)
    }
}

fn to_optimized_position(node: &LayoutNodeInput) -> OptimizedNodePosition {
    OptimizedNodePosition {
        id: node.id.clone(),
        x: round2(node.x),
        y: round2(node.y),
    }
}

fn collect_table_geometry(nodes: &[LayoutNodeInput]) -> BTreeMap<String, TableGeometry> {
    nodes
        .iter()
        .filter(|node| is_finite_node(node))
        .map(|node| {
            let left = node.x;
            let top = node.y;
            let right = node.x + node.width;
            let bottom = node.y + node.height;
            let model_id = node.id.clone();

            (
                model_id.clone(),
                TableGeometry {
                    bottom,
                    center_x: (left + right) / 2.0,
                    center_y: (top + bottom) / 2.0,
                    left,
                    model_id,
                    right,
                    top,
                },
            )
        })
        .collect()
}

fn collect_edge_geometry(
    edges: &[LayoutEdgeInput],
    tables: &BTreeMap<String, TableGeometry>,
) -> Vec<EdgeGeometry> {
    edges
        .iter()
        .filter_map(|edge| {
            let source = tables.get(&edge.source_id)?;
            let target = tables.get(&edge.target_id)?;

            Some(EdgeGeometry {
                end: Point {
                    x: target.center_x,
                    y: target.center_y,
                },
                source_model_id: edge.source_id.clone(),
                start: Point {
                    x: source.center_x,
                    y: source.center_y,
                },
                target_model_id: edge.target_id.clone(),
            })
        })
        .collect()
}

fn untangle_crossed_edges(
    edges: &[EdgeGeometry],
    tables: &BTreeMap<String, TableGeometry>,
    settings: LayoutOptimizerSettings,
) -> Vec<MoveCommand> {
    let mut commands = Vec::new();

    for left_index in 0..edges.len() {
        for right_index in (left_index + 1)..edges.len() {
            let left = &edges[left_index];
            let right = &edges[right_index];

            if share_endpoint(left, right)
                || !segments_cross(&left.start, &left.end, &right.start, &right.end)
            {
                continue;
            }

            if is_horizontally_dominant(left) || is_horizontally_dominant(right) {
                commands.extend(untangle_horizontal_pair(left, right, tables, settings));
            } else {
                commands.extend(untangle_vertical_pair(left, right, tables, settings));
            }
        }
    }

    commands
}

fn untangle_horizontal_pair(
    left: &EdgeGeometry,
    right: &EdgeGeometry,
    tables: &BTreeMap<String, TableGeometry>,
    settings: LayoutOptimizerSettings,
) -> Vec<MoveCommand> {
    let Some((left_anchor, left_movable)) = horizontal_endpoints(left, tables) else {
        return Vec::new();
    };
    let Some((right_anchor, right_movable)) = horizontal_endpoints(right, tables) else {
        return Vec::new();
    };
    let desired_order = left_anchor.center_y.total_cmp(&right_anchor.center_y);
    let actual_delta = left_movable.center_y - right_movable.center_y;
    let min_gap = edge_order_gap(settings).max(
        (left_movable.bottom - left_movable.top + right_movable.bottom - right_movable.top) / 4.0,
    );

    if desired_order.is_lt() && actual_delta < -min_gap {
        return Vec::new();
    }
    if desired_order.is_gt() && actual_delta > min_gap {
        return Vec::new();
    }
    if desired_order.is_eq() {
        return Vec::new();
    }

    let correction = if desired_order.is_lt() {
        (actual_delta + min_gap).max(0.0)
    } else {
        (min_gap - actual_delta).max(0.0)
    } * crossing_move_gain(settings);

    if correction <= 0.0 {
        return Vec::new();
    }

    if desired_order.is_lt() {
        vec![
            move_y(&left_movable.model_id, -correction / 2.0),
            move_y(&right_movable.model_id, correction / 2.0),
        ]
    } else {
        vec![
            move_y(&left_movable.model_id, correction / 2.0),
            move_y(&right_movable.model_id, -correction / 2.0),
        ]
    }
}

fn untangle_vertical_pair(
    left: &EdgeGeometry,
    right: &EdgeGeometry,
    tables: &BTreeMap<String, TableGeometry>,
    settings: LayoutOptimizerSettings,
) -> Vec<MoveCommand> {
    let Some((left_anchor, left_movable)) = vertical_endpoints(left, tables) else {
        return Vec::new();
    };
    let Some((right_anchor, right_movable)) = vertical_endpoints(right, tables) else {
        return Vec::new();
    };
    let desired_order = left_anchor.center_x.total_cmp(&right_anchor.center_x);
    let actual_delta = left_movable.center_x - right_movable.center_x;
    let min_gap = edge_order_gap(settings).max(
        (left_movable.right - left_movable.left + right_movable.right - right_movable.left) / 4.0,
    );

    if desired_order.is_lt() && actual_delta < -min_gap {
        return Vec::new();
    }
    if desired_order.is_gt() && actual_delta > min_gap {
        return Vec::new();
    }
    if desired_order.is_eq() {
        return Vec::new();
    }

    let correction = if desired_order.is_lt() {
        (actual_delta + min_gap).max(0.0)
    } else {
        (min_gap - actual_delta).max(0.0)
    } * crossing_move_gain(settings);

    if correction <= 0.0 {
        return Vec::new();
    }

    if desired_order.is_lt() {
        vec![
            move_x(&left_movable.model_id, -correction / 2.0),
            move_x(&right_movable.model_id, correction / 2.0),
        ]
    } else {
        vec![
            move_x(&left_movable.model_id, correction / 2.0),
            move_x(&right_movable.model_id, -correction / 2.0),
        ]
    }
}

fn horizontal_endpoints<'a>(
    edge: &EdgeGeometry,
    tables: &'a BTreeMap<String, TableGeometry>,
) -> Option<(&'a TableGeometry, &'a TableGeometry)> {
    let source = tables.get(&edge.source_model_id)?;
    let target = tables.get(&edge.target_model_id)?;

    if source.center_x <= target.center_x {
        Some((source, target))
    } else {
        Some((target, source))
    }
}

fn vertical_endpoints<'a>(
    edge: &EdgeGeometry,
    tables: &'a BTreeMap<String, TableGeometry>,
) -> Option<(&'a TableGeometry, &'a TableGeometry)> {
    let source = tables.get(&edge.source_model_id)?;
    let target = tables.get(&edge.target_model_id)?;

    if source.center_y <= target.center_y {
        Some((source, target))
    } else {
        Some((target, source))
    }
}

fn separate_overlapping_tables(
    tables: &BTreeMap<String, TableGeometry>,
    settings: LayoutOptimizerSettings,
) -> Vec<MoveCommand> {
    let ordered = tables.values().collect::<Vec<_>>();
    let mut commands = Vec::new();
    let node_padding_x = node_padding_x(settings);
    let node_padding_y = node_padding_y(settings);

    for left_index in 0..ordered.len() {
        for right_index in (left_index + 1)..ordered.len() {
            let left = ordered[left_index];
            let right = ordered[right_index];
            let overlap_x = (left.right + node_padding_x).min(right.right + node_padding_x)
                - (left.left - node_padding_x).max(right.left - node_padding_x);
            let overlap_y = (left.bottom + node_padding_y).min(right.bottom + node_padding_y)
                - (left.top - node_padding_y).max(right.top - node_padding_y);

            if overlap_x <= 0.0 || overlap_y <= 0.0 {
                continue;
            }

            if overlap_x <= overlap_y {
                let direction = if left.center_x <= right.center_x {
                    1.0
                } else {
                    -1.0
                };
                let delta = (overlap_x / 2.0 + 1.0).min(max_node_move(settings) / 2.0);
                commands.push(move_x(&left.model_id, -direction * delta));
                commands.push(move_x(&right.model_id, direction * delta));
            } else {
                let direction = if left.center_y <= right.center_y {
                    1.0
                } else {
                    -1.0
                };
                let delta = (overlap_y / 2.0 + 1.0).min(max_node_move(settings) / 2.0);
                commands.push(move_y(&left.model_id, -direction * delta));
                commands.push(move_y(&right.model_id, direction * delta));
            }
        }
    }

    commands
}

fn apply_move_commands(
    nodes: &[LayoutNodeInput],
    commands: &[MoveCommand],
    settings: LayoutOptimizerSettings,
) -> Vec<OptimizedNodePosition> {
    let mut movement_by_id = BTreeMap::<String, (f64, f64)>::new();
    let max_node_move = max_node_move(settings);

    for command in commands {
        let movement = movement_by_id
            .entry(command.model_id.clone())
            .or_insert((0.0, 0.0));
        movement.0 += command.dx;
        movement.1 += command.dy;
    }

    nodes
        .iter()
        .map(|node| {
            let (dx, dy) = movement_by_id
                .get(node.id.as_str())
                .copied()
                .unwrap_or((0.0, 0.0));

            OptimizedNodePosition {
                id: node.id.clone(),
                x: round2((node.x + dx.clamp(-max_node_move, max_node_move)).max(0.0)),
                y: round2((node.y + dy.clamp(-max_node_move, max_node_move)).max(0.0)),
            }
        })
        .collect()
}

fn normalize_factor(value: f64, fallback: f64, min: f64, max: f64) -> f64 {
    if value.is_finite() {
        value.clamp(min, max)
    } else {
        fallback
    }
}

fn node_spacing_ratio(settings: LayoutOptimizerSettings) -> f64 {
    settings.node_spacing / DEFAULT_NODE_SPACING
}

fn edge_detour_ratio(settings: LayoutOptimizerSettings) -> f64 {
    settings.edge_detour / DEFAULT_EDGE_DETOUR
}

fn node_padding_x(settings: LayoutOptimizerSettings) -> f64 {
    NODE_PADDING_X * node_spacing_ratio(settings)
}

fn node_padding_y(settings: LayoutOptimizerSettings) -> f64 {
    NODE_PADDING_Y * node_spacing_ratio(settings)
}

fn edge_order_gap(settings: LayoutOptimizerSettings) -> f64 {
    let weighted_ratio = node_spacing_ratio(settings) * 0.65 + edge_detour_ratio(settings) * 0.35;

    MIN_EDGE_ORDER_GAP * weighted_ratio
}

fn crossing_move_gain(settings: LayoutOptimizerSettings) -> f64 {
    (CROSSING_MOVE_GAIN * (0.9 + edge_detour_ratio(settings) * 0.1)).clamp(0.42, 0.78)
}

fn max_node_move(settings: LayoutOptimizerSettings) -> f64 {
    MAX_NODE_MOVE * (0.75 + node_spacing_ratio(settings) * 0.25)
}

fn share_endpoint(left: &EdgeGeometry, right: &EdgeGeometry) -> bool {
    left.source_model_id == right.source_model_id
        || left.source_model_id == right.target_model_id
        || left.target_model_id == right.source_model_id
        || left.target_model_id == right.target_model_id
}

fn is_horizontally_dominant(edge: &EdgeGeometry) -> bool {
    (edge.start.x - edge.end.x).abs() >= (edge.start.y - edge.end.y).abs()
}

fn segments_cross(
    left_start: &Point,
    left_end: &Point,
    right_start: &Point,
    right_end: &Point,
) -> bool {
    let d1 = direction(left_start, left_end, right_start);
    let d2 = direction(left_start, left_end, right_end);
    let d3 = direction(right_start, right_end, left_start);
    let d4 = direction(right_start, right_end, left_end);

    ((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0))
        && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))
}

fn direction(start: &Point, end: &Point, point: &Point) -> f64 {
    ((point.x - start.x) * (end.y - start.y)) - ((point.y - start.y) * (end.x - start.x))
}

fn move_x(model_id: &str, dx: f64) -> MoveCommand {
    MoveCommand {
        dx,
        dy: 0.0,
        model_id: model_id.to_string(),
    }
}

fn move_y(model_id: &str, dy: f64) -> MoveCommand {
    MoveCommand {
        dx: 0.0,
        dy,
        model_id: model_id.to_string(),
    }
}

fn is_finite_node(node: &LayoutNodeInput) -> bool {
    node.x.is_finite()
        && node.y.is_finite()
        && node.width.is_finite()
        && node.height.is_finite()
        && node.width >= 0.0
        && node.height >= 0.0
}

fn round2(value: f64) -> f64 {
    (value * 100.0).round() / 100.0
}

#[cfg(test)]
mod tests {
    use super::{
        LayoutEdgeInput, LayoutNodeInput, LayoutOptimizerSettings, compute_bounds,
        optimize_locations, optimize_locations_with_settings,
    };

    #[test]
    fn creates_minimal_moves_for_crossed_edge_endpoints() {
        let nodes = vec![
            node("mesh.Alpha", 0.0, 180.0),
            node("mesh.Beta", 0.0, 0.0),
            node("mesh.Gamma", 420.0, 180.0),
            node("mesh.Delta", 420.0, 0.0),
        ];
        let edges = vec![
            edge("mesh.Alpha", "mesh.Delta"),
            edge("mesh.Beta", "mesh.Gamma"),
        ];

        let optimized = optimize_locations(&nodes, &edges);

        assert!(y_of(&optimized, "mesh.Gamma") < 180.0);
        assert!(y_of(&optimized, "mesh.Delta") > 0.0);
        assert_eq!(x_of(&optimized, "mesh.Alpha"), 0.0);
        assert_eq!(x_of(&optimized, "mesh.Beta"), 0.0);
    }

    #[test]
    fn separates_overlapping_tables_with_bounded_moves() {
        let nodes = vec![node("mesh.Alpha", 0.0, 0.0), node("mesh.Beta", 40.0, 30.0)];

        let optimized = optimize_locations(&nodes, &[]);

        assert!(x_of(&optimized, "mesh.Beta") > 40.0 || y_of(&optimized, "mesh.Beta") > 30.0);
        assert!(
            !strictly_overlaps(
                rect_of(&optimized, "mesh.Alpha"),
                rect_of(&optimized, "mesh.Beta")
            ),
            "tables should no longer overlap after the bounded move",
        );
    }

    #[test]
    fn node_spacing_setting_increases_overlap_clearance() {
        let nodes = vec![node("mesh.Alpha", 0.0, 0.0), node("mesh.Beta", 40.0, 30.0)];

        let compact = optimize_locations_with_settings(
            &nodes,
            &[],
            LayoutOptimizerSettings {
                edge_detour: 1.35,
                node_spacing: 0.8,
            },
        );
        let expanded = optimize_locations_with_settings(
            &nodes,
            &[],
            LayoutOptimizerSettings {
                edge_detour: 1.35,
                node_spacing: 2.4,
            },
        );

        assert!(
            y_of(&expanded, "mesh.Beta") > y_of(&compact, "mesh.Beta") + 30.0,
            "higher node spacing should produce a larger minimal separation move",
        );
    }

    #[test]
    fn edge_detour_setting_increases_crossing_order_clearance() {
        let nodes = vec![
            node("mesh.Alpha", 0.0, 180.0),
            node("mesh.Beta", 0.0, 0.0),
            node("mesh.Gamma", 420.0, 180.0),
            node("mesh.Delta", 420.0, 0.0),
        ];
        let edges = vec![
            edge("mesh.Alpha", "mesh.Delta"),
            edge("mesh.Beta", "mesh.Gamma"),
        ];

        let compact = optimize_locations_with_settings(
            &nodes,
            &edges,
            LayoutOptimizerSettings {
                edge_detour: 0.8,
                node_spacing: 1.4,
            },
        );
        let detoured = optimize_locations_with_settings(
            &nodes,
            &edges,
            LayoutOptimizerSettings {
                edge_detour: 2.5,
                node_spacing: 1.4,
            },
        );

        assert!(
            y_of(&detoured, "mesh.Delta") > y_of(&compact, "mesh.Delta") + 4.0,
            "higher edge detour should give crossed endpoints a wider order-preserving move",
        );
    }

    #[test]
    fn computes_visible_node_bounds() {
        let bounds = compute_bounds(&[
            node("mesh.Alpha", 10.0, 20.0),
            node("mesh.Beta", 240.0, 80.0),
        ])
        .expect("expected bounds");

        assert_eq!(bounds.min_x, 10.0);
        assert_eq!(bounds.min_y, 20.0);
        assert_eq!(bounds.max_x, 420.0);
        assert_eq!(bounds.max_y, 180.0);
        assert_eq!(bounds.visible_count, 2);
    }

    fn node(id: &str, x: f64, y: f64) -> LayoutNodeInput {
        LayoutNodeInput {
            height: 100.0,
            id: id.to_string(),
            width: 180.0,
            x,
            y,
        }
    }

    fn edge(source_id: &str, target_id: &str) -> LayoutEdgeInput {
        LayoutEdgeInput {
            source_id: source_id.to_string(),
            target_id: target_id.to_string(),
        }
    }

    fn x_of(nodes: &[super::OptimizedNodePosition], model_id: &str) -> f64 {
        nodes
            .iter()
            .find(|node| node.id == model_id)
            .expect("expected node")
            .x
    }

    fn y_of(nodes: &[super::OptimizedNodePosition], model_id: &str) -> f64 {
        nodes
            .iter()
            .find(|node| node.id == model_id)
            .expect("expected node")
            .y
    }

    fn rect_of(nodes: &[super::OptimizedNodePosition], model_id: &str) -> (f64, f64, f64, f64) {
        let x = x_of(nodes, model_id);
        let y = y_of(nodes, model_id);
        (x, y, x + 180.0, y + 100.0)
    }

    fn strictly_overlaps(left: (f64, f64, f64, f64), right: (f64, f64, f64, f64)) -> bool {
        left.0 < right.2 && left.2 > right.0 && left.1 < right.3 && left.3 > right.1
    }
}
