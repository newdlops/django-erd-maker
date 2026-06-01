declare function setTimeout(
  callback: (...args: unknown[]) => void,
  delay?: number,
): unknown;

declare function clearTimeout(timeoutId: unknown): void;

declare function setInterval(
  callback: (...args: unknown[]) => void,
  delay?: number,
): unknown;

declare function clearInterval(intervalId: unknown): void;
