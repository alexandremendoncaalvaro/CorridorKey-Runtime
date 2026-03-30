import { getFirstEncodableVideoCodec, type VideoCodec } from "mediabunny";

export interface BrowserExportCapability {
  supported: boolean;
  preview_format: "mp4" | "webm" | null;
  codec: VideoCodec | null;
  reason: string | null;
}

export interface BrowserExportCapabilityDependencies {
  user_agent?: string;
  has_video_encoder?: boolean;
  has_video_decoder?: boolean;
  resolve_codec?: (codecs: VideoCodec[]) => Promise<VideoCodec | null>;
}

function looks_like_ios_browser(user_agent: string): boolean {
  const normalized = user_agent.toLowerCase();
  return (
    normalized.includes("iphone") ||
    normalized.includes("ipad") ||
    normalized.includes("ipod")
  );
}

export async function detect_browser_export_capability(
  dependencies: BrowserExportCapabilityDependencies = {},
): Promise<BrowserExportCapability> {
  const user_agent =
    dependencies.user_agent ??
    (typeof navigator === "undefined" ? "" : navigator.userAgent);
  if (looks_like_ios_browser(user_agent)) {
    return {
      supported: false,
      preview_format: null,
      codec: null,
      reason:
        "Full export is desktop Chromium-first in this phase. iPhone remains preview-only.",
    };
  }

  const has_video_encoder =
    dependencies.has_video_encoder ?? typeof VideoEncoder !== "undefined";
  const has_video_decoder =
    dependencies.has_video_decoder ?? typeof VideoDecoder !== "undefined";
  if (!has_video_encoder || !has_video_decoder) {
    return {
      supported: false,
      preview_format: null,
      codec: null,
      reason:
        "This browser does not expose the video encode and decode APIs required for full export.",
    };
  }

  const resolve_codec =
    dependencies.resolve_codec ??
    (async (codecs: VideoCodec[]) => getFirstEncodableVideoCodec(codecs));

  const mp4_codec = await resolve_codec(["avc"]);
  if (mp4_codec !== null) {
    return {
      supported: true,
      preview_format: "mp4",
      codec: mp4_codec,
      reason: null,
    };
  }

  const webm_codec = await resolve_codec(["vp9", "vp8"]);
  if (webm_codec !== null) {
    return {
      supported: true,
      preview_format: "webm",
      codec: webm_codec,
      reason: null,
    };
  }

  return {
    supported: false,
    preview_format: null,
    codec: null,
    reason:
      "This browser cannot encode a supported preview video format for full export.",
  };
}
