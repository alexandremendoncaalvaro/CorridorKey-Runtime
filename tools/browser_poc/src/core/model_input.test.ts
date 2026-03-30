import { describe, expect, it } from "vitest";
import type { RgbaFrame } from "./image_types";
import { prepare_model_input } from "./model_input";

const frame: RgbaFrame = {
  width: 1,
  height: 1,
  data: new Uint8ClampedArray([255, 128, 64, 255]),
};

describe("prepare_model_input", () => {
  it("writes planar normalized rgb plus the hint channel", () => {
    const tensor = prepare_model_input(frame, new Float32Array([0.25]));

    expect(tensor).toHaveLength(4);
    expect(tensor[0]).toBeCloseTo((1 - 0.485) / 0.229, 5);
    expect(tensor[1]).toBeCloseTo(((128 / 255) - 0.456) / 0.224, 5);
    expect(tensor[2]).toBeCloseTo(((64 / 255) - 0.406) / 0.225, 5);
    expect(tensor[3]).toBeCloseTo(0.25, 5);
  });
});
