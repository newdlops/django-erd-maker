export interface PipelineTimings {
  analyzerBootstrapMs?: number;
  discoveryMs?: number;
  extractMs?: number;
  graphMs?: number;
  layoutMs?: number;
  parseMs?: number;
  renderDocumentMs?: number;
}
