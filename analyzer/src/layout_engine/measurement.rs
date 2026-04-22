use crate::protocol::analysis::{AnalyzerOutput, ExtractedModel};
use crate::protocol::graph::DiagramGraph;
use crate::protocol::layout::Size;
use crate::protocol::model_identity::CanonicalModelId;
use std::collections::{BTreeMap, BTreeSet};

const BOX_HEIGHT: f64 = 76.0;
const MAX_WIDTH: f64 = 360.0;
const MIN_WIDTH: f64 = 180.0;
const TITLE_WIDTH_PADDING: f64 = 72.0;
const WIDTH_PER_CHAR: f64 = 8.0;

#[derive(Debug, Clone, PartialEq)]
pub struct MeasuredNode {
    pub app_label: String,
    pub model_id: CanonicalModelId,
    pub model_name: String,
    pub size: Size,
}

pub fn measure_visible_nodes(
    analyzer: &AnalyzerOutput,
    graph: &DiagramGraph,
    hidden_model_ids: &BTreeSet<String>,
) -> Vec<MeasuredNode> {
    let models_by_id = analyzer
        .models
        .iter()
        .map(|model| (model.identity.id.as_str().to_string(), model))
        .collect::<BTreeMap<_, _>>();

    let mut measured = graph
        .nodes
        .iter()
        .filter(|node| !hidden_model_ids.contains(node.model_id.as_str()))
        .map(|node| MeasuredNode {
            app_label: node.app_label.clone(),
            model_id: node.model_id.clone(),
            model_name: node.model_name.clone(),
            size: models_by_id
                .get(node.model_id.as_str())
                .map(|model| measure_model(model))
                .unwrap_or_else(|| default_size(&node.model_name)),
        })
        .collect::<Vec<_>>();

    measured.sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));
    measured
}

fn default_size(model_name: &str) -> Size {
    let width_chars = model_name.len().max(12) as f64;
    Size {
        height: BOX_HEIGHT,
        width: round2(
            (TITLE_WIDTH_PADDING + width_chars * WIDTH_PER_CHAR).clamp(MIN_WIDTH, MAX_WIDTH),
        ),
    }
}

fn measure_model(model: &ExtractedModel) -> Size {
    let width_chars = model
        .database_table_name
        .len()
        .max(model.identity.model_name.len())
        .max(12) as f64;

    Size {
        height: BOX_HEIGHT,
        width: round2(
            (TITLE_WIDTH_PADDING + width_chars * WIDTH_PER_CHAR).clamp(MIN_WIDTH, MAX_WIDTH),
        ),
    }
}

fn round2(value: f64) -> f64 {
    (value * 100.0).round() / 100.0
}
