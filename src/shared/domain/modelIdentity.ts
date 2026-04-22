export type ModelId = `${string}.${string}`;

export interface ModelIdentity {
  appLabel: string;
  id: ModelId;
  modelName: string;
  modulePath?: string;
}

export function makeModelId(appLabel: string, modelName: string): ModelId {
  return `${appLabel}.${modelName}` as ModelId;
}

export function isModelId(value: string): value is ModelId {
  const segments = value.split(".");

  return segments.length === 2 && segments.every((segment) => segment.length > 0);
}
