import { DEFAULT_TARGET_RESOLUTION } from "./constants";

export type ResolutionPresetId =
  | "auto"
  | "draft_512"
  | "high_1024";

export interface ProcessingResolutionPreset {
  id: ResolutionPresetId;
  label: string;
  description: string;
  resolution: number | null;
}

export const DEFAULT_RESOLUTION_PRESET_ID: ResolutionPresetId = "auto";

export const PROCESSING_RESOLUTION_PRESETS: readonly ProcessingResolutionPreset[] = [
  {
    id: "auto",
    label: "Auto",
    description: "Follow the active browser model resolution.",
    resolution: null,
  },
  {
    id: "draft_512",
    label: "Draft (512)",
    description: "Fastest browser preset for quick review passes.",
    resolution: 512,
  },
  {
    id: "high_1024",
    label: "High (1024)",
    description: "Highest fixed browser preset exposed in the main flow.",
    resolution: 1024,
  },
];

export function normalize_resolution_preset_id(
  preset_id: string,
): ResolutionPresetId {
  const matching_preset = PROCESSING_RESOLUTION_PRESETS.find(
    (preset) => preset.id === preset_id,
  );

  return matching_preset?.id ?? DEFAULT_RESOLUTION_PRESET_ID;
}

export function resolve_target_resolution(
  preset_id: string,
  auto_resolution: number,
): number {
  const normalized_preset_id = normalize_resolution_preset_id(preset_id);
  const preset =
    PROCESSING_RESOLUTION_PRESETS.find(
      (candidate) => candidate.id === normalized_preset_id,
    ) ?? PROCESSING_RESOLUTION_PRESETS[0];

  if (preset === undefined || preset.resolution === null) {
    return auto_resolution;
  }

  return preset.resolution;
}

export function resolution_preset_detail(
  preset_id: string,
  auto_resolution: number,
): string {
  const normalized_preset_id = normalize_resolution_preset_id(preset_id);

  if (normalized_preset_id === "auto") {
    return `Auto currently follows the active browser model at ${auto_resolution}.`;
  }

  const resolved_resolution = resolve_target_resolution(
    normalized_preset_id,
    auto_resolution,
  );

  return `Pinned to ${resolved_resolution} for the current browser processing pass.`;
}

export function fallback_auto_resolution(): number {
  return DEFAULT_TARGET_RESOLUTION;
}
