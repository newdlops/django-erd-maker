export interface DiagramInteractionSettings {
  edgeDetour: number;
  nodeSpacing: number;
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
    hint: "How aggressively nodes push away from each other after layout. Applies after Refresh.",
    key: "nodeSpacing",
    label: "Node Spacing",
    max: 2.4,
    min: 0.8,
    step: 0.05,
  },
  {
    hint: "How far edges detour around tables and tangled lanes. Applies after Refresh.",
    key: "edgeDetour",
    label: "Edge Detour",
    max: 2.5,
    min: 0.8,
    step: 0.05,
  },
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
  edgeDetour: 1.35,
  nodeSpacing: 1.4,
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
  key: DiagramInteractionSettingKey,
  value: number,
): string {
  if (key === "nodeSpacing" || key === "edgeDetour") {
    return `${value.toFixed(2)}x`;
  }

  return `${Math.round(value * 100)}%`;
}

export function normalizeInteractionSettings(
  value?: Partial<DiagramInteractionSettings>,
): DiagramInteractionSettings {
  return {
    edgeDetour: clampInteractionSetting(
      "edgeDetour",
      value?.edgeDetour ?? DEFAULT_INTERACTION_SETTINGS.edgeDetour,
    ),
    nodeSpacing: clampInteractionSetting(
      "nodeSpacing",
      value?.nodeSpacing ?? DEFAULT_INTERACTION_SETTINGS.nodeSpacing,
    ),
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
