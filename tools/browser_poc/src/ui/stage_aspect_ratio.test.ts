import { describe, expect, it } from "vitest";
import {
  DEFAULT_STAGE_CANVAS_ASPECT_RATIO,
  resolve_stage_canvas_dimensions,
  resolve_stage_draw_rect,
  resolve_stage_aspect_ratio,
} from "./stage_aspect_ratio";

describe("resolve_stage_aspect_ratio", () => {
  it("preserves the loaded media dimensions in the aspect ratio string", () => {
    expect(resolve_stage_aspect_ratio(1920, 1080)).toBe("1920 / 1080");
  });

  it("falls back when dimensions are invalid", () => {
    expect(resolve_stage_aspect_ratio(0, 0)).toBe(
      DEFAULT_STAGE_CANVAS_ASPECT_RATIO,
    );
    expect(resolve_stage_aspect_ratio(Number.NaN, 1080)).toBe(
      DEFAULT_STAGE_CANVAS_ASPECT_RATIO,
    );
  });

  it("fits wide media inside the target resolution box", () => {
    expect(resolve_stage_canvas_dimensions(512, 1920, 1080)).toEqual({
      width: 512,
      height: 288,
    });
  });

  it("fits tall media inside the target resolution box", () => {
    expect(resolve_stage_canvas_dimensions(512, 1080, 1920)).toEqual({
      width: 288,
      height: 512,
    });
  });

  it("falls back to square canvas dimensions when the aspect ratio is invalid", () => {
    expect(resolve_stage_canvas_dimensions(512, 0, 0)).toEqual({
      width: 512,
      height: 512,
    });
  });

  it("contains wide media inside a square stage", () => {
    expect(resolve_stage_draw_rect(512, 512, 1920, 1080)).toEqual({
      x: 0,
      y: 112,
      width: 512,
      height: 288,
    });
  });

  it("contains tall media inside a square stage", () => {
    expect(resolve_stage_draw_rect(512, 512, 1080, 1920)).toEqual({
      x: 112,
      y: 0,
      width: 288,
      height: 512,
    });
  });
});
