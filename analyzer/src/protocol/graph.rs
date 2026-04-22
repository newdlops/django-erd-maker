use crate::protocol::analysis::{MethodAssociationConfidence, RelationKind};
use crate::protocol::diagnostics::AnalyzerDiagnostic;
use crate::protocol::model_identity::CanonicalModelId;
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum StructuralEdgeProvenance {
    Declared,
    DerivedReverse,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GraphNode {
    pub app_label: String,
    pub model_id: CanonicalModelId,
    pub model_name: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StructuralGraphEdge {
    pub id: String,
    pub kind: RelationKind,
    pub provenance: StructuralEdgeProvenance,
    pub source_model_id: CanonicalModelId,
    pub target_model_id: CanonicalModelId,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MethodAssociation {
    pub confidence: MethodAssociationConfidence,
    pub id: String,
    pub method_name: String,
    pub provenance: &'static str,
    pub source_model_id: CanonicalModelId,
    pub target_model_id: CanonicalModelId,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiagramGraph {
    pub diagnostics: Vec<AnalyzerDiagnostic>,
    pub method_associations: Vec<MethodAssociation>,
    pub nodes: Vec<GraphNode>,
    pub structural_edges: Vec<StructuralGraphEdge>,
}

impl DiagramGraph {
    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).expect("diagram graph should serialize to JSON")
    }
}
