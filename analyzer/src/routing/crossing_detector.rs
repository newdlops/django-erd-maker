use crate::protocol::graph::StructuralGraphEdge;
use crate::protocol::layout::{CrossingMarkerStyle, EdgeCrossing, Point, RoutedEdgePath};
use std::collections::BTreeMap;

pub fn detect_crossings(
    edges: &[StructuralGraphEdge],
    routed_edges: &mut [RoutedEdgePath],
) -> Vec<EdgeCrossing> {
    let mut crossings = Vec::new();
    let index_by_edge_id = routed_edges
        .iter()
        .enumerate()
        .map(|(index, path)| (path.edge_id.clone(), index))
        .collect::<BTreeMap<_, _>>();

    for left_index in 0..edges.len() {
        for right_index in (left_index + 1)..edges.len() {
            let left_edge = &edges[left_index];
            let right_edge = &edges[right_index];
            if shares_endpoint(left_edge, right_edge) {
                continue;
            }

            let Some(&left_path_index) = index_by_edge_id.get(&left_edge.id) else {
                continue;
            };
            let Some(&right_path_index) = index_by_edge_id.get(&right_edge.id) else {
                continue;
            };

            let intersections = segment_intersections(
                &routed_edges[left_path_index].points,
                &routed_edges[right_path_index].points,
            );
            for (intersection_index, position) in intersections.into_iter().enumerate() {
                let crossing_id = format!(
                    "cross:{}:{}:{intersection_index}",
                    left_edge.id, right_edge.id
                );
                routed_edges[left_path_index]
                    .crossing_ids
                    .push(crossing_id.clone());
                routed_edges[right_path_index]
                    .crossing_ids
                    .push(crossing_id.clone());
                crossings.push(EdgeCrossing {
                    edge_ids: [left_edge.id.clone(), right_edge.id.clone()],
                    id: crossing_id,
                    marker_style: CrossingMarkerStyle::Bridge,
                    position,
                });
            }
        }
    }

    for path in routed_edges {
        path.crossing_ids.sort();
    }

    crossings.sort_by(|left, right| left.id.cmp(&right.id));
    crossings
}

fn segment_intersections(left_points: &[Point], right_points: &[Point]) -> Vec<Point> {
    let mut intersections = Vec::new();

    for left_segment in left_points.windows(2) {
        let Some(left_orientation) = orientation(&left_segment[0], &left_segment[1]) else {
            continue;
        };
        for right_segment in right_points.windows(2) {
            let Some(right_orientation) = orientation(&right_segment[0], &right_segment[1]) else {
                continue;
            };
            if left_orientation == right_orientation {
                continue;
            }

            let (horizontal_start, horizontal_end, vertical_start, vertical_end) =
                if left_orientation == SegmentOrientation::Horizontal {
                    (
                        &left_segment[0],
                        &left_segment[1],
                        &right_segment[0],
                        &right_segment[1],
                    )
                } else {
                    (
                        &right_segment[0],
                        &right_segment[1],
                        &left_segment[0],
                        &left_segment[1],
                    )
                };

            let x_range = ordered_pair(horizontal_start.x, horizontal_end.x);
            let y_range = ordered_pair(vertical_start.y, vertical_end.y);
            let intersection = Point {
                x: vertical_start.x,
                y: horizontal_start.y,
            };

            if strictly_between(intersection.x, x_range.0, x_range.1)
                && strictly_between(intersection.y, y_range.0, y_range.1)
            {
                intersections.push(intersection);
            }
        }
    }

    intersections.sort_by(|left, right| {
        left.x
            .partial_cmp(&right.x)
            .unwrap_or(std::cmp::Ordering::Equal)
            .then_with(|| {
                left.y
                    .partial_cmp(&right.y)
                    .unwrap_or(std::cmp::Ordering::Equal)
            })
    });
    intersections
        .dedup_by(|left, right| (left.x - right.x).abs() < 0.01 && (left.y - right.y).abs() < 0.01);

    intersections
}

fn ordered_pair(left: f64, right: f64) -> (f64, f64) {
    if left <= right {
        (left, right)
    } else {
        (right, left)
    }
}

fn shares_endpoint(left: &StructuralGraphEdge, right: &StructuralGraphEdge) -> bool {
    left.source_model_id == right.source_model_id
        || left.source_model_id == right.target_model_id
        || left.target_model_id == right.source_model_id
        || left.target_model_id == right.target_model_id
}

fn strictly_between(value: f64, start: f64, end: f64) -> bool {
    value > start + 0.01 && value < end - 0.01
}

fn orientation(start: &Point, end: &Point) -> Option<SegmentOrientation> {
    if (start.x - end.x).abs() < 0.01 && (start.y - end.y).abs() > 0.01 {
        Some(SegmentOrientation::Vertical)
    } else if (start.y - end.y).abs() < 0.01 && (start.x - end.x).abs() > 0.01 {
        Some(SegmentOrientation::Horizontal)
    } else {
        None
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SegmentOrientation {
    Horizontal,
    Vertical,
}
