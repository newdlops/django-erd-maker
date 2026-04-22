export interface Logger {
  error(message: string, error?: unknown): void;
  info(message: string): void;
  warn(message: string): void;
}
