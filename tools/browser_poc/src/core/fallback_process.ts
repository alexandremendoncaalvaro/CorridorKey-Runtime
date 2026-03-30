import { create_rough_matte } from "./rough_matte";
import type { ProcessResult, RgbaFrame } from "./image_types";

export function create_fallback_process_result(frame: RgbaFrame): ProcessResult {
  return {
    alpha: create_rough_matte(frame),
    foreground: null,
    width: frame.width,
    height: frame.height,
    mode: "rough_matte",
  };
}
