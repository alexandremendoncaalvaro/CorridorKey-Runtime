import { describe, expect, it } from "vitest";
import { detect_browser_export_capability } from "./browser_export_capability";

describe("browser_export_capability", () => {
  it("blocks iPhone WebKit for full export", async () => {
    const capability = await detect_browser_export_capability({
      user_agent:
        "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1",
      has_video_decoder: true,
      has_video_encoder: true,
      resolve_codec: async () => "avc",
    });

    expect(capability.supported).toBe(false);
    expect(capability.reason).toContain("iPhone");
  });

  it("blocks iPhone Chrome too because it still runs on iOS browser constraints", async () => {
    const capability = await detect_browser_export_capability({
      user_agent:
        "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 CriOS/135.0.7049.83 Mobile/15E148 Safari/604.1",
      has_video_decoder: true,
      has_video_encoder: true,
      resolve_codec: async () => "avc",
    });

    expect(capability.supported).toBe(false);
    expect(capability.reason).toContain("iPhone");
  });

  it("prefers mp4 when avc encoding is available", async () => {
    const capability = await detect_browser_export_capability({
      user_agent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/135.0",
      has_video_decoder: true,
      has_video_encoder: true,
      resolve_codec: async (codecs) => (codecs[0] === "avc" ? "avc" : null),
    });

    expect(capability.supported).toBe(true);
    expect(capability.preview_format).toBe("mp4");
    expect(capability.codec).toBe("avc");
  });

  it("falls back to webm when only vp9 is available", async () => {
    const capability = await detect_browser_export_capability({
      user_agent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/135.0",
      has_video_decoder: true,
      has_video_encoder: true,
      resolve_codec: async (codecs) => (codecs.includes("vp9") ? "vp9" : null),
    });

    expect(capability.supported).toBe(true);
    expect(capability.preview_format).toBe("webm");
    expect(capability.codec).toBe("vp9");
  });

  it("fails clearly when encode or decode APIs are missing", async () => {
    const capability = await detect_browser_export_capability({
      user_agent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/135.0",
      has_video_decoder: false,
      has_video_encoder: true,
    });

    expect(capability.supported).toBe(false);
    expect(capability.reason).toContain("encode and decode APIs");
  });
});
