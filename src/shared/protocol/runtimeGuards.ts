import { isModelId, type ModelId } from "../domain/modelIdentity";

export type JsonRecord = Record<string, unknown>;

export function readArray(
  record: JsonRecord,
  key: string,
  context: string,
): unknown[] {
  const value = record[key];

  if (!Array.isArray(value)) {
    throw new Error(`${context}.${key} must be an array.`);
  }

  return value;
}

export function readBoolean(
  record: JsonRecord,
  key: string,
  context: string,
): boolean {
  const value = record[key];

  if (typeof value !== "boolean") {
    throw new Error(`${context}.${key} must be a boolean.`);
  }

  return value;
}

export function readLiteral<T extends string>(
  record: JsonRecord,
  key: string,
  allowed: readonly T[],
  context: string,
): T {
  const value = readString(record, key, context);

  if (!allowed.includes(value as T)) {
    throw new Error(
      `${context}.${key} must be one of: ${allowed.join(", ")}.`,
    );
  }

  return value as T;
}

export function readModelId(
  record: JsonRecord,
  key: string,
  context: string,
): ModelId {
  const value = readString(record, key, context);

  if (!isModelId(value)) {
    throw new Error(`${context}.${key} must be a canonical model ID.`);
  }

  return value;
}

export function readNumber(
  record: JsonRecord,
  key: string,
  context: string,
): number {
  const value = record[key];

  if (typeof value !== "number" || Number.isNaN(value)) {
    throw new Error(`${context}.${key} must be a number.`);
  }

  return value;
}

export function readOptionalBoolean(
  record: JsonRecord,
  key: string,
  context: string,
): boolean | undefined {
  const value = record[key];

  if (value === undefined || value === null) {
    return undefined;
  }

  if (typeof value !== "boolean") {
    throw new Error(`${context}.${key} must be a boolean when present.`);
  }

  return value;
}

export function readOptionalModelId(
  record: JsonRecord,
  key: string,
  context: string,
): ModelId | undefined {
  const value = record[key];

  if (value === undefined || value === null) {
    return undefined;
  }

  if (typeof value !== "string" || !isModelId(value)) {
    throw new Error(`${context}.${key} must be a canonical model ID when present.`);
  }

  return value;
}

export function readOptionalNumber(
  record: JsonRecord,
  key: string,
  context: string,
): number | undefined {
  const value = record[key];

  if (value === undefined || value === null) {
    return undefined;
  }

  if (typeof value !== "number" || Number.isNaN(value)) {
    throw new Error(`${context}.${key} must be a number when present.`);
  }

  return value;
}

export function readOptionalObject(
  record: JsonRecord,
  key: string,
  context: string,
): JsonRecord | undefined {
  const value = record[key];

  if (value === undefined || value === null) {
    return undefined;
  }

  return readRecord(value, `${context}.${key}`);
}

export function readOptionalString(
  record: JsonRecord,
  key: string,
  context: string,
): string | undefined {
  const value = record[key];

  if (value === undefined || value === null) {
    return undefined;
  }

  if (typeof value !== "string") {
    throw new Error(`${context}.${key} must be a string when present.`);
  }

  return value;
}

export function readRecord(value: unknown, context: string): JsonRecord {
  if (!isRecord(value)) {
    throw new Error(`${context} must be an object.`);
  }

  return value;
}

export function readString(
  record: JsonRecord,
  key: string,
  context: string,
): string {
  const value = record[key];

  if (typeof value !== "string") {
    throw new Error(`${context}.${key} must be a string.`);
  }

  return value;
}

export function readStringArray(
  record: JsonRecord,
  key: string,
  context: string,
): string[] {
  return readArray(record, key, context).map((item, index) => {
    if (typeof item !== "string") {
      throw new Error(`${context}.${key}[${index}] must be a string.`);
    }

    return item;
  });
}

function isRecord(value: unknown): value is JsonRecord {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
