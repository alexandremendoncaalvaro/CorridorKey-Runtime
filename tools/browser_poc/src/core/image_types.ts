import type { AppError } from "../common/errors";
import type { Result } from "../common/result";

export type ProcessMode = "rough_matte" | "model";

export type InferenceStrategy =
  | { type: "singlepass" }
  | { type: "tiling", tile_size: number, overlap: number };

export type InputExtractionStrategy =
  | { type: "squash"; size: number }
  | { type: "native_bounded"; max_size: number };

export interface TileProgress {
  current_tile: number;
  total_tiles: number;
}

export interface RgbaFrame {
  width: number;
  height: number;
  data: Uint8ClampedArray;
  display_width?: number;
  display_height?: number;
}

export interface ProcessResult {
  alpha: Float32Array;
  foreground: Float32Array | null;
  width: number;
  height: number;
  mode: ProcessMode;
}

export interface ProcessedFrame {
  output_frame: RgbaFrame;
  result: ProcessResult;
  source_label: string;
  backend_label: string;
}

export interface ModelSession {
  backend_label: string;
  run_frame(
    frame: RgbaFrame,
    alpha_hint?: Float32Array,
    strategy?: InferenceStrategy,
    on_progress?: (progress: TileProgress) => void
  ): Promise<Result<ProcessResult, AppError>>;
  dispose(): void;
}

export interface BrowserModelDefinition {
  id: string;
  filename: string;
  label: string;
  description: string;
  resolution: number;
  size_label: string;
  url: string;
}
