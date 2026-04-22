export interface DiagramInteractionSettings {
  panSpeed: number;
  zoomSpeed: number;
}

export type DiagramInteractionSettingKey = keyof DiagramInteractionSettings;

export interface DiagramInteractionSettingDescriptor {
  hint: string;
  key: DiagramInteractionSettingKey;
  label: string;
  max: number;
  min: number;
  step: number;
}

export const INTERACTION_SETTING_DESCRIPTORS: readonly DiagramInteractionSettingDescriptor[] = [
  {
    hint: "Canvas drag sensitivity.",
    key: "panSpeed",
    label: "Pan Speed",
    max: 1.5,
    min: 0.25,
    step: 0.05,
  },
  {
    hint: "Wheel and toolbar zoom sensitivity.",
    key: "zoomSpeed",
    label: "Zoom Speed",
    max: 1.5,
    min: 0.05,
    step: 0.01,
  },
] as const;

export const DEFAULT_INTERACTION_SETTINGS: DiagramInteractionSettings = {
  panSpeed: 0.7,
  zoomSpeed: 0.6,
};

const INTERACTION_SETTING_LIMITS = new Map(
  INTERACTION_SETTING_DESCRIPTORS.map((descriptor) => [descriptor.key, descriptor] as const),
);

export function clampInteractionSetting(
  key: DiagramInteractionSettingKey,
  value: number,
): number {
  const descriptor = INTERACTION_SETTING_LIMITS.get(key);
  const fallback = DEFAULT_INTERACTION_SETTINGS[key];
  if (!descriptor || !Number.isFinite(value)) {
    return fallback;
  }

  return roundToStep(
    Math.max(descriptor.min, Math.min(descriptor.max, value)),
    descriptor.step,
  );
}

export function formatInteractionSettingValue(
  value: number,
): string {
  return `${Math.round(value * 100)}%`;
}

export function normalizeInteractionSettings(
  value?: Partial<DiagramInteractionSettings>,
): DiagramInteractionSettings {
  return {
    panSpeed: clampInteractionSetting(
      "panSpeed",
      value?.panSpeed ?? DEFAULT_INTERACTION_SETTINGS.panSpeed,
    ),
    zoomSpeed: clampInteractionSetting(
      "zoomSpeed",
      value?.zoomSpeed ?? DEFAULT_INTERACTION_SETTINGS.zoomSpeed,
    ),
  };
}

function roundToStep(value: number, step: number): number {
  return Math.round(Math.round(value / step) * step * 100) / 100;
}
