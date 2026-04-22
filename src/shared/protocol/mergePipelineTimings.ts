import type { PipelineTimings } from "./pipelineTimingContract";

export function mergePipelineTimings(
  existing: PipelineTimings | undefined,
  incoming: PipelineTimings,
): PipelineTimings {
  return {
    ...existing,
    ...incoming,
  };
}
