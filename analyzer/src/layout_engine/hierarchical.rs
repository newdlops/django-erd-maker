use crate::layout_engine::{
    LayoutContext, component_edges, component_nodes, layer_spacing_x, round2, row_spacing_y,
};
use crate::protocol::layout::{NodeLayout, Point};
use std::collections::{BTreeMap, VecDeque};

const COMPONENT_GAP_Y: f64 = 96.0;

pub fn compute(context: &LayoutContext) -> Vec<NodeLayout> {
    let mut layouts = Vec::new();
    let mut offset_y = 0.0;

    for component in &context.components {
        let nodes = component_nodes(context, component);
        let edges = component_edges(context, component);
        let levels = compute_levels(&nodes, &edges);
        let max_level = levels.values().copied().max().unwrap_or(0);
        let mut layer_widths = vec![0.0_f64; max_level + 1];

        for node in &nodes {
            let level = *levels.get(node.model_id.as_str()).unwrap_or(&0);
            layer_widths[level] = layer_widths[level].max(node.size.width);
        }

        let mut layer_x = Vec::with_capacity(layer_widths.len());
        let mut running_x = 0.0;
        for width in &layer_widths {
            layer_x.push(running_x);
            running_x += *width + layer_spacing_x();
        }

        let mut component_height = 0.0_f64;
        for level in 0..=max_level {
            let mut layer_nodes = nodes
                .iter()
                .filter(|node| levels.get(node.model_id.as_str()) == Some(&level))
                .collect::<Vec<_>>();
            layer_nodes.sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));

            let mut y = 0.0;
            for node in layer_nodes {
                layouts.push(NodeLayout {
                    model_id: node.model_id.clone(),
                    position: Point {
                        x: round2(layer_x[level]),
                        y: round2(offset_y + y),
                    },
                    size: node.size.clone(),
                });
                y += node.size.height + row_spacing_y();
            }
            component_height = component_height.max(y);
        }

        offset_y += component_height + COMPONENT_GAP_Y;
    }

    layouts
}

fn compute_levels(
    nodes: &[&crate::layout_engine::measurement::MeasuredNode],
    edges: &[(String, String)],
) -> BTreeMap<String, usize> {
    let node_ids = nodes
        .iter()
        .map(|node| node.model_id.as_str().to_string())
        .collect::<Vec<_>>();
    let mut incoming_count = node_ids
        .iter()
        .map(|model_id| (model_id.clone(), 0usize))
        .collect::<BTreeMap<_, _>>();
    let mut outgoing = BTreeMap::<String, Vec<String>>::new();

    for (source, target) in edges {
        if let Some(incoming) = incoming_count.get_mut(target) {
            *incoming += 1;
        }
        outgoing
            .entry(source.clone())
            .or_default()
            .push(target.clone());
    }

    let mut queue = incoming_count
        .iter()
        .filter(|(_, count)| **count == 0)
        .map(|(model_id, _)| model_id.clone())
        .collect::<Vec<_>>();
    if queue.is_empty() {
        queue.push(node_ids[0].clone());
    }
    queue.sort();

    let mut deque = VecDeque::from(queue);
    let mut levels = BTreeMap::<String, usize>::new();

    while let Some(current) = deque.pop_front() {
        let current_level = *levels.get(&current).unwrap_or(&0);
        let mut neighbors = outgoing.get(&current).cloned().unwrap_or_default();
        neighbors.sort();
        for neighbor in neighbors {
            let next_level = current_level + 1;
            if levels.get(&neighbor).copied().unwrap_or(0) < next_level {
                levels.insert(neighbor.clone(), next_level);
            }
            if let Some(incoming) = incoming_count.get_mut(&neighbor) {
                if *incoming > 0 {
                    *incoming -= 1;
                    if *incoming == 0 {
                        deque.push_back(neighbor);
                    }
                }
            }
        }
    }

    for node_id in node_ids {
        levels.entry(node_id).or_insert(0);
    }

    levels
}
