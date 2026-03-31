import { describe, expect, it } from "vitest";
import type { ProcessResult, RgbaFrame } from "./image_types";
import { apply_post_process } from "./post_process";

describe("apply_post_process", () => {
  it("creates a fallback foreground from the source frame and despills it", () => {
    const frame: RgbaFrame = {
      width: 1,
      height: 1,
      data: new Uint8ClampedArray([32, 220, 16, 255]),
    };
    const result: ProcessResult = {
      alpha: new Float32Array([1]),
      foreground: null,
      width: 1,
      height: 1,
      mode: "rough_matte",
    };

    const output = apply_post_process(frame, result);

    expect(output.foreground).not.toBeNull();
    expect(output.foreground?.[1]).toBeLessThan(frame.data[1] / 255);
  });
});
