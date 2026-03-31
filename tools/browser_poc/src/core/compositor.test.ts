import { describe, expect, it } from "vitest";
import { composite_over_checker } from "./compositor";
import type { ProcessResult, RgbaFrame } from "./image_types";

function make_frame(
  width: number,
  height: number,
  data: number[],
  display_width?: number,
  display_height?: number,
): RgbaFrame {
  return {
    width,
    height,
    data: new Uint8ClampedArray(data),
    display_width,
    display_height,
  };
}

function red_at(frame: RgbaFrame, x: number, y: number): number {
  return frame.data[(y * frame.width + x) * 4] ?? -1;
}

describe("composite_over_checker", () => {
  it("returns the source pixel when alpha is fully opaque", () => {
    const source_frame = make_frame(1, 1, [200, 10, 20, 255]);
    const result: ProcessResult = {
      alpha: new Float32Array([1]),
      foreground: null,
      width: 1,
      height: 1,
      mode: "rough_matte",
    };

    const output = composite_over_checker(source_frame, result);

    expect(output.data[0]).toBe(200);
    expect(output.data[1]).toBe(10);
    expect(output.data[2]).toBe(20);
    expect(output.data[3]).toBe(255);
  });

  it("returns the checker pixel when alpha is fully transparent", () => {
    const source_frame = make_frame(1, 1, [200, 10, 20, 255]);
    const result: ProcessResult = {
      alpha: new Float32Array([0]),
      foreground: null,
      width: 1,
      height: 1,
      mode: "rough_matte",
    };

    const output = composite_over_checker(source_frame, result);

    expect(output.data[0]).toBe(42);
    expect(output.data[1]).toBe(42);
    expect(output.data[2]).toBe(42);
    expect(output.data[3]).toBe(255);
  });

  it("compensates the checker width for wide display canvases", () => {
    const source_frame = make_frame(
      32,
      32,
      new Array(32 * 32 * 4).fill(0),
      64,
      32,
    );
    const result: ProcessResult = {
      alpha: new Float32Array(32 * 32),
      foreground: null,
      width: 32,
      height: 32,
      mode: "rough_matte",
    };

    const output = composite_over_checker(source_frame, result);

    expect(red_at(output, 0, 0)).toBe(42);
    expect(red_at(output, 8, 0)).toBe(138);
    expect(red_at(output, 15, 0)).toBe(138);
    expect(red_at(output, 16, 0)).toBe(42);
  });
});
