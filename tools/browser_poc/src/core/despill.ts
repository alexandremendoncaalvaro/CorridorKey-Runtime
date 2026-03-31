import { DEFAULT_DESPILL_STRENGTH } from "../common/constants";
import { clamp_unit } from "../common/math";

export function despill_foreground(
  foreground: Float32Array,
  width: number,
  height: number,
  strength = DEFAULT_DESPILL_STRENGTH,
): Float32Array {
  if (strength <= 0) {
    return foreground.slice();
  }

  const plane_size = width * height;
  const output = foreground.slice();

  for (let index = 0; index < plane_size; index += 1) {
    const red = output[index];
    const green = output[plane_size + index];
    const blue = output[plane_size * 2 + index];
    const limit = (red + blue) * 0.5;
    const spill = Math.max(0, green - limit);

    if (spill <= 0) {
      continue;
    }

    const effective_spill = spill * strength;
    output[plane_size + index] = clamp_unit(green - effective_spill);
    output[index] = clamp_unit(red + effective_spill * 0.5);
    output[plane_size * 2 + index] = clamp_unit(blue + effective_spill * 0.5);
  }

  return output;
}
