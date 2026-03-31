import { clamp_unit } from "../common/math";
import type { RgbaFrame } from "./image_types";

export function create_rough_matte(frame: RgbaFrame): Float32Array {
  const matte = new Float32Array(frame.width * frame.height);

  for (let index = 0; index < matte.length; index += 1) {
    const rgba_index = index * 4;
    const red = frame.data[rgba_index] / 255;
    const green = frame.data[rgba_index + 1] / 255;
    const blue = frame.data[rgba_index + 2] / 255;
    const green_bias = green - Math.max(red, blue);
    matte[index] = 1 - clamp_unit(green_bias * 2);
  }

  return matte;
}
