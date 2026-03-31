import { describe, expect, it } from "vitest";
import {
  create_alpha_archive_filename,
  create_alpha_frame_filename,
  create_alpha_png_filename,
  create_preview_png_filename,
  create_preview_video_filename,
  file_stem,
} from "./browser_export_artifacts";

describe("browser_export_artifacts", () => {
  it("preserves the base stem of regular filenames", () => {
    expect(file_stem("plate.mov")).toBe("plate");
    expect(file_stem("plate.version01.mov")).toBe("plate.version01");
    expect(file_stem("plate")).toBe("plate");
  });

  it("creates coherent preview and alpha filenames", () => {
    expect(create_preview_video_filename("plate.mov", "mp4")).toBe(
      "plate_corridorkey_preview.mp4",
    );
    expect(create_alpha_archive_filename("plate.mov")).toBe(
      "plate_corridorkey_alpha.zip",
    );
    expect(create_preview_png_filename("plate.png")).toBe(
      "plate_corridorkey_preview.png",
    );
    expect(create_alpha_png_filename("plate.png")).toBe(
      "plate_corridorkey_alpha.png",
    );
  });

  it("uses stable zero-padded alpha frame names", () => {
    expect(create_alpha_frame_filename(0)).toBe("alpha_000001.png");
    expect(create_alpha_frame_filename(14)).toBe("alpha_000015.png");
    expect(create_alpha_frame_filename(999)).toBe("alpha_001000.png");
  });
});
