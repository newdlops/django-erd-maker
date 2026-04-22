declare function setTimeout(
  callback: (...args: unknown[]) => void,
  delay?: number,
): unknown;

declare function clearTimeout(timeoutId: unknown): void;
