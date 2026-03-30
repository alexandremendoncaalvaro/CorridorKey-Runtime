import { describe, expect, it, vi } from "vitest";
import {
  create_alpha_archive_filename,
  create_preview_video_filename,
} from "../common/browser_export_artifacts";
import { initialize_video_output } from "./full_media_export_service";

describe("FullMediaExportService helpers", () => {
  it("keeps preview and alpha export naming coherent", () => {
    expect(create_preview_video_filename("plate.mov", "mp4")).toBe(
      "plate_corridorkey_preview.mp4",
    );
    expect(create_alpha_archive_filename("plate.mov")).toBe(
      "plate_corridorkey_alpha.zip",
    );
  });

  it("starts the output before the caller begins adding video frames", async () => {
    const events: string[] = [];
    const output = {
      addVideoTrack: vi.fn(() => {
        events.push("addVideoTrack");
      }),
      start: vi.fn(async () => {
        events.push("start");
      }),
    };

    await initialize_video_output(
      output as never,
      {} as never,
      { frameRate: 24 },
    );

    expect(events).toEqual(["addVideoTrack", "start"]);
  });
});
