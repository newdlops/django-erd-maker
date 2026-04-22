use crate::protocol::layout::{Point, RoutedEdgePath};
use crate::routing::anchor::{AnchoredRoute, RouteAxis};

pub fn route_orthogonal(anchored_route: &AnchoredRoute) -> RoutedEdgePath {
    let mut points = match anchored_route.axis {
        RouteAxis::Horizontal => horizontal_points(anchored_route),
        RouteAxis::Vertical => vertical_points(anchored_route),
    };

    compress_points(&mut points);

    RoutedEdgePath {
        crossing_ids: Vec::new(),
        edge_id: anchored_route.edge_id.clone(),
        points,
    }
}

fn horizontal_points(anchored_route: &AnchoredRoute) -> Vec<Point> {
    let mid_x = (anchored_route.start_stub.x + anchored_route.end_stub.x) / 2.0;

    vec![
        anchored_route.start.clone(),
        anchored_route.start_stub.clone(),
        Point {
            x: mid_x,
            y: anchored_route.start_stub.y,
        },
        Point {
            x: mid_x,
            y: anchored_route.end_stub.y,
        },
        anchored_route.end_stub.clone(),
        anchored_route.end.clone(),
    ]
}

fn vertical_points(anchored_route: &AnchoredRoute) -> Vec<Point> {
    let mid_y = (anchored_route.start_stub.y + anchored_route.end_stub.y) / 2.0;

    vec![
        anchored_route.start.clone(),
        anchored_route.start_stub.clone(),
        Point {
            x: anchored_route.start_stub.x,
            y: mid_y,
        },
        Point {
            x: anchored_route.end_stub.x,
            y: mid_y,
        },
        anchored_route.end_stub.clone(),
        anchored_route.end.clone(),
    ]
}

fn compress_points(points: &mut Vec<Point>) {
    points.retain(|point| !point.x.is_nan() && !point.y.is_nan());

    let mut deduped = Vec::<Point>::new();
    for point in points.drain(..) {
        if deduped
            .last()
            .is_some_and(|last| almost_same_point(last, &point))
        {
            continue;
        }
        deduped.push(round_point(point));
    }

    let mut compressed = Vec::<Point>::new();
    for point in deduped {
        if compressed.len() >= 2 {
            let prev = &compressed[compressed.len() - 1];
            let prev_prev = &compressed[compressed.len() - 2];
            if is_collinear(prev_prev, prev, &point) {
                compressed.pop();
            }
        }
        compressed.push(point);
    }

    *points = compressed;
}

fn almost_same_point(left: &Point, right: &Point) -> bool {
    (left.x - right.x).abs() < 0.01 && (left.y - right.y).abs() < 0.01
}

fn is_collinear(left: &Point, middle: &Point, right: &Point) -> bool {
    ((left.x - middle.x).abs() < 0.01 && (middle.x - right.x).abs() < 0.01)
        || ((left.y - middle.y).abs() < 0.01 && (middle.y - right.y).abs() < 0.01)
}

fn round_point(point: Point) -> Point {
    Point {
        x: round2(point.x),
        y: round2(point.y),
    }
}

fn round2(value: f64) -> f64 {
    (value * 100.0).round() / 100.0
}
