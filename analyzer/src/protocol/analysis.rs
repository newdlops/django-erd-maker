use crate::protocol::contract_version::CONTRACT_VERSION;
use crate::protocol::diagnostics::AnalyzerDiagnostic;
use crate::protocol::model_identity::{CanonicalModelId, ModelIdentity};
use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ChoiceValueKind {
    Boolean,
    Null,
    Number,
    String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum MethodAssociationConfidence {
    High,
    Low,
    Medium,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum MethodVisibility {
    Private,
    Protected,
    Public,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ModelFieldPersistence {
    Computed,
    Stored,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum RelationKind {
    ForeignKey,
    ManyToMany,
    OneToOne,
    ReverseForeignKey,
    ReverseManyToMany,
    ReverseOneToOne,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ResolutionState {
    Deferred,
    Resolved,
    Unresolved,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AnalysisSummary {
    pub diagnostic_count: usize,
    pub discovered_app_count: usize,
    pub discovered_model_count: usize,
    pub workspace_root: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ChoiceOption {
    pub label: String,
    pub value: String,
    pub value_kind: ChoiceValueKind,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ChoiceFieldMetadata {
    pub is_choice_field: bool,
    pub is_fully_resolved: bool,
    pub options: Vec<ChoiceOption>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RelationTargetReference {
    pub app_label_hint: Option<String>,
    pub raw_reference: String,
    pub resolution_state: ResolutionState,
    pub resolved_model_id: Option<CanonicalModelId>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FieldRelation {
    pub kind: RelationKind,
    pub reverse_accessor_name: Option<String>,
    pub target: RelationTargetReference,
    pub through_model_id: Option<CanonicalModelId>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModelField {
    pub choice_metadata: Option<ChoiceFieldMetadata>,
    pub field_type: String,
    pub name: String,
    pub nullable: bool,
    pub persistence: ModelFieldPersistence,
    pub primary_key: bool,
    pub relation: Option<FieldRelation>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PropertyAttribute {
    pub name: String,
    pub return_type: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct MethodRelatedModelReference {
    pub confidence: MethodAssociationConfidence,
    pub evidence: Option<String>,
    pub raw_reference: Option<String>,
    pub target_model_id: Option<CanonicalModelId>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct UserMethod {
    pub name: String,
    pub related_models: Vec<MethodRelatedModelReference>,
    pub visibility: MethodVisibility,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ExtractedModel {
    pub database_table_name: String,
    pub declared_base_classes: Vec<String>,
    pub has_explicit_database_table_name: bool,
    pub fields: Vec<ModelField>,
    pub identity: ModelIdentity,
    pub methods: Vec<UserMethod>,
    pub properties: Vec<PropertyAttribute>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AnalyzerOutput {
    pub contract_version: &'static str,
    pub diagnostics: Vec<AnalyzerDiagnostic>,
    pub models: Vec<ExtractedModel>,
    pub summary: AnalysisSummary,
}

impl AnalyzerOutput {
    pub fn empty(workspace_root: &str) -> Self {
        Self {
            contract_version: CONTRACT_VERSION,
            diagnostics: Vec::new(),
            models: Vec::new(),
            summary: AnalysisSummary {
                diagnostic_count: 0,
                discovered_app_count: 0,
                discovered_model_count: 0,
                workspace_root: workspace_root.to_string(),
            },
        }
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).expect("analyzer output should serialize to JSON")
    }
}
