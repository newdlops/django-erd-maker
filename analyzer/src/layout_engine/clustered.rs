use crate::layout_engine::{LayoutContext, round2};
use crate::protocol::layout::{NodeLayout, Point};
use std::collections::BTreeMap;

const CLUSTER_GAP_X: f64 = 120.0;
const CLUSTER_GAP_Y: f64 = 120.0;
const NODE_GAP_Y: f64 = 36.0;

pub fn compute(context: &LayoutContext) -> Vec<NodeLayout> {
    let mut layouts = Vec::new();
    let mut by_app =
        BTreeMap::<String, Vec<&crate::layout_engine::measurement::MeasuredNode>>::new();

    for node in &context.nodes {
        by_app.entry(node.app_label.clone()).or_default().push(node);
    }

    let app_labels = by_app.keys().cloned().collect::<Vec<_>>();
    let columns = (app_labels.len().max(1) as f64).sqrt().ceil() as usize;

    for (index, app_label) in app_labels.iter().enumerate() {
        let mut nodes = by_app.get(app_label).cloned().unwrap_or_default();
        nodes.sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));

        let column = index % columns;
        let row = index / columns;
        let cluster_width = nodes.iter().map(|node| node.size.width).fold(0.0, f64::max);
        let cluster_x = column as f64 * (cluster_width + CLUSTER_GAP_X);
        let cluster_y = row as f64 * cluster_height(&nodes);

        let mut y = cluster_y;
        for node in nodes {
            layouts.push(NodeLayout {
                model_id: node.model_id.clone(),
                position: Point {
                    x: round2(cluster_x),
                    y: round2(y),
                },
                size: node.size.clone(),
            });
            y += node.size.height + NODE_GAP_Y;
        }
    }

    layouts
}

fn cluster_height(nodes: &[&crate::layout_engine::measurement::MeasuredNode]) -> f64 {
    let total_height = nodes.iter().map(|node| node.size.height).sum::<f64>();
    let gaps = if nodes.is_empty() {
        0.0
    } else {
        (nodes.len() - 1) as f64 * NODE_GAP_Y
    };

    total_height + gaps + CLUSTER_GAP_Y
}
