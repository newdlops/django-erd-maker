use serde::Serialize;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct CanonicalModelId(String);

impl CanonicalModelId {
    pub fn new(app_label: &str, model_name: &str) -> Self {
        Self(format!("{app_label}.{model_name}"))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModelIdentity {
    pub app_label: String,
    pub id: CanonicalModelId,
    pub model_name: String,
    pub module_path: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::CanonicalModelId;

    #[test]
    fn creates_canonical_model_id() {
        let model_id = CanonicalModelId::new("blog", "Post");

        assert_eq!(model_id.as_str(), "blog.Post");
    }
}
