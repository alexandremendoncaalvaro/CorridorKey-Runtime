import type { RgbaFrame } from "./image_types";

export function prepare_model_input(
  frame: RgbaFrame,
  alpha_hint: Float32Array,
): Float32Array {
  const plane_size = frame.width * frame.height;
  const tensor = new Float32Array(plane_size * 4);

  for (let index = 0; index < plane_size; index += 1) {
    const rgba_index = index * 4;
    const red = frame.data[rgba_index] / 255;
    const green = frame.data[rgba_index + 1] / 255;
    const blue = frame.data[rgba_index + 2] / 255;

    tensor[index] = (red - 0.485) / 0.229;
    tensor[plane_size + index] = (green - 0.456) / 0.224;
    tensor[plane_size * 2 + index] = (blue - 0.406) / 0.225;
    tensor[plane_size * 3 + index] = alpha_hint[index];
  }

  return tensor;
}
