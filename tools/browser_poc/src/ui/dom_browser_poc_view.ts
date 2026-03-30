import {
  CHECKER_DARK,
  CHECKER_LIGHT,
  CHECKER_TILE_SIZE,
  DEFAULT_DOWNLOAD_FILENAME,
  DEFAULT_TARGET_RESOLUTION,
} from "../common/constants";
import { app_error, type AppError } from "../common/errors";
import {
  resolve_target_resolution,
  type ProcessingResolutionPreset,
} from "../common/processing_resolution";
import { err, ok, type Result } from "../common/result";
import type { BrowserModelDefinition, RgbaFrame } from "../core/image_types";
import {
  DEFAULT_STAGE_CANVAS_ASPECT_RATIO,
  DEFAULT_STAGE_VIDEO_ASPECT_RATIO,
  resolve_stage_canvas_dimensions,
  resolve_stage_draw_rect,
  resolve_stage_aspect_ratio,
  type StageCanvasDimensions,
} from "./stage_aspect_ratio";

export interface ViewMetrics {
  mode: string;
  backend: string;
  frame_time_ms: number | null;
  recording: string;
}

export interface ViewModelState {
  active_model_label: string;
  state_label: string;
  progress_ratio: number | null;
  detail: string;
}

export interface ViewSequenceState {
  visible: boolean;
  current_index: number;
  total_count: number;
}

export interface ViewHintSequenceState {
  visible: boolean;
  current_index: number;
  total_count: number;
}

export interface ViewButtonsState {
  can_load_selected_model: boolean;
  can_process_frame: boolean;
  can_process_full_media: boolean;
  can_start_preview: boolean;
  can_stop_preview: boolean;
  can_start_recording: boolean;
  can_stop_recording: boolean;
  can_start_webcam: boolean;
  can_stop_webcam: boolean;
  can_load_hint: boolean;
}

export interface ViewProcessingState {
  visible: boolean;
  stage_label: string;
  detail: string;
  progress_ratio: number | null;
}

export interface ViewHandlers {
  on_source_files_selected: () => void;
  on_hint_files_selected: () => void;
  on_model_selection_changed: () => void;
  on_selected_model_load_requested: () => void;
  on_process_frame_requested: () => void;
  on_process_full_media_requested: () => void;
  on_preview_start_requested: () => void;
  on_preview_stop_requested: () => void;
  on_recording_start_requested: () => void;
  on_recording_stop_requested: () => void;
  on_resolution_changed: () => void;
  on_video_metadata_loaded: () => void;
  on_video_ended: () => void;
  on_webcam_start_requested: () => void;
  on_webcam_stop_requested: () => void;
  on_sequence_index_changed: () => void;
  on_hint_sequence_index_changed: () => void;
}

function require_element<T extends HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (!(element instanceof HTMLElement)) {
    throw new Error(`Missing required element: ${id}`);
  }
  return element as T;
}

function require_context(canvas: HTMLCanvasElement): CanvasRenderingContext2D {
  const context = canvas.getContext("2d", {
    alpha: true,
    willReadFrequently: true,
  });

  if (context === null) {
    throw new Error("Failed to acquire a 2D canvas context.");
  }

  return context;
}

function resolve_canvas_source_dimensions(
  source: CanvasImageSource,
): { width: number; height: number } | null {
  if (source instanceof HTMLVideoElement) {
    return {
      width: source.videoWidth,
      height: source.videoHeight,
    };
  }

  if (source instanceof ImageBitmap) {
    return {
      width: source.width,
      height: source.height,
    };
  }

  if (source instanceof HTMLCanvasElement) {
    return {
      width: source.width,
      height: source.height,
    };
  }

  if (typeof OffscreenCanvas !== "undefined" && source instanceof OffscreenCanvas) {
    return {
      width: source.width,
      height: source.height,
    };
  }

  return null;
}

function fill_checkerboard(
  context: CanvasRenderingContext2D,
  width: number,
  height: number,
): void {
  const normalized_width = Math.max(1, Math.round(width));
  const normalized_height = Math.max(1, Math.round(height));

  for (let y = 0; y < normalized_height; y += CHECKER_TILE_SIZE) {
    for (let x = 0; x < normalized_width; x += CHECKER_TILE_SIZE) {
      const checker =
        (Math.floor(y / CHECKER_TILE_SIZE) + Math.floor(x / CHECKER_TILE_SIZE)) %
          2 ===
        0
          ? CHECKER_DARK
          : CHECKER_LIGHT;
      context.fillStyle = `rgb(${checker}, ${checker}, ${checker})`;
      context.fillRect(x, y, CHECKER_TILE_SIZE, CHECKER_TILE_SIZE);
    }
  }
}

export class DomBrowserPocView {
  private readonly source_files_input =
    require_element<HTMLInputElement>("source-files");
  private readonly model_select =
    require_element<HTMLSelectElement>("model-select");
  private readonly resolution_input =
    require_element<HTMLSelectElement>("target-resolution");
  private readonly resolution_detail =
    require_element<HTMLElement>("target-resolution-detail");
  private readonly hint_files_input =
    require_element<HTMLInputElement>("hint-files");
  private readonly start_webcam_button =
    require_element<HTMLButtonElement>("start-webcam");
  private readonly stop_webcam_button =
    require_element<HTMLButtonElement>("stop-webcam");
  private readonly load_selected_model_button =
    require_element<HTMLButtonElement>("load-selected-model");
  private readonly process_frame_button =
    require_element<HTMLButtonElement>("process-frame");
  private readonly process_full_media_button =
    require_element<HTMLButtonElement>("process-full-media");
  private readonly start_preview_button =
    require_element<HTMLButtonElement>("start-preview");
  private readonly stop_preview_button =
    require_element<HTMLButtonElement>("stop-preview");
  private readonly start_recording_button =
    require_element<HTMLButtonElement>("start-recording");
  private readonly stop_recording_button =
    require_element<HTMLButtonElement>("stop-recording");
  private readonly download_link =
    require_element<HTMLAnchorElement>("download-link");
  private readonly source_video =
    require_element<HTMLVideoElement>("source-video");
  private readonly active_model_value =
    require_element<HTMLElement>("active-model-value");
  private readonly model_state_value =
    require_element<HTMLElement>("model-state-value");
  private readonly model_progress =
    require_element<HTMLProgressElement>("model-load-progress");
  private readonly model_progress_detail =
    require_element<HTMLElement>("model-progress-detail");
  private readonly process_panel =
    require_element<HTMLDivElement>("process-panel");
  private readonly process_stage_value =
    require_element<HTMLElement>("process-stage-value");
  private readonly process_progress =
    require_element<HTMLProgressElement>("process-progress");
  private readonly process_progress_detail =
    require_element<HTMLElement>("process-progress-detail");
  private readonly sequence_panel =
    require_element<HTMLDivElement>("sequence-panel");
  private readonly sequence_index =
    require_element<HTMLInputElement>("sequence-index");
  private readonly sequence_frame_value =
    require_element<HTMLElement>("sequence-frame-value");
  private readonly source_canvas =
    require_element<HTMLCanvasElement>("source-canvas");
  private readonly output_canvas =
    require_element<HTMLCanvasElement>("output-canvas");
  private readonly hint_video = require_element<HTMLVideoElement>("hint-video");
  private readonly hint_sequence_panel =
    require_element<HTMLDivElement>("hint-sequence-panel");
  private readonly hint_sequence_index =
    require_element<HTMLInputElement>("hint-sequence-index");
  private readonly hint_sequence_frame_value =
    require_element<HTMLElement>("hint-sequence-frame-value");
  private readonly status_log = require_element<HTMLDivElement>("status-log");
  private readonly mode_value = require_element<HTMLElement>("mode-value");
  private readonly backend_value = require_element<HTMLElement>("backend-value");
  private readonly frame_time_value =
    require_element<HTMLElement>("frame-time-value");
  private readonly recording_value =
    require_element<HTMLElement>("recording-value");
  private readonly source_ctx = require_context(this.source_canvas);
  private readonly output_ctx = require_context(this.output_canvas);
  private readonly working_canvas = document.createElement("canvas");
  private readonly working_ctx = require_context(this.working_canvas);
  private readonly output_frame_canvas = document.createElement("canvas");
  private readonly output_frame_ctx = require_context(this.output_frame_canvas);
  private readonly hint_canvas = document.createElement("canvas");
  private readonly hint_ctx = require_context(this.hint_canvas);
  private m_auto_target_resolution = DEFAULT_TARGET_RESOLUTION;
  private m_stage_target_resolution = DEFAULT_TARGET_RESOLUTION;
  private m_stage_display_width = 1;
  private m_stage_display_height = 1;
  private m_stage_canvas_lock: StageCanvasDimensions | null = null;
  private m_locked_stage_aspect_ratio: string | null = null;

  bind_handlers(handlers: ViewHandlers): void {
    this.source_files_input.addEventListener(
      "change",
      handlers.on_source_files_selected,
    );
    this.hint_files_input.addEventListener(
      "change",
      handlers.on_hint_files_selected,
    );
    this.model_select.addEventListener(
      "change",
      handlers.on_model_selection_changed,
    );
    this.load_selected_model_button.addEventListener(
      "click",
      handlers.on_selected_model_load_requested,
    );
    this.process_frame_button.addEventListener(
      "click",
      handlers.on_process_frame_requested,
    );
    this.process_full_media_button.addEventListener(
      "click",
      handlers.on_process_full_media_requested,
    );
    this.start_preview_button.addEventListener(
      "click",
      handlers.on_preview_start_requested,
    );
    this.stop_preview_button.addEventListener(
      "click",
      handlers.on_preview_stop_requested,
    );
    this.start_recording_button.addEventListener(
      "click",
      handlers.on_recording_start_requested,
    );
    this.stop_recording_button.addEventListener(
      "click",
      handlers.on_recording_stop_requested,
    );
    this.start_webcam_button.addEventListener(
      "click",
      handlers.on_webcam_start_requested,
    );
    this.stop_webcam_button.addEventListener(
      "click",
      handlers.on_webcam_stop_requested,
    );
    this.sequence_index.addEventListener(
      "input",
      handlers.on_sequence_index_changed,
    );
    this.hint_sequence_index.addEventListener(
      "input",
      handlers.on_hint_sequence_index_changed,
    );
    this.resolution_input.addEventListener(
      "change",
      handlers.on_resolution_changed,
    );
    this.source_video.addEventListener(
      "loadedmetadata",
      handlers.on_video_metadata_loaded,
    );
    this.source_video.addEventListener("ended", handlers.on_video_ended);
  }

  selected_source_files(): File[] {
    return Array.from(this.source_files_input.files ?? []);
  }

  selected_hint_files(): File[] {
    return Array.from(this.hint_files_input.files ?? []);
  }

  selected_sequence_index(): number {
    const parsed = Number.parseInt(this.sequence_index.value, 10);
    if (!Number.isFinite(parsed)) {
      return 0;
    }

    return Math.max(0, parsed - 1);
  }

  selected_hint_sequence_index(): number {
    const parsed = Number.parseInt(this.hint_sequence_index.value, 10);
    if (!Number.isFinite(parsed)) {
      return 0;
    }

    return Math.max(0, parsed - 1);
  }

  selected_model_id(): string {
    return this.model_select.value;
  }

  selected_resolution_preset_id(): string {
    return this.resolution_input.value;
  }

  set_model_catalog(
    models: readonly BrowserModelDefinition[],
    selected_model_id: string,
  ): void {
    this.model_select.replaceChildren();

    for (const model of models) {
      const option = document.createElement("option");
      option.value = model.id;
      option.textContent = `${model.label} | ${model.size_label}`;
      option.title = model.description;
      option.selected = model.id === selected_model_id;
      this.model_select.append(option);
    }
  }

  set_resolution_presets(
    presets: readonly ProcessingResolutionPreset[],
    selected_preset_id: string,
  ): void {
    this.resolution_input.replaceChildren();

    for (const preset of presets) {
      const option = document.createElement("option");
      option.value = preset.id;
      option.textContent = preset.label;
      option.title = preset.description;
      option.selected = preset.id === selected_preset_id;
      this.resolution_input.append(option);
    }
  }

  set_auto_target_resolution(auto_target_resolution: number): void {
    this.m_auto_target_resolution = auto_target_resolution;
  }

  set_resolution_detail(detail: string): void {
    this.resolution_detail.textContent = detail;
  }

  set_sequence_state(state: ViewSequenceState): void {
    this.sequence_panel.classList.toggle("hidden", !state.visible);
    this.sequence_index.min = "1";
    this.sequence_index.max = `${Math.max(1, state.total_count)}`;
    this.sequence_index.value = `${Math.min(
      Math.max(1, state.current_index + 1),
      Math.max(1, state.total_count),
    )}`;
    this.sequence_frame_value.textContent = `Frame ${state.current_index + 1} / ${Math.max(1, state.total_count)}`;
  }

  set_hint_sequence_state(state: ViewHintSequenceState): void {
    this.hint_sequence_panel.classList.toggle("hidden", !state.visible);
    this.hint_sequence_index.min = "1";
    this.hint_sequence_index.max = `${Math.max(1, state.total_count)}`;
    this.hint_sequence_index.value = `${Math.min(
      Math.max(1, state.current_index + 1),
      Math.max(1, state.total_count),
    )}`;
    this.hint_sequence_frame_value.textContent = `Frame ${state.current_index + 1} / ${Math.max(1, state.total_count)}`;
  }

  target_resolution(): number {
    return resolve_target_resolution(
      this.selected_resolution_preset_id(),
      this.m_auto_target_resolution,
    );
  }

  private sync_stage_aspect_styles(
    canvas_aspect_ratio: string,
    video_aspect_ratio: string,
  ): void {
    this.source_video.style.aspectRatio = video_aspect_ratio;
    this.source_canvas.style.aspectRatio = canvas_aspect_ratio;
    this.output_canvas.style.aspectRatio = canvas_aspect_ratio;
  }

  sync_stage_resolution(size: number): void {
    this.m_stage_target_resolution = size;
    const stage_dimensions =
      this.m_stage_canvas_lock ??
      resolve_stage_canvas_dimensions(
        size,
        this.m_stage_display_width,
        this.m_stage_display_height,
      );
    this.source_canvas.width = stage_dimensions.width;
    this.source_canvas.height = stage_dimensions.height;
    this.output_canvas.width = stage_dimensions.width;
    this.output_canvas.height = stage_dimensions.height;
    this.working_canvas.width = size;
    this.working_canvas.height = size;
  }

  set_stage_aspect_ratio(width: number, height: number): void {
    if (Number.isFinite(width) && Number.isFinite(height)) {
      const normalized_width = Math.round(width);
      const normalized_height = Math.round(height);
      if (normalized_width > 0 && normalized_height > 0) {
        this.m_stage_display_width = normalized_width;
        this.m_stage_display_height = normalized_height;
      }
    }

    const video_aspect_ratio = resolve_stage_aspect_ratio(
      width,
      height,
      DEFAULT_STAGE_VIDEO_ASPECT_RATIO,
    );
    const canvas_aspect_ratio =
      this.m_locked_stage_aspect_ratio ?? resolve_stage_aspect_ratio(width, height);
    this.sync_stage_aspect_styles(canvas_aspect_ratio, video_aspect_ratio);
    this.sync_stage_resolution(this.m_stage_target_resolution);
  }

  reset_stage_aspect_ratio(): void {
    this.m_stage_display_width = 1;
    this.m_stage_display_height = 1;
    this.sync_stage_aspect_styles(
      this.m_locked_stage_aspect_ratio ?? DEFAULT_STAGE_CANVAS_ASPECT_RATIO,
      DEFAULT_STAGE_VIDEO_ASPECT_RATIO,
    );
    this.sync_stage_resolution(this.m_stage_target_resolution);
  }

  lock_stage_canvas_to_display_dimensions(width: number, height: number): void {
    this.m_stage_canvas_lock = resolve_stage_canvas_dimensions(
      this.m_stage_target_resolution,
      width,
      height,
    );
    this.m_locked_stage_aspect_ratio = resolve_stage_aspect_ratio(width, height);
    this.sync_stage_aspect_styles(
      this.m_locked_stage_aspect_ratio,
      resolve_stage_aspect_ratio(width, height, DEFAULT_STAGE_VIDEO_ASPECT_RATIO),
    );
    this.sync_stage_resolution(this.m_stage_target_resolution);
  }

  unlock_stage_canvas_dimensions(): void {
    if (
      this.m_stage_canvas_lock === null &&
      this.m_locked_stage_aspect_ratio === null
    ) {
      return;
    }

    this.m_stage_canvas_lock = null;
    this.m_locked_stage_aspect_ratio = null;
    this.sync_stage_aspect_styles(
      resolve_stage_aspect_ratio(
        this.m_stage_display_width,
        this.m_stage_display_height,
      ),
      resolve_stage_aspect_ratio(
        this.m_stage_display_width,
        this.m_stage_display_height,
        DEFAULT_STAGE_VIDEO_ASPECT_RATIO,
      ),
    );
    this.sync_stage_resolution(this.m_stage_target_resolution);
  }

  set_source_visual_mode(mode: "video" | "still"): void {
    this.source_video.classList.toggle("hidden", mode !== "video");
  }

  load_video_source(video_url: string): void {
    this.source_video.srcObject = null;
    this.source_video.src = video_url;
    this.source_video.load();
  }

  attach_camera_stream(stream: MediaStream): void {
    this.source_video.src = "";
    this.source_video.srcObject = stream;
  }

  clear_video_source(): void {
    this.source_video.pause();
    this.source_video.srcObject = null;
    this.source_video.removeAttribute("src");
    this.source_video.load();
  }

  set_hint_visual_mode(mode: "video" | "still"): void {
    this.hint_video.classList.toggle("hidden", mode !== "video");
  }

  load_hint_video_source(video_url: string): void {
    this.hint_video.src = video_url;
    this.hint_video.load();
  }

  hint_video_ready(): boolean {
    return this.hint_video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA;
  }

  clear_hint_video_source(): void {
    this.hint_video.pause();
    this.hint_video.removeAttribute("src");
    this.hint_video.load();
  }

  video_ready(): boolean {
    return this.source_video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA;
  }

  video_has_metadata(): boolean {
    return this.source_video.readyState >= HTMLMediaElement.HAVE_METADATA;
  }

  video_dimensions(): { width: number; height: number } {
    return {
      width: this.source_video.videoWidth,
      height: this.source_video.videoHeight,
    };
  }

  video_paused(): boolean {
    return this.source_video.paused;
  }

  video_duration(): number {
    return Number.isFinite(this.source_video.duration)
      ? this.source_video.duration
      : 0;
  }

  video_current_time(): number {
    return this.source_video.currentTime;
  }

  video_ended(): boolean {
    return this.source_video.ended;
  }

  async seek_video(time_seconds: number): Promise<Result<void, AppError>> {
    return this.seek_media_element(this.source_video, time_seconds);
  }

  async play_video(): Promise<Result<void, AppError>> {
    try {
      await this.source_video.play();
      return ok(undefined);
    } catch (cause) {
      return err(
        app_error(
          "invalid_state",
          "The browser refused to start video playback.",
          cause,
        ),
      );
    }
  }

  pause_video(): void {
    this.source_video.pause();
  }

  hint_video_has_metadata(): boolean {
    return this.hint_video.readyState >= HTMLMediaElement.HAVE_METADATA;
  }

  async seek_hint_video(time_seconds: number): Promise<Result<void, AppError>> {
    return this.seek_media_element(this.hint_video, time_seconds);
  }

  async play_hint_video(): Promise<Result<void, AppError>> {
    try {
      await this.hint_video.play();
      return ok(undefined);
    } catch (cause) {
      return err(
        app_error(
          "invalid_state",
          "The browser refused to start hint video playback.",
          cause,
        ),
      );
    }
  }

  pause_hint_video(): void {
    this.hint_video.pause();
  }

  private draw_canvas_image_source(source: CanvasImageSource): RgbaFrame {
    const size = this.target_resolution();
    const display_dimensions = resolve_canvas_source_dimensions(source);
    const stage_width = this.source_canvas.width;
    const stage_height = this.source_canvas.height;
    const stage_rect = resolve_stage_draw_rect(
      stage_width,
      stage_height,
      display_dimensions?.width ?? stage_width,
      display_dimensions?.height ?? stage_height,
    );
    this.source_ctx.clearRect(0, 0, stage_width, stage_height);
    this.working_ctx.clearRect(0, 0, size, size);
    this.source_ctx.drawImage(
      source,
      stage_rect.x,
      stage_rect.y,
      stage_rect.width,
      stage_rect.height,
    );
    this.working_ctx.drawImage(source, 0, 0, size, size);
    const image_data = this.working_ctx.getImageData(0, 0, size, size);

    return {
      width: image_data.width,
      height: image_data.height,
      data: image_data.data,
      display_width: display_dimensions?.width,
      display_height: display_dimensions?.height,
    };
  }

  draw_video_frame(): Result<RgbaFrame, AppError> {
    if (!this.video_ready()) {
      return err(
        app_error(
          "invalid_state",
          "The source video is not ready for frame extraction.",
        ),
      );
    }

    return ok(this.draw_canvas_image_source(this.source_video));
  }

  draw_bitmap_frame(source: ImageBitmap): Result<RgbaFrame, AppError> {
    return ok(this.draw_canvas_image_source(source));
  }

  private capture_hint_canvas(source: CanvasImageSource): Float32Array {
    const size = this.target_resolution();
    this.hint_canvas.width = size;
    this.hint_canvas.height = size;
    this.hint_ctx.clearRect(0, 0, size, size);
    this.hint_ctx.drawImage(source, 0, 0, size, size);
    const image_data = this.hint_ctx.getImageData(0, 0, size, size);
    const plane_size = size * size;
    const output = new Float32Array(plane_size);

    for (let index = 0; index < plane_size; index += 1) {
      const rgba_index = index * 4;
      const red = image_data.data[rgba_index] / 255;
      const green = image_data.data[rgba_index + 1] / 255;
      const blue = image_data.data[rgba_index + 2] / 255;
      output[index] = 0.299 * red + 0.587 * green + 0.114 * blue;
    }

    return output;
  }

  draw_hint_video_frame(): Result<Float32Array, AppError> {
    if (!this.hint_video_ready()) {
      return err(
        app_error(
          "invalid_state",
          "The hint video is not ready for frame extraction.",
        ),
      );
    }

    return ok(this.capture_hint_canvas(this.hint_video));
  }

  draw_hint_bitmap_frame(bitmap: ImageBitmap): Float32Array {
    return this.capture_hint_canvas(bitmap);
  }

  render_output(frame: RgbaFrame): void {
    const image_data = new ImageData(frame.data, frame.width, frame.height);
    const output_rect = resolve_stage_draw_rect(
      this.output_canvas.width,
      this.output_canvas.height,
      frame.display_width ?? frame.width,
      frame.display_height ?? frame.height,
    );
    this.output_frame_canvas.width = frame.width;
    this.output_frame_canvas.height = frame.height;
    this.output_frame_ctx.putImageData(image_data, 0, 0);
    fill_checkerboard(
      this.output_ctx,
      this.output_canvas.width,
      this.output_canvas.height,
    );
    this.output_ctx.drawImage(
      this.output_frame_canvas,
      output_rect.x,
      output_rect.y,
      output_rect.width,
      output_rect.height,
    );
  }

  capture_output_stream(fps: number): MediaStream {
    return this.output_canvas.captureStream(fps);
  }

  async output_png_blob(): Promise<Result<Blob, AppError>> {
    return new Promise<Result<Blob, AppError>>((resolve) => {
      this.output_canvas.toBlob((blob) => {
        if (blob === null) {
          resolve(
            err(
              app_error(
                "recording_failed",
                "Failed to encode the output canvas as a PNG.",
              ),
            ),
          );
          return;
        }

        resolve(ok(blob));
      }, "image/png");
    });
  }

  set_status(message: string): void {
    this.status_log.textContent = message;
  }

  set_metrics(metrics: ViewMetrics): void {
    this.mode_value.textContent = metrics.mode;
    this.backend_value.textContent = metrics.backend;
    this.frame_time_value.textContent =
      metrics.frame_time_ms === null
        ? "-"
        : `${metrics.frame_time_ms.toFixed(1)} ms`;
    this.recording_value.textContent = metrics.recording;
  }

  set_model_state(state: ViewModelState): void {
    this.active_model_value.textContent = state.active_model_label;
    this.model_state_value.textContent = state.state_label;
    this.model_progress_detail.textContent = state.detail;

    if (state.progress_ratio === null) {
      this.model_progress.removeAttribute("value");
      return;
    }

    this.model_progress.value = Math.max(0, Math.min(1, state.progress_ratio));
  }

  set_processing_state(state: ViewProcessingState): void {
    this.process_panel.classList.toggle("hidden", !state.visible);
    this.process_stage_value.textContent = state.stage_label;
    this.process_progress_detail.textContent = state.detail;

    if (state.progress_ratio === null) {
      this.process_progress.removeAttribute("value");
      return;
    }

    this.process_progress.value = Math.max(
      0,
      Math.min(1, state.progress_ratio),
    );
  }

  set_button_state(state: ViewButtonsState): void {
    this.load_selected_model_button.disabled = !state.can_load_selected_model;
    this.process_frame_button.disabled = !state.can_process_frame;
    this.process_full_media_button.disabled = !state.can_process_full_media;
    this.start_preview_button.disabled = !state.can_start_preview;
    this.stop_preview_button.disabled = !state.can_stop_preview;
    this.start_recording_button.disabled = !state.can_start_recording;
    this.stop_recording_button.disabled = !state.can_stop_recording;
    this.start_webcam_button.disabled = !state.can_start_webcam;
    this.stop_webcam_button.disabled = !state.can_stop_webcam;
    this.hint_files_input.disabled = !state.can_load_hint;
  }

  set_download_link(url: string | null): void {
    if (url === null) {
      this.download_link.removeAttribute("href");
      this.download_link.classList.add("disabled");
      return;
    }

    this.download_link.href = url;
    this.download_link.download = DEFAULT_DOWNLOAD_FILENAME;
    this.download_link.classList.remove("disabled");
  }

  set_download_artifact(
    artifact: { url: string; filename: string } | null,
  ): void {
    if (artifact === null) {
      this.set_download_link(null);
      return;
    }

    this.download_link.href = artifact.url;
    this.download_link.download = artifact.filename;
    this.download_link.classList.remove("disabled");
  }

  private async seek_media_element(
    element: HTMLVideoElement,
    time_seconds: number,
  ): Promise<Result<void, AppError>> {
    if (element.readyState < HTMLMediaElement.HAVE_METADATA) {
      return err(
        app_error(
          "invalid_state",
          "The video metadata is not ready for timeline seeking.",
        ),
      );
    }

    const duration = Number.isFinite(element.duration) ? element.duration : 0;
    const clamped_time = Math.max(0, Math.min(time_seconds, duration || time_seconds));

    if (Math.abs(element.currentTime - clamped_time) < 0.001) {
      return ok(undefined);
    }

    return new Promise<Result<void, AppError>>((resolve) => {
      const handle_seeked = () => {
        cleanup();
        resolve(ok(undefined));
      };
      const handle_error = () => {
        cleanup();
        resolve(
          err(
            app_error(
              "invalid_state",
              "The browser failed to seek the requested video frame.",
            ),
          ),
        );
      };
      const cleanup = () => {
        element.removeEventListener("seeked", handle_seeked);
        element.removeEventListener("error", handle_error);
      };

      element.addEventListener("seeked", handle_seeked, { once: true });
      element.addEventListener("error", handle_error, { once: true });
      element.currentTime = clamped_time;
    });
  }
}
