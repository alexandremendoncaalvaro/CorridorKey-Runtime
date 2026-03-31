import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { ok } from "../common/result";
import { BrowserPocController } from "./browser_poc_controller";
import type { ViewHandlers } from "../ui/dom_browser_poc_view";
import type { ProcessedFrame, RgbaFrame } from "../core/image_types";

const mocks = vi.hoisted(() => ({
  model_loader_load: vi.fn(),
  detect_browser_export_capability: vi.fn(),
}));

const fake_session = {
  backend_label: "webgpu",
  run_frame: vi.fn(),
  dispose: vi.fn(),
};

vi.mock("./browser_model_loader", () => ({
  BrowserModelLoader: class {
    load = mocks.model_loader_load;
  },
}));

vi.mock("../common/browser_export_capability", () => ({
  detect_browser_export_capability: mocks.detect_browser_export_capability,
}));

function flush_async_work(): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
}

function create_video_file(name: string): File {
  return {
    name,
    type: "video/mp4",
  } as File;
}

function create_processed_frame(): ProcessedFrame {
  const frame: RgbaFrame = {
    width: 2,
    height: 2,
    data: new Uint8ClampedArray(16),
    display_width: 2,
    display_height: 2,
  };

  return {
    output_frame: frame,
    result: {
      alpha: new Float32Array(4),
      foreground: null,
      width: 2,
      height: 2,
      mode: "model",
    },
    source_label: "onnxruntime-web",
    backend_label: "webgpu",
  };
}

class FakeView {
  handlers: ViewHandlers | null = null;
  source_video_files: File[] = [];
  source_still_files: File[] = [];
  hint_files: File[] = [];
  status_messages: string[] = [];
  loaded_hint_urls: string[] = [];
  loaded_source_urls: string[] = [];
  clear_hint_video_source_calls = 0;
  clear_video_source_calls = 0;
  video_has_metadata_value = false;

  bind_handlers(handlers: ViewHandlers): void {
    this.handlers = handlers;
  }

  selected_source_video_files(): File[] {
    return this.source_video_files;
  }

  selected_source_still_files(): File[] {
    return this.source_still_files;
  }

  selected_hint_files(): File[] {
    return this.hint_files;
  }

  selected_resolution_preset_id(): string {
    return "auto";
  }

  selected_model_id(): string {
    return "corridorkey_int8_512";
  }

  set_model_catalog(): void {}

  set_resolution_presets(): void {}

  reset_stage_aspect_ratio(): void {}

  set_sequence_state(): void {}

  set_hint_sequence_state(): void {}

  set_auto_target_resolution(): void {}

  set_resolution_detail(): void {}

  sync_stage_resolution(): void {}

  set_processing_state(): void {}

  set_status(message: string): void {
    this.status_messages.push(message);
  }

  set_metrics(): void {}

  set_model_state(): void {}

  set_download_artifacts(): void {}

  set_export_capability(): void {}

  set_button_state(): void {}

  load_video_source(url: string): void {
    this.loaded_source_urls.push(url);
  }

  clear_video_source(): void {
    this.clear_video_source_calls += 1;
  }

  load_hint_video_source(url: string): void {
    this.loaded_hint_urls.push(url);
  }

  clear_hint_video_source(): void {
    this.clear_hint_video_source_calls += 1;
  }

  video_has_metadata(): boolean {
    return this.video_has_metadata_value;
  }

  draw_source_video_frame() {
    return ok({
      width: 2,
      height: 2,
      data: new Uint8ClampedArray(16),
      display_width: 2,
      display_height: 2,
    });
  }

  draw_hint_video_frame() {
    throw new Error("draw_hint_video_frame should not run before the hint video is ready.");
  }
}

function create_processing_service() {
  let session: typeof fake_session | null = null;

  return {
    process_frame: vi.fn(async () => ok(create_processed_frame())),
    set_model_session(model_session: typeof fake_session | null) {
      session = model_session;
    },
    clear_model_session() {
      session = null;
    },
    has_model_session() {
      return session !== null;
    },
    current_backend_label() {
      return session?.backend_label ?? "fallback";
    },
  };
}

describe("BrowserPocController", () => {
  beforeEach(() => {
    mocks.model_loader_load.mockReset();
    mocks.model_loader_load.mockResolvedValue(ok(fake_session));
    mocks.detect_browser_export_capability.mockReset();
    mocks.detect_browser_export_capability.mockResolvedValue({
      supported: true,
      preview_format: "mp4",
      codec: "avc",
      reason: null,
    });
    Object.defineProperty(globalThis, "window", {
      configurable: true,
      value: { addEventListener: vi.fn() },
    });
    Object.defineProperty(URL, "createObjectURL", {
      configurable: true,
      value: vi.fn(() => "blob:mock"),
    });
    Object.defineProperty(URL, "revokeObjectURL", {
      configurable: true,
      value: vi.fn(),
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("keeps the hint selection intact when the source video changes", async () => {
    const view = new FakeView();
    const processing_service = create_processing_service();
    new BrowserPocController(view as never, processing_service as never);
    await flush_async_work();

    view.source_video_files = [create_video_file("plate_a.mp4")];
    view.handlers?.on_source_video_selected();
    await flush_async_work();

    view.hint_files = [create_video_file("hint.mp4")];
    view.handlers?.on_hint_files_selected();
    await flush_async_work();

    view.source_video_files = [create_video_file("plate_b.mp4")];
    view.handlers?.on_source_video_selected();
    await flush_async_work();

    expect(view.loaded_hint_urls).toHaveLength(1);
    expect(view.clear_hint_video_source_calls).toBe(0);
  });

  it("waits for the hint video to become ready before refreshing the preview", async () => {
    const view = new FakeView();
    view.video_has_metadata_value = true;
    const processing_service = create_processing_service();
    new BrowserPocController(view as never, processing_service as never);
    await flush_async_work();

    view.source_video_files = [create_video_file("plate.mp4")];
    view.handlers?.on_source_video_selected();
    await flush_async_work();

    view.hint_files = [create_video_file("hint.mp4")];
    view.handlers?.on_hint_files_selected();
    await flush_async_work();

    expect(processing_service.process_frame).not.toHaveBeenCalled();
    expect(view.status_messages.at(-1)).toBe("Loaded hint video hint.mp4.");
  });
});
