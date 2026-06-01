use crate::protocol::analysis::{AnalyzerOutput, RelationKind};
use crate::protocol::diagnostics::AnalyzerDiagnostic;
use crate::protocol::graph::{
    DiagramGraph, MethodAssociation, StructuralEdgeProvenance, StructuralGraphEdge,
};
use crate::protocol::model_identity::CanonicalModelId;
use crate::resolve::model_registry::ModelRegistry;
use std::collections::BTreeSet;

pub fn build_diagram_graph(analyzer: &AnalyzerOutput) -> DiagramGraph {
    let registry = ModelRegistry::new(&analyzer.models);
    let mut diagnostics = analyzer.diagnostics.clone();
    let mut structural_edges = build_structural_edges(analyzer, &registry, &mut diagnostics);
    structural_edges.extend(build_inheritance_edges(analyzer, &registry));
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
    _diagnostics: &mut Vec<AnalyzerDiagnostic>,
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
                    // User policy: assume all relevant models are discovered
                    // via the models.Model inheritance scan. Skip diagnostic.
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

fn build_inheritance_edges(
    analyzer: &AnalyzerOutput,
    registry: &ModelRegistry,
) -> Vec<StructuralGraphEdge> {
    let mut edges = Vec::new();
    let mut seen_keys = BTreeSet::new();

    for model in &analyzer.models {
        for raw_base in &model.declared_base_classes {
            if is_django_builtin_base(raw_base) {
                continue;
            }

            let Some(parent_id) = registry.resolve_base_class(
                &model.identity.app_label,
                &model.identity.id,
                raw_base,
            ) else {
                continue;
            };

            if parent_id.as_str() == model.identity.id.as_str() {
                continue;
            }

            let edge = StructuralGraphEdge {
                id: inheritance_edge_id(&model.identity.id, &parent_id),
                kind: RelationKind::Inheritance,
                provenance: StructuralEdgeProvenance::Declared,
                source_model_id: model.identity.id.clone(),
                target_model_id: parent_id,
            };
            let key = edge_key(&edge);
            if seen_keys.insert(key) {
                edges.push(edge);
            }
        }
    }

    edges
}

fn is_django_builtin_base(raw: &str) -> bool {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return true;
    }
    // Standard Python / Django chrome that should never become a node edge.
    matches!(
        trimmed,
        "object" | "Model" | "models.Model"
    ) || trimmed.starts_with("django.")
}

fn inheritance_edge_id(child_id: &CanonicalModelId, parent_id: &CanonicalModelId) -> String {
    format!(
        "edge:inheritance:{}->{}",
        child_id.as_str(),
        parent_id.as_str()
    )
}

fn build_structural_edges(
    analyzer: &AnalyzerOutput,
    registry: &ModelRegistry,
    _diagnostics: &mut Vec<AnalyzerDiagnostic>,
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
                // User policy: assume all models discovered; self-refs
                // handled visually elsewhere. Skip diagnostic.
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
        | RelationKind::ReverseOneToOne
        | RelationKind::Inheritance => None,
    }
}

