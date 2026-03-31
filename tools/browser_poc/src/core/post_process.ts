import { despill_foreground } from "./despill";
import type { ProcessResult, RgbaFrame } from "./image_types";

function create_source_foreground(frame: RgbaFrame): Float32Array {
  const plane_size = frame.width * frame.height;
  const foreground = new Float32Array(plane_size * 3);

  for (let index = 0; index < plane_size; index += 1) {
    const rgba_index = index * 4;
    foreground[index] = frame.data[rgba_index] / 255;
    foreground[plane_size + index] = frame.data[rgba_index + 1] / 255;
    foreground[plane_size * 2 + index] = frame.data[rgba_index + 2] / 255;
  }

  return foreground;
}

export function apply_post_process(
  frame: RgbaFrame,
  result: ProcessResult,
): ProcessResult {
  const foreground = result.foreground ?? create_source_foreground(frame);

  return {
    ...result,
    foreground: despill_foreground(foreground, result.width, result.height),
  };
}
