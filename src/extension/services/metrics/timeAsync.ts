export async function timeAsync<T>(
  work: () => Promise<T>,
): Promise<{ durationMs: number; result: T }> {
  const started = Date.now();
  const result = await work();

  return {
    durationMs: Date.now() - started,
    result,
  };
}
