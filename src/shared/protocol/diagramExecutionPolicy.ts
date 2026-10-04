// Reserve ten seconds of the user's two-minute limit for document generation
// and the webview's first frame. Layout shares the request's absolute deadline.
export const DIAGRAM_COMPUTATION_BUDGET_MS = 110_000;
export const LAYOUT_COMPUTATION_BUDGET_MS = 90_000;

export interface DiagramExecutionOptions {
  freshAnalysis?: boolean;
  deadlineMs?: number;
}
