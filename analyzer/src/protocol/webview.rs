use crate::protocol::analysis::AnalyzerOutput;
use crate::protocol::contract_version::CONTRACT_VERSION;
use crate::protocol::graph::DiagramGraph;
use crate::protocol::layout::{LayoutMode, LayoutSnapshot, Point};
use crate::protocol::model_identity::CanonicalModelId;
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PipelineTimings {
    pub analyzer_bootstrap_ms: Option<f64>,
    pub discovery_ms: Option<f64>,
    pub extract_ms: Option<f64>,
    pub graph_ms: Option<f64>,
    pub layout_ms: Option<f64>,
    pub parse_ms: Option<f64>,
    pub render_document_ms: Option<f64>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TableViewOptions {
    pub hidden: bool,
    pub manual_position: Option<Point>,
    pub model_id: CanonicalModelId,
    pub show_method_highlights: bool,
    pub show_methods: bool,
    pub show_properties: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SelectedMethodContext {
    pub method_name: String,
    pub model_id: CanonicalModelId,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InitialViewState {
    pub layout_mode: LayoutMode,
    pub selected_method_context: Option<SelectedMethodContext>,
    pub selected_model_id: Option<CanonicalModelId>,
    pub table_options: Vec<TableViewOptions>,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiagramBootstrapPayload {
    pub analyzer: AnalyzerOutput,
    pub contract_version: &'static str,
    pub graph: DiagramGraph,
    pub layout: LayoutSnapshot,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub timings: Option<PipelineTimings>,
    pub view: InitialViewState,
}

impl DiagramBootstrapPayload {
    pub fn new(
        analyzer: AnalyzerOutput,
        graph: DiagramGraph,
        layout: LayoutSnapshot,
        view: InitialViewState,
    ) -> Self {
        Self {
            analyzer,
            contract_version: CONTRACT_VERSION,
            graph,
            layout,
            timings: None,
            view,
        }
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self)
            .expect("diagram bootstrap payload should serialize to JSON")
    }
}
