import { describe, expect, it } from "vitest";
import {
  normalize_resolution_preset_id,
  resolution_preset_detail,
  resolve_target_resolution,
} from "./processing_resolution";

describe("processing_resolution", () => {
  it("uses the active model resolution when the preset is auto", () => {
    expect(resolve_target_resolution("auto", 768)).toBe(768);
  });

  it("pins the target size for fixed presets", () => {
    expect(resolve_target_resolution("draft_512", 1024)).toBe(512);
    expect(resolve_target_resolution("high_1024", 512)).toBe(1024);
  });

  it("falls back to auto for unknown preset ids", () => {
    expect(normalize_resolution_preset_id("unknown")).toBe("auto");
    expect(resolve_target_resolution("unknown", 512)).toBe(512);
  });

  it("describes the current auto resolution clearly", () => {
    expect(resolution_preset_detail("auto", 768)).toContain("768");
    expect(resolution_preset_detail("draft_512", 1024)).toContain("512");
  });
});
