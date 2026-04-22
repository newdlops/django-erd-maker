use crate::layout_engine::{LayoutContext, component_nodes, round2};
use crate::protocol::layout::{NodeLayout, Point};
use std::f64::consts::{FRAC_PI_2, TAU};

const COMPONENT_GAP_X: f64 = 160.0;
const MIN_RADIUS: f64 = 140.0;

pub fn compute(context: &LayoutContext) -> Vec<NodeLayout> {
    let mut layouts = Vec::new();
    let mut offset_x = 0.0;

    for component in &context.components {
        let mut nodes = component_nodes(context, component);
        nodes.sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));

        let max_width = nodes.iter().map(|node| node.size.width).fold(0.0, f64::max);
        let max_height = nodes
            .iter()
            .map(|node| node.size.height)
            .fold(0.0, f64::max);
        let radius = if nodes.len() <= 1 {
            0.0
        } else {
            (MIN_RADIUS + nodes.len() as f64 * 20.0).max(max_width.max(max_height))
        };
        let center_x = offset_x + radius + max_width / 2.0;
        let center_y = radius + max_height / 2.0;

        if nodes.len() == 1 {
            let node = nodes[0];
            layouts.push(NodeLayout {
                model_id: node.model_id.clone(),
                position: Point {
                    x: round2(center_x - node.size.width / 2.0),
                    y: round2(center_y - node.size.height / 2.0),
                },
                size: node.size.clone(),
            });
        } else {
            for (index, node) in nodes.iter().enumerate() {
                let angle = TAU * index as f64 / nodes.len() as f64 - FRAC_PI_2;
                layouts.push(NodeLayout {
                    model_id: node.model_id.clone(),
                    position: Point {
                        x: round2(center_x + radius * angle.cos() - node.size.width / 2.0),
                        y: round2(center_y + radius * angle.sin() - node.size.height / 2.0),
                    },
                    size: node.size.clone(),
                });
            }
        }

        offset_x += radius * 2.0 + max_width + COMPONENT_GAP_X;
    }

    layouts
}
