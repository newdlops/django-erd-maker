use crate::protocol::analysis::{
    ExtractedModel, MethodRelatedModelReference, RelationTargetReference,
};
use crate::protocol::graph::GraphNode;
use crate::protocol::model_identity::{CanonicalModelId, ModelIdentity};
use std::collections::BTreeMap;

#[derive(Debug, Clone)]
pub struct ModelRegistry {
    models: BTreeMap<String, ModelIdentity>,
}

impl ModelRegistry {
    pub fn new(models: &[ExtractedModel]) -> Self {
        let models = models
            .iter()
            .map(|model| {
                (
                    model.identity.id.as_str().to_string(),
                    model.identity.clone(),
                )
            })
            .collect();

        Self { models }
    }

    pub fn contains(&self, model_id: &CanonicalModelId) -> bool {
        self.models.contains_key(model_id.as_str())
    }

    pub fn graph_nodes(&self) -> Vec<GraphNode> {
        self.models
            .values()
            .map(|identity| GraphNode {
                app_label: identity.app_label.clone(),
                model_id: identity.id.clone(),
                model_name: identity.model_name.clone(),
            })
            .collect()
    }

    pub fn resolve_method_target(
        &self,
        source_app_label: &str,
        source_model_id: &CanonicalModelId,
        reference: &MethodRelatedModelReference,
    ) -> Option<CanonicalModelId> {
        if let Some(target_model_id) = &reference.target_model_id {
            if self.contains(target_model_id) {
                return Some(target_model_id.clone());
            }
        }

        let raw_reference = reference.raw_reference.as_deref()?;
        let candidate =
            candidate_from_raw_reference(source_app_label, source_model_id, raw_reference)?;

        self.contains(&candidate).then_some(candidate)
    }

    pub fn resolve_relation_target(
        &self,
        source_app_label: &str,
        source_model_id: &CanonicalModelId,
        target: &RelationTargetReference,
    ) -> Option<CanonicalModelId> {
        if let Some(resolved_model_id) = &target.resolved_model_id {
            if self.contains(resolved_model_id) {
                return Some(resolved_model_id.clone());
            }
        }

        let candidate = if target.raw_reference == "self" {
            Some(source_model_id.clone())
        } else if target.raw_reference.contains('.') {
            candidate_from_raw_reference(source_app_label, source_model_id, &target.raw_reference)
        } else {
            let app_label = target.app_label_hint.as_deref().unwrap_or(source_app_label);
            Some(CanonicalModelId::new(app_label, &target.raw_reference))
        }?;

        self.contains(&candidate).then_some(candidate)
    }
}

fn candidate_from_raw_reference(
    source_app_label: &str,
    source_model_id: &CanonicalModelId,
    raw_reference: &str,
) -> Option<CanonicalModelId> {
    if raw_reference.is_empty() {
        return None;
    }

    if raw_reference == "self" {
        return Some(source_model_id.clone());
    }

    let segments = raw_reference.split('.').collect::<Vec<_>>();
    match segments.as_slice() {
        [model_name] => Some(CanonicalModelId::new(source_app_label, model_name)),
        [app_label, model_name] => Some(CanonicalModelId::new(app_label, model_name)),
        _ => None,
    }
}
