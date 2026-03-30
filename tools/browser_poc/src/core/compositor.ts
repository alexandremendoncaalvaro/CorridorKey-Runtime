import {
  CHECKER_DARK,
  CHECKER_LIGHT,
  CHECKER_TILE_SIZE,
} from "../common/constants";
import { clamp_unit } from "../common/math";
import type { ProcessResult, RgbaFrame } from "./image_types";

function resolve_checker_tile_width(
  frame: RgbaFrame,
  result: ProcessResult,
): number {
  const display_width = frame.display_width ?? result.width;
  const display_height = frame.display_height ?? result.height;
  if (
    display_width <= 0 ||
    display_height <= 0 ||
    result.width <= 0 ||
    result.height <= 0
  ) {
    return CHECKER_TILE_SIZE;
  }

  const corrected_tile_width =
    CHECKER_TILE_SIZE *
    ((display_height * result.width) / (display_width * result.height));

  return Math.max(1, corrected_tile_width);
}

export function composite_over_checker(
  frame: RgbaFrame,
  result: ProcessResult,
): RgbaFrame {
  const output = new Uint8ClampedArray(result.width * result.height * 4);
  const plane_size = result.width * result.height;
  const checker_tile_width = resolve_checker_tile_width(frame, result);

  for (let index = 0; index < plane_size; index += 1) {
    const y = Math.floor(index / result.width);
    const x = index % result.width;
    const rgba_index = index * 4;
    const alpha = clamp_unit(result.alpha[index]);
    const is_dark =
      (Math.floor(y / CHECKER_TILE_SIZE) +
        Math.floor(x / checker_tile_width)) %
        2 ===
      0;
    const checker = is_dark ? CHECKER_DARK : CHECKER_LIGHT;

    const foreground_red =
      result.foreground === null
        ? frame.data[rgba_index]
        : clamp_unit(result.foreground[index]) * 255;
    const foreground_green =
      result.foreground === null
        ? frame.data[rgba_index + 1]
        : clamp_unit(result.foreground[plane_size + index]) * 255;
    const foreground_blue =
      result.foreground === null
        ? frame.data[rgba_index + 2]
        : clamp_unit(result.foreground[plane_size * 2 + index]) * 255;

    output[rgba_index] = Math.round(
      foreground_red * alpha + checker * (1 - alpha),
    );
    output[rgba_index + 1] = Math.round(
      foreground_green * alpha + checker * (1 - alpha),
    );
    output[rgba_index + 2] = Math.round(
      foreground_blue * alpha + checker * (1 - alpha),
    );
    output[rgba_index + 3] = 255;
  }

  return {
    width: result.width,
    height: result.height,
    data: output,
    display_width: frame.display_width,
    display_height: frame.display_height,
  };
}
