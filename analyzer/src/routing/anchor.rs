use crate::protocol::graph::StructuralGraphEdge;
use crate::protocol::layout::{NodeLayout, Point};

const PORT_OFFSET: f64 = 28.0;
const SIDE_MARGIN: f64 = 18.0;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteAxis {
    Horizontal,
    Vertical,
}

#[derive(Debug, Clone, PartialEq)]
pub struct AnchoredRoute {
    pub axis: RouteAxis,
    pub edge_id: String,
    pub end: Point,
    pub end_stub: Point,
    pub start: Point,
    pub start_stub: Point,
}

pub fn select_anchors(
    edge: &StructuralGraphEdge,
    source: &NodeLayout,
    target: &NodeLayout,
    lane_offset: f64,
) -> AnchoredRoute {
    let source_center = center(source);
    let target_center = center(target);
    let dx = target_center.x - source_center.x;
    let dy = target_center.y - source_center.y;
    let axis = if dx.abs() >= dy.abs() {
        RouteAxis::Horizontal
    } else {
        RouteAxis::Vertical
    };

    match axis {
        RouteAxis::Horizontal => horizontal_route(edge, source, target, lane_offset, dx >= 0.0),
        RouteAxis::Vertical => vertical_route(edge, source, target, lane_offset, dy >= 0.0),
    }
}

fn center(node: &NodeLayout) -> Point {
    Point {
        x: node.position.x + node.size.width / 2.0,
        y: node.position.y + node.size.height / 2.0,
    }
}

fn horizontal_route(
    edge: &StructuralGraphEdge,
    source: &NodeLayout,
    target: &NodeLayout,
    lane_offset: f64,
    left_to_right: bool,
) -> AnchoredRoute {
    let source_y = clamp_vertical_offset(source, lane_offset);
    let target_y = clamp_vertical_offset(target, lane_offset);
    let source_x = if left_to_right {
        source.position.x + source.size.width
    } else {
        source.position.x
    };
    let target_x = if left_to_right {
        target.position.x
    } else {
        target.position.x + target.size.width
    };
    let stub_delta = if left_to_right {
        PORT_OFFSET
    } else {
        -PORT_OFFSET
    };

    AnchoredRoute {
        axis: RouteAxis::Horizontal,
        edge_id: edge.id.clone(),
        end: Point {
            x: target_x,
            y: target_y,
        },
        end_stub: Point {
            x: target_x - stub_delta,
            y: target_y,
        },
        start: Point {
            x: source_x,
            y: source_y,
        },
        start_stub: Point {
            x: source_x + stub_delta,
            y: source_y,
        },
    }
}

fn vertical_route(
    edge: &StructuralGraphEdge,
    source: &NodeLayout,
    target: &NodeLayout,
    lane_offset: f64,
    top_to_bottom: bool,
) -> AnchoredRoute {
    let source_x = clamp_horizontal_offset(source, lane_offset);
    let target_x = clamp_horizontal_offset(target, lane_offset);
    let source_y = if top_to_bottom {
        source.position.y + source.size.height
    } else {
        source.position.y
    };
    let target_y = if top_to_bottom {
        target.position.y
    } else {
        target.position.y + target.size.height
    };
    let stub_delta = if top_to_bottom {
        PORT_OFFSET
    } else {
        -PORT_OFFSET
    };

    AnchoredRoute {
        axis: RouteAxis::Vertical,
        edge_id: edge.id.clone(),
        end: Point {
            x: target_x,
            y: target_y,
        },
        end_stub: Point {
            x: target_x,
            y: target_y - stub_delta,
        },
        start: Point {
            x: source_x,
            y: source_y,
        },
        start_stub: Point {
            x: source_x,
            y: source_y + stub_delta,
        },
    }
}

fn clamp_horizontal_offset(node: &NodeLayout, lane_offset: f64) -> f64 {
    let center_x = node.position.x + node.size.width / 2.0;
    let limit = (node.size.width / 2.0 - SIDE_MARGIN).max(0.0);
    center_x + lane_offset.clamp(-limit, limit)
}

fn clamp_vertical_offset(node: &NodeLayout, lane_offset: f64) -> f64 {
    let center_y = node.position.y + node.size.height / 2.0;
    let limit = (node.size.height / 2.0 - SIDE_MARGIN).max(0.0);
    center_y + lane_offset.clamp(-limit, limit)
}
