#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HelloPayload {
    message: &'static str,
    status: &'static str,
    version: &'static str,
}

impl HelloPayload {
    pub fn new(message: &'static str, version: &'static str) -> Self {
        Self {
            message,
            status: "ok",
            version,
        }
    }

    pub fn to_json(&self) -> String {
        format!(
            "{{\"status\":\"{}\",\"message\":\"{}\",\"version\":\"{}\"}}",
            escape_json(self.status),
            escape_json(self.message),
            escape_json(self.version),
        )
    }
}

fn escape_json(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}

#[cfg(test)]
mod tests {
    use super::HelloPayload;

    #[test]
    fn renders_expected_json_shape() {
        let payload = HelloPayload::new("placeholder", "0.0.1");

        assert_eq!(
            payload.to_json(),
            "{\"status\":\"ok\",\"message\":\"placeholder\",\"version\":\"0.0.1\"}"
        );
    }
}
