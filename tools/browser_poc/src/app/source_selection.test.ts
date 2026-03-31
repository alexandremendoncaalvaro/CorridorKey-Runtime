import { describe, expect, it } from "vitest";
import { classify_source_files } from "./source_selection";

function file(name: string, type: string) {
  return { name, type };
}

describe("classify_source_files", () => {
  it("classifies a single video file as video", () => {
    const result = classify_source_files([file("plate.mov", "video/quicktime")]);

    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.kind).toBe("video");
  });

  it("classifies a single image file as image", () => {
    const result = classify_source_files([file("plate.png", "image/png")]);

    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.kind).toBe("image");
  });

  it("classifies multiple images as a naturally sorted sequence", () => {
    const result = classify_source_files([
      file("frame_10.png", "image/png"),
      file("frame_2.png", "image/png"),
      file("frame_1.png", "image/png"),
    ]);

    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.kind).toBe("sequence");
    expect(result.value.files.map((entry) => entry.name)).toEqual([
      "frame_1.png",
      "frame_2.png",
      "frame_10.png",
    ]);
  });

  it("rejects mixed image and video selections", () => {
    const result = classify_source_files([
      file("plate.mov", "video/quicktime"),
      file("frame_1.png", "image/png"),
    ]);

    expect(result.ok).toBe(false);
    if (result.ok) {
      return;
    }

    expect(result.error.code).toBe("source_load_failed");
  });
});
