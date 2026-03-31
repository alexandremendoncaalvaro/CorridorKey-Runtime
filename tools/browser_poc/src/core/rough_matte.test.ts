import { describe, expect, it } from "vitest";
import type { RgbaFrame } from "./image_types";
import { create_rough_matte } from "./rough_matte";

function make_frame(data: number[]): RgbaFrame {
  return {
    width: 2,
    height: 1,
    data: new Uint8ClampedArray(data),
  };
}

describe("create_rough_matte", () => {
  it("suppresses clear green pixels and preserves non-green pixels", () => {
    const frame = make_frame([0, 255, 0, 255, 255, 0, 0, 255]);
    const matte = create_rough_matte(frame);

    expect(matte[0]).toBeCloseTo(0, 5);
    expect(matte[1]).toBeCloseTo(1, 5);
  });
});
