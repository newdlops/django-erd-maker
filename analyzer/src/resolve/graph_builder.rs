use crate::protocol::analysis::{AnalyzerOutput, RelationKind};
use crate::protocol::diagnostics::{AnalyzerDiagnostic, DiagnosticCode, DiagnosticSeverity};
use crate::protocol::graph::{
    DiagramGraph, MethodAssociation, StructuralEdgeProvenance, StructuralGraphEdge,
};
use crate::protocol::model_identity::CanonicalModelId;
use crate::resolve::model_registry::ModelRegistry;
use std::collections::BTreeSet;

pub fn build_diagram_graph(analyzer: &AnalyzerOutput) -> DiagramGraph {
    let registry = ModelRegistry::new(&analyzer.models);
    let mut diagnostics = analyzer.diagnostics.clone();
    let structural_edges = build_structural_edges(analyzer, &registry, &mut diagnostics);
    let method_associations = build_method_associations(analyzer, &registry, &mut diagnostics);

    let mut graph = DiagramGraph {
        diagnostics: dedupe_diagnostics(diagnostics),
        method_associations,
        nodes: registry.graph_nodes(),
        structural_edges,
    };

    graph
        .method_associations
        .sort_by(|left, right| left.id.cmp(&right.id));
    graph
        .structural_edges
        .sort_by(|left, right| left.id.cmp(&right.id));

    graph
}

fn build_method_associations(
    analyzer: &AnalyzerOutput,
    registry: &ModelRegistry,
    diagnostics: &mut Vec<AnalyzerDiagnostic>,
) -> Vec<MethodAssociation> {
    let mut associations = Vec::new();
    let mut seen_keys = BTreeSet::new();

    for model in &analyzer.models {
        for method in &model.methods {
            for (reference_index, reference) in method.related_models.iter().enumerate() {
                let Some(target_model_id) = registry.resolve_method_target(
                    &model.identity.app_label,
                    &model.identity.id,
                    reference,
                ) else {
                    if let Some(raw_reference) = reference.raw_reference.as_deref() {
                        diagnostics.push(unresolved_reference_diagnostic(
                            &model.identity.id,
                            format!(
                                "Method '{}.{}' references '{}', but that model was not discovered.",
                                model.identity.model_name, method.name, raw_reference
                            ),
                        ));
                    }
                    continue;
                };

                let key = format!(
                    "{}::{}::{}::{}",
                    model.identity.id.as_str(),
                    method.name,
                    target_model_id.as_str(),
                    reference_index
                );
                if !seen_keys.insert(key.clone()) {
                    continue;
                }

                associations.push(MethodAssociation {
                    confidence: reference.confidence.clone(),
                    id: format!(
                        "assoc:{}:{}:{reference_index}",
                        model.identity.id.as_str(),
                        method.name
                    ),
                    method_name: method.name.clone(),
                    provenance: "method_inference",
                    source_model_id: model.identity.id.clone(),
                    target_model_id,
                });
            }
        }
    }

    associations
}

fn build_structural_edges(
    analyzer: &AnalyzerOutput,
    registry: &ModelRegistry,
    diagnostics: &mut Vec<AnalyzerDiagnostic>,
) -> Vec<StructuralGraphEdge> {
    let mut edges = Vec::new();
    let mut seen_edges = BTreeSet::new();

    for model in &analyzer.models {
        for field in &model.fields {
            let Some(relation) = &field.relation else {
                continue;
            };

            let Some(target_model_id) = registry.resolve_relation_target(
                &model.identity.app_label,
                &model.identity.id,
                &relation.target,
            ) else {
                diagnostics.push(unresolved_reference_diagnostic(
                    &model.identity.id,
                    format!(
                        "Relation field '{}.{}' points to '{}', but that model was not discovered.",
                        model.identity.model_name, field.name, relation.target.raw_reference
                    ),
                ));
                continue;
            };

            let declared_edge = StructuralGraphEdge {
                id: declared_edge_id(&model.identity.id, &field.name),
                kind: relation.kind.clone(),
                provenance: StructuralEdgeProvenance::Declared,
                source_model_id: model.identity.id.clone(),
                target_model_id: target_model_id.clone(),
            };
            let declared_key = edge_key(&declared_edge);
            if seen_edges.insert(declared_key) {
                edges.push(declared_edge.clone());
            }

            if let Some(reverse_kind) = reverse_relation_kind(&relation.kind) {
                let reverse_edge = StructuralGraphEdge {
                    id: reverse_edge_id(&model.identity.id, &field.name),
                    kind: reverse_kind,
                    provenance: StructuralEdgeProvenance::DerivedReverse,
                    source_model_id: target_model_id,
                    target_model_id: model.identity.id.clone(),
                };
                let reverse_key = edge_key(&reverse_edge);
                if seen_edges.insert(reverse_key) {
                    edges.push(reverse_edge);
                }
            }
        }
    }

    edges
}

fn dedupe_diagnostics(diagnostics: Vec<AnalyzerDiagnostic>) -> Vec<AnalyzerDiagnostic> {
    let mut seen = BTreeSet::new();
    let mut unique = Vec::new();

    for diagnostic in diagnostics {
        let key = format!(
            "{:?}|{:?}|{}|{}",
            diagnostic.code,
            diagnostic.related_model_id,
            diagnostic.message,
            diagnostic
                .location
                .as_ref()
                .map(|location| location.file_path.as_str())
                .unwrap_or_default()
        );
        if seen.insert(key) {
            unique.push(diagnostic);
        }
    }

    unique.sort_by(|left, right| {
        let left_key = (
            left.location
                .as_ref()
                .map(|location| location.file_path.as_str())
                .unwrap_or_default(),
            left.message.as_str(),
        );
        let right_key = (
            right
                .location
                .as_ref()
                .map(|location| location.file_path.as_str())
                .unwrap_or_default(),
            right.message.as_str(),
        );
        left_key.cmp(&right_key)
    });

    unique
}

fn declared_edge_id(source_model_id: &CanonicalModelId, field_name: &str) -> String {
    format!("edge:declared:{}:{field_name}", source_model_id.as_str())
}

fn edge_key(edge: &StructuralGraphEdge) -> String {
    format!(
        "{}|{:?}|{}|{}|{:?}",
        edge.id,
        edge.kind,
        edge.source_model_id.as_str(),
        edge.target_model_id.as_str(),
        edge.provenance
    )
}

fn reverse_edge_id(source_model_id: &CanonicalModelId, field_name: &str) -> String {
    format!("edge:reverse:{}:{field_name}", source_model_id.as_str())
}

fn reverse_relation_kind(kind: &RelationKind) -> Option<RelationKind> {
    match kind {
        RelationKind::ForeignKey => Some(RelationKind::ReverseForeignKey),
        RelationKind::ManyToMany => Some(RelationKind::ReverseManyToMany),
        RelationKind::OneToOne => Some(RelationKind::ReverseOneToOne),
        RelationKind::ReverseForeignKey
        | RelationKind::ReverseManyToMany
        | RelationKind::ReverseOneToOne => None,
    }
}

fn unresolved_reference_diagnostic(
    related_model_id: &CanonicalModelId,
    message: String,
) -> AnalyzerDiagnostic {
    AnalyzerDiagnostic {
        code: DiagnosticCode::UnresolvedReference,
        location: None,
        message,
        related_model_id: Some(related_model_id.clone()),
        severity: DiagnosticSeverity::Warning,
    }
}
