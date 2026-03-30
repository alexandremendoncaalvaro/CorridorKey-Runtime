import { app_error, type AppError } from "../common/errors";
import {
  DEFAULT_DOWNLOAD_FILENAME,
  MODEL_LABEL,
  PREVIEW_RECORDING_FPS,
  ROUGH_MATTE_LABEL,
  SEQUENCE_PREVIEW_FPS,
} from "../common/constants";
import {
  DEFAULT_RESOLUTION_PRESET_ID,
  PROCESSING_RESOLUTION_PRESETS,
  fallback_auto_resolution,
  resolution_preset_detail,
  resolve_target_resolution,
} from "../common/processing_resolution";
import {
  BROWSER_MODEL_CATALOG,
  DEFAULT_BROWSER_MODEL_ID,
  find_browser_model_definition,
} from "../common/browser_model_catalog";
import { err, ok, type Result } from "../common/result";
import { pick_recording_mime_type } from "../common/media";
import type { BrowserModelDefinition, ProcessMode, RgbaFrame } from "../core/image_types";
import { BrowserModelLoader, type BrowserModelLoadProgress } from "./browser_model_loader";
import {
  FrameProcessingService,
  type FrameProcessingProgress,
} from "./frame_processing_service";
import { classify_source_files } from "./source_selection";
import {
  DomBrowserPocView,
  type ViewButtonsState,
} from "../ui/dom_browser_poc_view";

type SourceState =
  | { kind: "none" }
  | { kind: "video"; file: File }
  | { kind: "webcam"; stream: MediaStream }
  | { kind: "image"; file: File; bitmap: ImageBitmap }
    | {
        kind: "sequence";
        files: File[];
        current_index: number;
        current_bitmap: ImageBitmap | null;
      };

type HintState =
  | { kind: "none" }
  | { kind: "video"; file: File; url: string }
  | { kind: "image"; file: File; bitmap: ImageBitmap }
  | {
      kind: "sequence";
      files: File[];
      current_index: number;
      current_bitmap: ImageBitmap | null;
    };

function mode_label(mode: ProcessMode): string {
  return mode === "model" ? MODEL_LABEL : ROUGH_MATTE_LABEL;
}

function safe_close_bitmap(bitmap: ImageBitmap | null): void {
  bitmap?.close();
}

function file_stem(file_name: string): string {
  const extension_index = file_name.lastIndexOf(".");
  return extension_index > 0 ? file_name.slice(0, extension_index) : file_name;
}

function format_seconds(seconds: number): string {
  const total_seconds = Math.max(0, Math.floor(seconds));
  const minutes = Math.floor(total_seconds / 60);
  const remaining_seconds = total_seconds % 60;

  return `${minutes}:${remaining_seconds.toString().padStart(2, "0")}`;
}

export class BrowserPocController {
  private m_frame_in_flight = false;
  private m_full_media_job_active = false;
  private m_preview_loop_active = false;
  private m_model_load_in_flight = false;
  private m_recorder: MediaRecorder | null = null;
  private m_recorded_chunks: Blob[] = [];
  private m_recorded_url: string | null = null;
  private m_video_url: string | null = null;
  private m_active_model_id: string | null = null;
  private m_source: SourceState = { kind: "none" };
  private m_hint: HintState = { kind: "none" };
  private m_sequence_preview_last_tick = 0;
  private readonly m_model_loader = new BrowserModelLoader();

  constructor(
    private readonly m_view: DomBrowserPocView,
    private readonly m_processing_service: FrameProcessingService,
  ) {
    this.m_view.bind_handlers({
      on_source_files_selected: () => {
        void this.handle_source_files_selected();
      },
      on_hint_files_selected: () => {
        void this.handle_hint_files_selected();
      },
      on_model_selection_changed: () => {
        this.handle_model_selection_changed();
        this.refresh_button_state();
      },
      on_selected_model_load_requested: () => {
        void this.handle_selected_model_load_requested();
      },
      on_process_frame_requested: () => {
        void this.process_current_frame();
      },
      on_process_full_media_requested: () => {
        void this.process_full_media();
      },
      on_preview_start_requested: () => {
        void this.start_preview_loop();
      },
      on_preview_stop_requested: () => {
        this.stop_preview_loop();
      },
      on_recording_start_requested: () => {
        void this.start_recording();
      },
      on_recording_stop_requested: () => {
        this.stop_recording();
      },
      on_resolution_changed: () => {
        void this.handle_resolution_changed();
      },
      on_video_metadata_loaded: () => {
        void this.handle_video_metadata_loaded();
      },
      on_video_ended: () => {
        this.handle_video_ended();
      },
      on_webcam_start_requested: () => {
        void this.handle_webcam_start_requested();
      },
      on_webcam_stop_requested: () => {
        this.handle_webcam_stop_requested();
      },
      on_sequence_index_changed: () => {
        void this.handle_sequence_index_changed();
      },
      on_hint_sequence_index_changed: () => {
        void this.handle_hint_sequence_index_changed();
      },
    });

    window.addEventListener("beforeunload", () => {
      this.dispose_resources();
    });

    this.m_view.set_model_catalog(
      BROWSER_MODEL_CATALOG,
      DEFAULT_BROWSER_MODEL_ID,
    );
    this.m_view.set_resolution_presets(
      PROCESSING_RESOLUTION_PRESETS,
      DEFAULT_RESOLUTION_PRESET_ID,
    );
    this.m_view.set_source_visual_mode("video");
    this.m_view.reset_stage_aspect_ratio();
    this.m_view.set_sequence_state({
      visible: false,
      current_index: 0,
      total_count: 1,
    });
    this.apply_resolution_state();
    this.m_view.set_processing_state({
      visible: false,
      stage_label: "idle",
      detail: "Waiting for a manual processing pass.",
      progress_ratio: 0,
    });
    this.m_view.set_status(
      "Sandbox booting. The default browser model is loading.",
    );
    this.m_view.set_metrics({
      mode: ROUGH_MATTE_LABEL,
      backend: "none",
      frame_time_ms: null,
      recording: "idle",
    });
    this.m_view.set_model_state({
      active_model_label: "bootstrapping",
      state_label: "loading",
      progress_ratio: 0,
      detail: "Preparing the default browser-safe model.",
    });
    this.m_view.set_download_link(null);
    this.refresh_button_state();
    void this.load_browser_model(DEFAULT_BROWSER_MODEL_ID, true);
  }

  private recording_state_label(): string {
    if (this.m_full_media_job_active) {
      return "exporting";
    }

    if (this.m_recorder !== null) {
      return "recording";
    }

    return this.m_recorded_url === null ? "idle" : "ready";
  }

  private current_selected_model(): BrowserModelDefinition | null {
    return find_browser_model_definition(this.m_view.selected_model_id());
  }

  private active_model_definition(): BrowserModelDefinition | null {
    if (this.m_active_model_id === null) {
      return null;
    }

    return find_browser_model_definition(this.m_active_model_id);
  }

  private active_model_label(): string {
    if (this.m_active_model_id === null) {
      return "rough matte fallback";
    }

    return (
      find_browser_model_definition(this.m_active_model_id)?.filename ??
      "rough matte fallback"
    );
  }

  private webcam_active(): boolean {
    return this.m_source.kind === "webcam";
  }

  private source_ready(): boolean {
    switch (this.m_source.kind) {
      case "video":
      case "webcam":
        return this.m_view.video_has_metadata();
      case "image":
        return true;
      case "sequence":
        return this.m_source.current_bitmap !== null;
      case "none":
        return false;
    }
  }

  private current_source_display_dimensions(): { width: number; height: number } | null {
    switch (this.m_source.kind) {
      case "video":
      case "webcam": {
        const dimensions = this.m_view.video_dimensions();
        if (dimensions.width <= 0 || dimensions.height <= 0) {
          return null;
        }

        return dimensions;
      }
      case "image":
        return {
          width: this.m_source.bitmap.width,
          height: this.m_source.bitmap.height,
        };
      case "sequence":
        if (this.m_source.current_bitmap === null) {
          return null;
        }

        return {
          width: this.m_source.current_bitmap.width,
          height: this.m_source.current_bitmap.height,
        };
      case "none":
        return null;
    }
  }

  private source_supports_preview(): boolean {
    return (
      this.m_source.kind === "video" ||
      this.m_source.kind === "webcam" ||
      this.m_source.kind === "sequence"
    );
  }

  private source_supports_recording(): boolean {
    return this.source_supports_preview();
  }

  private source_supports_full_media(): boolean {
    return (
      this.m_source.kind === "video" ||
      this.m_source.kind === "image" ||
      this.m_source.kind === "sequence"
    );
  }

  private auto_target_resolution(): number {
    return (
      this.active_model_definition()?.resolution ??
      this.current_selected_model()?.resolution ??
      fallback_auto_resolution()
    );
  }

  private resolved_target_resolution(): number {
    return resolve_target_resolution(
      this.m_view.selected_resolution_preset_id(),
      this.auto_target_resolution(),
    );
  }

  private apply_resolution_state(): void {
    const auto_resolution = this.auto_target_resolution();
    this.m_view.set_auto_target_resolution(auto_resolution);
    this.m_view.set_resolution_detail(
      resolution_preset_detail(
        this.m_view.selected_resolution_preset_id(),
        auto_resolution,
      ),
    );
    this.m_view.sync_stage_resolution(this.resolved_target_resolution());
  }

  private handle_model_selection_changed(): void {
    this.apply_resolution_state();
  }

  private processing_progress_visible(): boolean {
    return !this.m_preview_loop_active;
  }

  private async yield_to_browser(): Promise<void> {
    await new Promise<void>((resolve) => {
      requestAnimationFrame(() => {
        resolve();
      });
    });
  }

  private async wait_ms(delay_ms: number): Promise<void> {
    await new Promise<void>((resolve) => {
      window.setTimeout(resolve, delay_ms);
    });
  }

  private set_processing_state(
    progress: {
      stage_label: string;
      detail: string;
      progress_ratio: number | null;
    },
  ): void {
    if (!this.processing_progress_visible()) {
      return;
    }

    this.m_view.set_processing_state({
      visible: true,
      ...progress,
    });
  }

  private clear_processing_state(): void {
    if (this.m_full_media_job_active) {
      this.m_view.set_processing_state({
        visible: true,
        stage_label: "media export",
        detail: "Preparing the next media frame.",
        progress_ratio: null,
      });
      return;
    }

    this.m_view.set_processing_state({
      visible: false,
      stage_label: "idle",
      detail: "Waiting for a manual processing pass.",
      progress_ratio: 0,
    });
  }

  private apply_processing_progress(progress: FrameProcessingProgress): void {
    this.set_processing_state({
      stage_label:
        progress.stage === "inference" ? "inference" : "post-process",
      detail: progress.detail,
      progress_ratio: progress.progress_ratio,
    });
  }

  private async record_output_stream_job(
    fps: number,
    job: () => Promise<void>,
    locked_dimensions: { width: number; height: number } | null = null,
  ): Promise<Result<Blob, AppError>> {
    if (locked_dimensions !== null) {
      this.m_view.lock_stage_canvas_to_display_dimensions(
        locked_dimensions.width,
        locked_dimensions.height,
      );
    }

    const mime_type = pick_recording_mime_type();
    const stream = this.m_view.capture_output_stream(fps);
    const chunks: Blob[] = [];
    let stream_finalized = false;

    const finalize_stream = (): void => {
      if (stream_finalized) {
        return;
      }

      stream_finalized = true;
      for (const track of stream.getTracks()) {
        track.stop();
      }
      this.m_view.unlock_stage_canvas_dimensions();
    };

    let recorder: MediaRecorder;
    try {
      recorder =
        mime_type.length > 0
          ? new MediaRecorder(stream, { mimeType: mime_type })
          : new MediaRecorder(stream);
    } catch (cause) {
      finalize_stream();

      return err(
        app_error(
          "recording_failed",
          "MediaRecorder is unavailable for full-media export.",
          cause,
        ),
      );
    }

    const blob_result_promise = new Promise<Result<Blob, AppError>>((resolve) => {
      recorder.addEventListener("dataavailable", (event) => {
        if (event.data.size > 0) {
          chunks.push(event.data);
        }
      });
      recorder.addEventListener("stop", () => {
        finalize_stream();

        if (chunks.length === 0) {
          resolve(
            err(
              app_error(
                "recording_failed",
                "Full-media export stopped before any chunks were captured.",
              ),
            ),
          );
          return;
        }

        resolve(
          ok(
            new Blob(chunks, {
              type: recorder.mimeType || "video/webm",
            }),
          ),
        );
      });
      recorder.addEventListener("error", () => {
        finalize_stream();

        resolve(
          err(
            app_error(
              "recording_failed",
              "MediaRecorder reported an error during full-media export.",
            ),
          ),
        );
      });
    });

    recorder.start(250);

    try {
      await job();
    } catch (cause) {
      if (recorder.state !== "inactive") {
        recorder.stop();
      }

      await blob_result_promise;
      return err(
        app_error(
          "recording_failed",
          "Full-media export aborted before the output file was finalized.",
          cause,
        ),
      );
    }

    if (recorder.state !== "inactive") {
      recorder.stop();
    }

    return blob_result_promise;
  }

  private refresh_button_state(): void {
    const interactions_blocked =
      this.m_model_load_in_flight ||
      this.m_frame_in_flight ||
      this.m_full_media_job_active;
    const selected_model = this.current_selected_model();
    const can_load_selected_model =
      !interactions_blocked &&
      selected_model !== null &&
      selected_model.id !== this.m_active_model_id;

    const state: ViewButtonsState = {
      can_load_selected_model,
      can_process_frame: this.source_ready() && !this.m_frame_in_flight,
      can_process_full_media:
        this.source_ready() &&
        this.source_supports_full_media() &&
        !this.m_preview_loop_active &&
        this.m_recorder === null &&
        !interactions_blocked,
      can_start_preview:
        this.source_ready() &&
        this.source_supports_preview() &&
        !this.m_frame_in_flight &&
        !this.m_preview_loop_active,
      can_stop_preview: this.m_preview_loop_active,
      can_start_recording:
        this.source_ready() &&
        this.source_supports_recording() &&
        !this.m_frame_in_flight &&
        this.m_recorder === null,
      can_stop_recording: this.m_recorder !== null,
      can_start_webcam: !this.webcam_active() && !this.m_frame_in_flight,
      can_stop_webcam: this.webcam_active() && !this.m_frame_in_flight,
      can_load_hint: !interactions_blocked,
    };

    this.m_view.set_button_state(state);
  }

  private dispose_resources(): void {
    this.stop_preview_loop_internal(null);
    this.release_source();

    if (this.m_recorded_url !== null) {
      URL.revokeObjectURL(this.m_recorded_url);
      this.m_recorded_url = null;
    }
  }

  private release_video_url(): void {
    if (this.m_video_url !== null) {
      URL.revokeObjectURL(this.m_video_url);
      this.m_video_url = null;
    }
  }

  private release_source(): void {
    switch (this.m_source.kind) {
      case "video":
        this.release_video_url();
        this.m_view.clear_video_source();
        break;
      case "webcam":
        for (const track of this.m_source.stream.getTracks()) {
          track.stop();
        }
        this.m_view.clear_video_source();
        break;
      case "image":
        safe_close_bitmap(this.m_source.bitmap);
        break;
      case "sequence":
        safe_close_bitmap(this.m_source.current_bitmap);
        break;
      case "none":
        break;
    }

    this.m_source = { kind: "none" };
    this.m_view.set_source_visual_mode("video");
    this.m_view.reset_stage_aspect_ratio();
    this.m_view.set_sequence_state({
      visible: false,
      current_index: 0,
      total_count: 1,
    });
    this.release_hint();
  }

  private release_hint(): void {
    switch (this.m_hint.kind) {
      case "video":
        URL.revokeObjectURL(this.m_hint.url);
        this.m_view.clear_hint_video_source();
        break;
      case "image":
        safe_close_bitmap(this.m_hint.bitmap);
        break;
      case "sequence":
        safe_close_bitmap(this.m_hint.current_bitmap);
        break;
      case "none":
        break;
    }

    this.m_hint = { kind: "none" };
    this.m_view.set_hint_sequence_state({
      visible: false,
      current_index: 0,
      total_count: 1,
    });
  }

  private prepare_for_source_change(): void {
    if (this.m_recorder !== null) {
      this.stop_recording();
    }

    this.stop_preview_loop_internal(null);
    this.release_source();
  }

  private clear_download_artifact(): void {
    if (this.m_recorded_url !== null) {
      URL.revokeObjectURL(this.m_recorded_url);
      this.m_recorded_url = null;
    }

    this.m_view.set_download_artifact(null);
  }

  private set_download_artifact(blob: Blob, filename: string): void {
    this.clear_download_artifact();
    this.m_recorded_url = URL.createObjectURL(blob);
    this.m_view.set_download_artifact({
      url: this.m_recorded_url,
      filename,
    });
  }

  private export_filename(extension: "png" | "webm"): string {
    switch (this.m_source.kind) {
      case "video":
      case "image":
        return `${file_stem(this.m_source.file.name)}_corridorkey.${extension}`;
      case "sequence":
        return `${file_stem(this.m_source.files[0]?.name ?? "sequence")}_corridorkey.${extension}`;
      case "webcam":
        return `webcam_corridorkey.${extension}`;
      case "none":
        return `corridorkey_export.${extension}`;
    }
  }

  private async create_bitmap(file: File): Promise<Result<ImageBitmap, AppError>> {
    try {
      const bitmap = await createImageBitmap(file);
      return { ok: true, value: bitmap };
    } catch (cause) {
      return err(
        app_error(
          "source_load_failed",
          `Failed to decode ${file.name}.`,
          cause,
        ),
      );
    }
  }

  private async load_video_file(file: File): Promise<void> {
    this.prepare_for_source_change();

    this.m_video_url = URL.createObjectURL(file);
    this.m_source = { kind: "video", file };
    this.m_view.set_source_visual_mode("video");
    this.m_view.load_video_source(this.m_video_url);
    this.m_view.set_status(`Loading video ${file.name}...`);
    this.refresh_button_state();
  }

  private async load_image_file(file: File): Promise<void> {
    this.prepare_for_source_change();

    const bitmap_result = await this.create_bitmap(file);
    if (!bitmap_result.ok) {
      this.m_view.set_status(bitmap_result.error.message);
      this.refresh_button_state();
      return;
    }

    this.m_source = {
      kind: "image",
      file,
      bitmap: bitmap_result.value,
    };
    this.m_view.set_source_visual_mode("still");
    this.m_view.set_stage_aspect_ratio(
      bitmap_result.value.width,
      bitmap_result.value.height,
    );
    this.m_view.set_status(`Loaded image ${file.name}.`);
    await this.process_current_frame();
    this.refresh_button_state();
  }

  private async set_sequence_frame(
    index: number,
    process_frame: boolean,
  ): Promise<Result<void, AppError>> {
    if (this.m_source.kind !== "sequence") {
      return err(
        app_error(
          "invalid_state",
          "No image sequence is currently loaded.",
        ),
      );
    }

    const normalized_index = Math.min(
      Math.max(0, index),
      this.m_source.files.length - 1,
    );
    const file = this.m_source.files[normalized_index];
    const bitmap_result = await this.create_bitmap(file);
    if (!bitmap_result.ok) {
      return bitmap_result;
    }

    safe_close_bitmap(this.m_source.current_bitmap);
    this.m_source.current_bitmap = bitmap_result.value;
    this.m_source.current_index = normalized_index;
    this.m_view.set_stage_aspect_ratio(
      bitmap_result.value.width,
      bitmap_result.value.height,
    );
    this.m_view.set_sequence_state({
      visible: true,
      current_index: normalized_index,
      total_count: this.m_source.files.length,
    });

    const hint_sync_result = await this.sync_hint_sequence_to_source_index(
      normalized_index,
      false,
    );
    if (!hint_sync_result.ok) {
      return hint_sync_result;
    }

    if (process_frame) {
      await this.process_current_frame();
    }

    return { ok: true, value: undefined };
  }

  private async load_image_sequence(files: File[]): Promise<void> {
    this.prepare_for_source_change();

    this.m_source = {
      kind: "sequence",
      files,
      current_index: 0,
      current_bitmap: null,
    };
    this.m_view.set_source_visual_mode("still");
    this.m_view.set_sequence_state({
      visible: true,
      current_index: 0,
      total_count: files.length,
    });

    const frame_result = await this.set_sequence_frame(0, true);
    if (!frame_result.ok) {
      this.m_view.set_status(frame_result.error.message);
      this.refresh_button_state();
      return;
    }

    this.m_view.set_status(
      `Loaded image sequence with ${files.length} frames.`,
    );
    this.refresh_button_state();
  }

  private async handle_source_files_selected(): Promise<void> {
    const selection_result = classify_source_files(
      this.m_view.selected_source_files(),
    );
    if (!selection_result.ok) {
      this.m_view.set_status(selection_result.error.message);
      this.refresh_button_state();
      return;
    }

    const { kind, files } = selection_result.value;
    if (kind === "video") {
      await this.load_video_file(files[0]);
      return;
    }

    if (kind === "image") {
      await this.load_image_file(files[0]);
      return;
    }

    await this.load_image_sequence(files);
  }

  private async handle_hint_files_selected(): Promise<void> {
    const selection_result = classify_source_files(
      this.m_view.selected_hint_files(),
    );
    if (!selection_result.ok) {
      this.m_view.set_status(selection_result.error.message);
      this.refresh_button_state();
      return;
    }

    const { kind, files } = selection_result.value;
    if (kind === "video") {
      await this.load_hint_video(files[0]);
      return;
    }

    if (kind === "image") {
      await this.load_hint_image(files[0]);
      return;
    }

    await this.load_hint_sequence(files);
  }

  private async load_hint_video(file: File): Promise<void> {
    this.release_hint();

    const url = URL.createObjectURL(file);
    this.m_hint = { kind: "video", file, url };
    this.m_view.set_hint_visual_mode("video");
    this.m_view.load_hint_video_source(url);
    this.m_view.set_status(`Loading hint video ${file.name}...`);
    this.refresh_button_state();
  }

  private async load_hint_image(file: File): Promise<void> {
    this.release_hint();

    const bitmap_result = await this.create_bitmap(file);
    if (!bitmap_result.ok) {
      this.m_view.set_status(bitmap_result.error.message);
      this.refresh_button_state();
      return;
    }

    this.m_hint = {
      kind: "image",
      file,
      bitmap: bitmap_result.value,
    };
    this.m_view.set_hint_visual_mode("still");
    this.m_view.set_status(`Loaded hint image ${file.name}.`);
    this.refresh_button_state();
  }

  private async load_hint_sequence(files: File[]): Promise<void> {
    this.release_hint();

    this.m_hint = {
      kind: "sequence",
      files,
      current_index: 0,
      current_bitmap: null,
    };
    this.m_view.set_hint_sequence_state({
      visible: true,
      current_index: 0,
      total_count: files.length,
    });
    this.m_view.set_hint_visual_mode("still");

    const frame_result = await this.set_hint_sequence_frame(0, false);
    if (!frame_result.ok) {
      this.m_view.set_status(frame_result.error.message);
      this.refresh_button_state();
      return;
    }

    this.m_view.set_status(
      `Loaded hint sequence with ${files.length} frames.`,
    );
    this.refresh_button_state();
  }

  private async set_hint_sequence_frame(
    index: number,
    process_frame: boolean,
  ): Promise<Result<void, AppError>> {
    if (this.m_hint.kind !== "sequence") {
      return err(
        app_error(
          "invalid_state",
          "No hint sequence is currently loaded.",
        ),
      );
    }

    const normalized_index = Math.min(
      Math.max(0, index),
      this.m_hint.files.length - 1,
    );
    const file = this.m_hint.files[normalized_index];
    const bitmap_result = await this.create_bitmap(file);
    if (!bitmap_result.ok) {
      return bitmap_result;
    }

    safe_close_bitmap(this.m_hint.current_bitmap);
    this.m_hint.current_bitmap = bitmap_result.value;
    this.m_hint.current_index = normalized_index;
    this.m_view.set_hint_sequence_state({
      visible: true,
      current_index: normalized_index,
      total_count: this.m_hint.files.length,
    });

    if (process_frame) {
      await this.process_current_frame();
    }

    return { ok: true, value: undefined };
  }

  private async sync_hint_sequence_to_source_index(
    source_index: number,
    process_frame: boolean,
  ): Promise<Result<void, AppError>> {
    if (this.m_hint.kind !== "sequence") {
      return ok(undefined);
    }

    const hint_index = Math.min(
      Math.max(0, source_index),
      this.m_hint.files.length - 1,
    );

    return this.set_hint_sequence_frame(hint_index, process_frame);
  }

  private async rewind_media_for_export(): Promise<Result<void, AppError>> {
    if (this.m_source.kind === "video") {
      const source_seek = await this.m_view.seek_video(0);
      if (!source_seek.ok) {
        return source_seek;
      }
    }

    if (this.m_hint.kind === "video" && this.m_view.hint_video_has_metadata()) {
      const hint_seek = await this.m_view.seek_hint_video(0);
      if (!hint_seek.ok) {
        return hint_seek;
      }
    }

    if (this.m_source.kind === "sequence") {
      const source_reset = await this.set_sequence_frame(0, false);
      if (!source_reset.ok) {
        return source_reset;
      }
    }

    return ok(undefined);
  }

  private async handle_webcam_start_requested(): Promise<void> {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      this.m_view.set_status(
        "This browser does not expose getUserMedia for webcam capture.",
      );
      return;
    }

    this.prepare_for_source_change();

    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: true,
        audio: false,
      });
      this.m_source = { kind: "webcam", stream };
      this.m_view.set_source_visual_mode("video");
      this.m_view.attach_camera_stream(stream);
      this.m_view.set_status("Waiting for webcam metadata...");
    } catch (cause) {
      const denied =
        cause instanceof DOMException &&
        (cause.name === "NotAllowedError" || cause.name === "SecurityError");
      const message = denied
        ? "Webcam permission was denied by the browser."
        : "Failed to start the webcam source.";
      this.m_view.set_status(message);
    }

    this.refresh_button_state();
  }

  private handle_webcam_stop_requested(): void {
    if (!this.webcam_active()) {
      return;
    }

    this.prepare_for_source_change();
    this.m_view.set_status("Webcam source stopped.");
    this.refresh_button_state();
  }

  private async handle_selected_model_load_requested(): Promise<void> {
    const selected_model = this.current_selected_model();
    if (selected_model === null) {
      this.m_view.set_status("Select a supported browser model before loading.");
      return;
    }

    await this.load_browser_model(selected_model.id, false);
  }

  private progress_ratio(progress: BrowserModelLoadProgress): number | null {
    if (progress.phase === "initialize") {
      return 0.95;
    }

    if (progress.ratio === null) {
      return null;
    }

    return Math.min(0.9, progress.ratio * 0.9);
  }

  private progress_detail(progress: BrowserModelLoadProgress): string {
    if (progress.phase === "initialize") {
      return `Initializing ONNX Runtime Web for ${progress.model.filename}.`;
    }

    if (progress.total_bytes === null || progress.ratio === null) {
      return `Fetching ${progress.model.filename}...`;
    }

    return `Fetching ${progress.model.filename}: ${Math.round(progress.ratio * 100)}%.`;
  }

  private update_model_loading_view(progress: BrowserModelLoadProgress): void {
    this.m_view.set_model_state({
      active_model_label:
        this.m_active_model_id === null
          ? "bootstrapping"
          : this.active_model_label(),
      state_label:
        progress.phase === "download" ? "loading" : "initializing",
      progress_ratio: this.progress_ratio(progress),
      detail: this.progress_detail(progress),
    });
  }

  private handle_model_load_failure(
    model: BrowserModelDefinition,
    message: string,
  ): void {
    if (this.m_processing_service.has_model_session()) {
      const active_model_label = this.active_model_label();
      this.m_view.set_model_state({
        active_model_label,
        state_label: "ready",
        progress_ratio: 1,
        detail: `Failed to load ${model.filename}. Keeping ${active_model_label} active.`,
      });
      this.m_view.set_status(`Model switch failed. ${message}`);
      return;
    }

    this.m_processing_service.clear_model_session();
    this.m_view.set_metrics({
      mode: ROUGH_MATTE_LABEL,
      backend: "none",
      frame_time_ms: null,
      recording: this.recording_state_label(),
    });
    this.m_view.set_model_state({
      active_model_label: "rough matte fallback",
      state_label: "fallback",
      progress_ratio: 0,
      detail: `Default model failed to load. ${message}`,
    });
    this.m_view.set_status(
      `Default model unavailable. Falling back to rough matte. ${message}`,
    );
  }

  private async load_browser_model(
    model_id: string,
    is_bootstrap: boolean,
  ): Promise<void> {
    const model = find_browser_model_definition(model_id);
    if (model === null || this.m_model_load_in_flight) {
      return;
    }

    this.m_model_load_in_flight = true;
    this.m_view.set_status(
      is_bootstrap
        ? `Bootstrapping ${model.filename}...`
        : `Loading ${model.filename} from the fixed browser catalog...`,
    );
    this.m_view.set_model_state({
      active_model_label:
        this.m_active_model_id === null
          ? "bootstrapping"
          : this.active_model_label(),
      state_label: "loading",
      progress_ratio: 0,
      detail: `Fetching ${model.filename}.`,
    });
    this.refresh_button_state();

    const session_result = await this.m_model_loader.load(model, (progress) => {
      this.update_model_loading_view(progress);
    });

    this.m_model_load_in_flight = false;

    if (!session_result.ok) {
      this.handle_model_load_failure(model, session_result.error.message);
      this.refresh_button_state();
      return;
    }

    this.m_processing_service.set_model_session(session_result.value);
    this.m_active_model_id = model.id;
    this.apply_resolution_state();
    this.m_view.set_metrics({
      mode: MODEL_LABEL,
      backend: session_result.value.backend_label,
      frame_time_ms: null,
      recording: this.recording_state_label(),
    });
    this.m_view.set_model_state({
      active_model_label: model.filename,
      state_label: "ready",
      progress_ratio: 1,
      detail: `${model.label} active on ${session_result.value.backend_label}.`,
    });
    this.m_view.set_status(
      `${model.filename} ready on ${session_result.value.backend_label}.`,
    );

    if (this.source_ready()) {
      await this.process_current_frame();
    }

    this.refresh_button_state();
  }

  private async handle_video_metadata_loaded(): Promise<void> {
    if (this.m_source.kind !== "video" && this.m_source.kind !== "webcam") {
      return;
    }

    this.m_view.sync_stage_resolution(this.m_view.target_resolution());

    if (this.m_source.kind === "webcam") {
      const play_result = await this.m_view.play_video();
      if (!play_result.ok) {
        this.m_view.set_status(play_result.error.message);
        this.refresh_button_state();
        return;
      }
    }

    const dimensions = this.m_view.video_dimensions();
    this.m_view.set_stage_aspect_ratio(dimensions.width, dimensions.height);
    const label =
      this.m_source.kind === "video"
        ? this.m_source.file.name
        : "webcam stream";
    this.m_view.set_status(
      `Loaded ${label}: ${dimensions.width}x${dimensions.height}.`,
    );
    await this.process_current_frame();
    this.refresh_button_state();
  }

  private handle_video_ended(): void {
    if (this.m_source.kind === "video") {
      this.stop_preview_loop_internal("Video playback reached the end.");
    }
  }

  private async handle_sequence_index_changed(): Promise<void> {
    if (this.m_source.kind !== "sequence") {
      return;
    }

    if (this.m_preview_loop_active) {
      this.stop_preview_loop_internal("Preview loop stopped.");
    }

    const result = await this.set_sequence_frame(
      this.m_view.selected_sequence_index(),
      true,
    );
    if (!result.ok) {
      this.m_view.set_status(result.error.message);
    }

    this.refresh_button_state();
  }

  private async handle_hint_sequence_index_changed(): Promise<void> {
    if (this.m_hint.kind !== "sequence") {
      return;
    }

    const result = await this.set_hint_sequence_frame(
      this.m_view.selected_hint_sequence_index(),
      true,
    );
    if (!result.ok) {
      this.m_view.set_status(result.error.message);
    }

    this.refresh_button_state();
  }

  private async handle_resolution_changed(): Promise<void> {
    this.apply_resolution_state();
    if (!this.source_ready()) {
      this.refresh_button_state();
      return;
    }

    await this.process_current_frame();
  }

  private async capture_source_frame(): Promise<Result<RgbaFrame, AppError>> {
    switch (this.m_source.kind) {
      case "video":
      case "webcam":
        return this.m_view.draw_video_frame();
      case "image":
        return this.m_view.draw_bitmap_frame(this.m_source.bitmap);
      case "sequence":
        if (this.m_source.current_bitmap === null) {
          return err(
            app_error(
              "invalid_state",
              "The image sequence has no decoded frame ready.",
            ),
          );
        }
        return this.m_view.draw_bitmap_frame(this.m_source.current_bitmap);
      case "none":
        return err(
          app_error(
            "missing_input",
            "Load a webcam, image, image sequence, or video before processing.",
          ),
        );
    }
  }

  private async capture_hint_normalized(): Promise<Result<Float32Array | null, AppError>> {
    switch (this.m_hint.kind) {
      case "video":
        return this.m_view.draw_hint_video_frame();
      case "image":
        return ok(this.m_view.draw_hint_bitmap_frame(this.m_hint.bitmap));
      case "sequence":
        if (this.m_hint.current_bitmap === null) {
          return err(
            app_error(
              "invalid_state",
              "The hint sequence has no decoded frame ready.",
            ),
          );
        }
        return ok(this.m_view.draw_hint_bitmap_frame(this.m_hint.current_bitmap));
      case "none":
        return ok(null);
    }
  }

  async process_current_frame(): Promise<Result<void, AppError>> {
    if (this.m_frame_in_flight || !this.source_ready()) {
      return err(
        app_error(
          "invalid_state",
          "A source frame is not ready for processing.",
        ),
      );
    }

    this.m_frame_in_flight = true;
    this.apply_resolution_state();
    this.refresh_button_state();

    try {
      this.set_processing_state({
        stage_label: "capture",
        detail: `Capturing a ${this.resolved_target_resolution()}x${this.resolved_target_resolution()} source frame.`,
        progress_ratio: 0.12,
      });
      if (this.processing_progress_visible()) {
        await this.yield_to_browser();
      }

      const frame_result = await this.capture_source_frame();
      if (!frame_result.ok) {
        this.m_view.set_status(frame_result.error.message);
        return frame_result;
      }

      const started_at = performance.now();
      this.set_processing_state({
        stage_label: "hint",
        detail:
          this.m_hint.kind === "none"
            ? "Preparing the fallback hint input."
            : "Normalizing the external alpha hint.",
        progress_ratio: 0.28,
      });
      const hint_result = await this.capture_hint_normalized();
      if (!hint_result.ok) {
        this.m_view.set_status(hint_result.error.message);
        return hint_result;
      }

      const processed_frame = await this.m_processing_service.process_frame(
        frame_result.value,
        hint_result.value ?? undefined,
        (progress) => {
          this.apply_processing_progress(progress);
        },
      );
      if (!processed_frame.ok) {
        this.m_view.set_status(processed_frame.error.message);
        return processed_frame;
      }

      this.set_processing_state({
        stage_label: "render",
        detail: "Drawing the processed frame to the output canvas.",
        progress_ratio: 0.96,
      });
      this.m_view.render_output(processed_frame.value.output_frame);
      const elapsed_ms = performance.now() - started_at;

      this.m_view.set_metrics({
        mode: mode_label(processed_frame.value.result.mode),
        backend: processed_frame.value.backend_label,
        frame_time_ms: elapsed_ms,
        recording: this.recording_state_label(),
      });
      this.m_view.set_status(
        `Processed ${processed_frame.value.result.width}x${processed_frame.value.result.height} frame via ${processed_frame.value.source_label} in ${elapsed_ms.toFixed(1)} ms.`,
      );
      return ok(undefined);
    } finally {
      this.m_frame_in_flight = false;
      this.clear_processing_state();
      this.refresh_button_state();
    }
  }

  private async export_current_image(): Promise<Result<void, AppError>> {
    const frame_result = await this.process_current_frame();
    if (!frame_result.ok) {
      return frame_result;
    }
    const png_result = await this.m_view.output_png_blob();
    if (!png_result.ok) {
      return png_result;
    }

    this.set_download_artifact(
      png_result.value,
      this.export_filename("png"),
    );
    this.m_view.set_status(
      `${this.export_filename("png")} is ready to download.`,
    );
    return ok(undefined);
  }

  private async export_sequence_media(): Promise<Result<void, AppError>> {
    if (this.m_source.kind !== "sequence") {
      return err(
        app_error(
          "invalid_state",
          "A source image sequence is required for sequence export.",
        ),
      );
    }

    const total_frames = this.m_source.files.length;
    const frame_interval_ms = 1000 / SEQUENCE_PREVIEW_FPS;
    const rewind_result = await this.rewind_media_for_export();
    if (!rewind_result.ok) {
      return rewind_result;
    }

    const locked_dimensions = this.current_source_display_dimensions();
    if (locked_dimensions === null) {
      return err(
        app_error(
          "invalid_state",
          "The first sequence frame is not ready for export.",
        ),
      );
    }

    const record_result = await this.record_output_stream_job(
      SEQUENCE_PREVIEW_FPS,
      async () => {
        for (let index = 0; index < total_frames; index += 1) {
          if (index > 0) {
            const frame_result = await this.set_sequence_frame(index, false);
            if (!frame_result.ok) {
              throw frame_result.error;
            }
          }

          this.m_view.set_status(
            `Exporting sequence frame ${index + 1} / ${total_frames}.`,
          );
          const process_result = await this.process_current_frame();
          if (!process_result.ok) {
            throw process_result.error;
          }
          this.set_processing_state({
            stage_label: "media export",
            detail: `Encoding sequence frame ${index + 1} of ${total_frames}.`,
            progress_ratio: Math.min((index + 1) / total_frames, 0.99),
          });
          await this.wait_ms(frame_interval_ms);
        }
      },
      locked_dimensions,
    );

    if (!record_result.ok) {
      return record_result;
    }

    const filename = this.export_filename("webm");
    this.set_download_artifact(record_result.value, filename);
    this.m_view.set_status(`${filename} is ready to download.`);
    return ok(undefined);
  }

  private async export_video_media(): Promise<Result<void, AppError>> {
    if (this.m_source.kind !== "video") {
      return err(
        app_error(
          "invalid_state",
          "A source video is required for full video export.",
        ),
      );
    }

    const duration = this.m_view.video_duration();
    const min_frame_delta = 1 / (PREVIEW_RECORDING_FPS * 2);
    const rewind_result = await this.rewind_media_for_export();
    if (!rewind_result.ok) {
      return rewind_result;
    }

    const locked_dimensions = this.current_source_display_dimensions();
    if (locked_dimensions === null) {
      return err(
        app_error(
          "invalid_state",
          "The source video dimensions are unavailable for export.",
        ),
      );
    }

    const record_result = await this.record_output_stream_job(
      PREVIEW_RECORDING_FPS,
      async () => {
        await this.process_current_frame();

        if (this.m_hint.kind === "video") {
          const hint_play_result = await this.m_view.play_hint_video();
          if (!hint_play_result.ok) {
            throw hint_play_result.error;
          }
        }

        const play_result = await this.m_view.play_video();
        if (!play_result.ok) {
          throw play_result.error;
        }

        let last_processed_time = 0;
        while (!this.m_view.video_ended()) {
          await this.yield_to_browser();
          const current_time = this.m_view.video_current_time();
          if (
            current_time > 0 &&
            current_time - last_processed_time < min_frame_delta &&
            !this.m_view.video_ended()
          ) {
            continue;
          }

          last_processed_time = current_time;
          const elapsed_label = format_seconds(current_time);
          const duration_label = format_seconds(duration);
          this.m_view.set_status(
            `Exporting video ${elapsed_label} / ${duration_label}.`,
          );
          const process_result = await this.process_current_frame();
          if (!process_result.ok) {
            throw process_result.error;
          }
          this.set_processing_state({
            stage_label: "media export",
            detail: `Encoding video timeline ${elapsed_label} / ${duration_label}.`,
            progress_ratio:
              duration > 0
                ? Math.min(current_time / duration, 0.99)
                : null,
          });
        }

        this.m_view.pause_video();
        if (this.m_hint.kind === "video") {
          this.m_view.pause_hint_video();
        }
        await this.wait_ms(1000 / PREVIEW_RECORDING_FPS);
      },
      locked_dimensions,
    );

    if (!record_result.ok) {
      return record_result;
    }

    const filename = this.export_filename("webm");
    this.set_download_artifact(record_result.value, filename);
    this.m_view.set_status(`${filename} is ready to download.`);
    return ok(undefined);
  }

  async process_full_media(): Promise<void> {
    if (
      this.m_full_media_job_active ||
      this.m_frame_in_flight ||
      !this.source_ready() ||
      !this.source_supports_full_media() ||
      this.m_recorder !== null
    ) {
      return;
    }

    this.m_full_media_job_active = true;
    this.stop_preview_loop_internal(null);
    this.apply_resolution_state();
    this.clear_download_artifact();
    this.refresh_button_state();
    this.m_view.set_metrics({
      mode: this.m_processing_service.has_model_session()
        ? MODEL_LABEL
        : ROUGH_MATTE_LABEL,
      backend: this.m_processing_service.current_backend_label(),
      frame_time_ms: null,
      recording: this.recording_state_label(),
    });
    this.set_processing_state({
      stage_label: "media export",
      detail: "Preparing the full-media export job.",
      progress_ratio: 0,
    });

    try {
      const result =
        this.m_source.kind === "image"
          ? await this.export_current_image()
          : this.m_source.kind === "sequence"
            ? await this.export_sequence_media()
            : await this.export_video_media();

      if (!result.ok) {
        this.m_view.set_status(result.error.message);
      }
    } finally {
      this.m_full_media_job_active = false;
      if (this.m_source.kind === "video") {
        this.m_view.pause_video();
      }
      if (this.m_hint.kind === "video") {
        this.m_view.pause_hint_video();
      }
      this.clear_processing_state();
      this.m_view.set_metrics({
        mode: this.m_processing_service.has_model_session()
          ? MODEL_LABEL
          : ROUGH_MATTE_LABEL,
        backend: this.m_processing_service.current_backend_label(),
        frame_time_ms: null,
        recording: this.recording_state_label(),
      });
      this.refresh_button_state();
    }
  }

  private async advance_sequence_frame(): Promise<boolean> {
    if (this.m_source.kind !== "sequence") {
      return false;
    }

    const next_index = this.m_source.current_index + 1;
    if (next_index >= this.m_source.files.length) {
      return false;
    }

    const result = await this.set_sequence_frame(next_index, true);
    return result.ok;
  }

  private tick_preview = async (timestamp: number): Promise<void> => {
    if (!this.m_preview_loop_active) {
      return;
    }

    if (this.m_source.kind === "video") {
      if (!this.m_view.video_paused() && !this.m_view.video_ended()) {
        await this.process_current_frame();
      }
      requestAnimationFrame((next_timestamp) => {
        void this.tick_preview(next_timestamp);
      });
      return;
    }

    if (this.m_source.kind === "webcam") {
      await this.process_current_frame();
      requestAnimationFrame((next_timestamp) => {
        void this.tick_preview(next_timestamp);
      });
      return;
    }

    if (this.m_source.kind === "sequence") {
      if (this.m_sequence_preview_last_tick === 0) {
        this.m_sequence_preview_last_tick = timestamp;
      }

      const step_ms = 1000 / SEQUENCE_PREVIEW_FPS;
      if (timestamp - this.m_sequence_preview_last_tick >= step_ms) {
        this.m_sequence_preview_last_tick = timestamp;
        const advanced = await this.advance_sequence_frame();
        if (!advanced) {
          this.stop_preview_loop_internal(
            "Preview loop finished at the end of the image sequence.",
          );
          return;
        }
      }

      requestAnimationFrame((next_timestamp) => {
        void this.tick_preview(next_timestamp);
      });
      return;
    }

    this.stop_preview_loop_internal(null);
  };

  async start_preview_loop(): Promise<void> {
    if (
      this.m_preview_loop_active ||
      !this.source_ready() ||
      !this.source_supports_preview()
    ) {
      return;
    }

    this.m_preview_loop_active = true;
    this.m_sequence_preview_last_tick = 0;
    this.refresh_button_state();

    if (this.m_source.kind === "video" || this.m_source.kind === "webcam") {
      const play_result = await this.m_view.play_video();
      if (!play_result.ok) {
        this.m_preview_loop_active = false;
        this.refresh_button_state();
        this.m_view.set_status(play_result.error.message);
        return;
      }
    }

    this.m_view.set_status("Preview loop running.");
    requestAnimationFrame((timestamp) => {
      void this.tick_preview(timestamp);
    });
  }

  private stop_preview_loop_internal(message: string | null): void {
    const was_active = this.m_preview_loop_active;
    this.m_preview_loop_active = false;
    this.m_sequence_preview_last_tick = 0;

    if (was_active && this.m_source.kind === "video") {
      this.m_view.pause_video();
    }

    if (message !== null) {
      this.m_view.set_status(message);
    }

    this.refresh_button_state();
  }

  stop_preview_loop(): void {
    this.stop_preview_loop_internal("Preview loop stopped.");
  }

  private finalize_recording(): void {
    if (this.m_recorded_chunks.length === 0) {
      this.m_view.set_status("Recording stopped, but no chunks were captured.");
      this.m_recorder = null;
      this.refresh_button_state();
      return;
    }

    if (this.m_recorded_url !== null) {
      URL.revokeObjectURL(this.m_recorded_url);
    }

    const blob = new Blob(this.m_recorded_chunks, {
      type: this.m_recorder?.mimeType || "video/webm",
    });
    this.m_recorded_url = URL.createObjectURL(blob);
    this.m_view.set_download_link(this.m_recorded_url);
    this.m_view.set_status(
      `Recording finished. ${DEFAULT_DOWNLOAD_FILENAME} is ready to download.`,
    );
    this.m_recorder = null;
    this.m_view.set_metrics({
      mode: this.m_processing_service.has_model_session()
        ? MODEL_LABEL
        : ROUGH_MATTE_LABEL,
      backend: this.m_processing_service.current_backend_label(),
      frame_time_ms: null,
      recording: this.recording_state_label(),
    });
    this.refresh_button_state();
  }

  async start_recording(): Promise<void> {
    if (this.m_recorder !== null || !this.source_supports_recording()) {
      return;
    }

    const locked_dimensions = this.current_source_display_dimensions();
    if (locked_dimensions === null) {
      this.m_view.set_status(
        "A source with known dimensions is required before recording.",
      );
      return;
    }

    this.m_view.lock_stage_canvas_to_display_dimensions(
      locked_dimensions.width,
      locked_dimensions.height,
    );

    const mime_type = pick_recording_mime_type();
    const stream = this.m_view.capture_output_stream(PREVIEW_RECORDING_FPS);
    this.m_recorded_chunks = [];

    try {
      this.m_recorder =
        mime_type.length > 0
          ? new MediaRecorder(stream, { mimeType: mime_type })
          : new MediaRecorder(stream);
    } catch (cause) {
      for (const track of stream.getTracks()) {
        track.stop();
      }
      this.m_view.unlock_stage_canvas_dimensions();
      this.m_view.set_status(
        `Recording is unavailable. ${String(
          cause instanceof Error ? cause.message : cause,
        )}`,
      );
      return;
    }

    this.m_recorder.addEventListener("dataavailable", (event) => {
      if (event.data.size > 0) {
        this.m_recorded_chunks.push(event.data);
      }
    });
    this.m_recorder.addEventListener("stop", () => {
      for (const track of stream.getTracks()) {
        track.stop();
      }
      this.m_view.unlock_stage_canvas_dimensions();
      this.finalize_recording();
    });
    this.m_recorder.start(250);

    await this.start_preview_loop();
    this.m_view.set_status("Recording started from the output canvas.");
    this.m_view.set_metrics({
      mode: this.m_processing_service.has_model_session()
        ? MODEL_LABEL
        : ROUGH_MATTE_LABEL,
      backend: this.m_processing_service.current_backend_label(),
      frame_time_ms: null,
      recording: this.recording_state_label(),
    });
    this.refresh_button_state();
  }

  stop_recording(): void {
    if (this.m_recorder === null) {
      return;
    }

    this.m_recorder.stop();
    this.m_view.set_status("Stopping recording...");
    this.refresh_button_state();
  }
}
