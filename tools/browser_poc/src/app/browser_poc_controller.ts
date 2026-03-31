import { app_error, type AppError } from "../common/errors";
import { MODEL_LABEL, ROUGH_MATTE_LABEL } from "../common/constants";
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
import { detect_browser_export_capability, type BrowserExportCapability } from "../common/browser_export_capability";
import { err, ok, type Result } from "../common/result";
import type { BrowserModelDefinition, ProcessMode, ProcessedFrame, RgbaFrame } from "../core/image_types";
import { BrowserModelLoader, type BrowserModelLoadProgress } from "./browser_model_loader";
import { FrameProcessingService, type FrameProcessingProgress } from "./frame_processing_service";
import { classify_source_files } from "./source_selection";
import { FullMediaExportService, type FullMediaExportProgress, type FullMediaExportResult } from "./full_media_export_service";
import { DomBrowserPocView, type ViewButtonsState, type ViewDownloadArtifacts } from "../ui/dom_browser_poc_view";

type SourceState =
  | { kind: "none" }
  | { kind: "video"; file: File }
  | { kind: "image"; file: File; bitmap: ImageBitmap }
  | { kind: "sequence"; files: File[]; current_index: number; current_bitmap: ImageBitmap | null };

type HintState =
  | { kind: "none" }
  | { kind: "video"; file: File; url: string }
  | { kind: "image"; file: File; bitmap: ImageBitmap }
  | { kind: "sequence"; files: File[]; current_index: number; current_bitmap: ImageBitmap | null };

function mode_label(mode: ProcessMode): string {
  return mode === "model" ? MODEL_LABEL : ROUGH_MATTE_LABEL;
}

function safe_close_bitmap(bitmap: ImageBitmap | null): void {
  bitmap?.close();
}

export class BrowserPocController {
  private m_frame_in_flight = false;
  private m_full_media_job_active = false;
  private m_model_load_in_flight = false;
  private m_preview_loop_active = false;
  private m_active_model_id: string | null = null;
  private m_video_url: string | null = null;
  private m_preview_download_url: string | null = null;
  private m_alpha_download_url: string | null = null;
  private m_source: SourceState = { kind: "none" };
  private m_hint: HintState = { kind: "none" };
  private m_export_capability: BrowserExportCapability = {
    supported: false,
    preview_format: null,
    codec: null,
    reason: "Checking browser export support...",
  };
  private readonly m_model_loader = new BrowserModelLoader();
  private readonly m_export_service = new FullMediaExportService();

  constructor(
    private readonly m_view: DomBrowserPocView,
    private readonly m_processing_service: FrameProcessingService,
  ) {
    this.m_view.bind_handlers({
      on_source_video_selected: () => void this.handle_source_video_selected(),
      on_source_stills_selected: () => void this.handle_source_stills_selected(),
      on_hint_files_selected: () => void this.handle_hint_files_selected(),
      on_hint_video_ready: () => void this.handle_hint_video_ready(),
      on_model_selection_changed: () => void this.handle_model_selection_changed(),
      on_resolution_changed: () => void this.handle_resolution_changed(),
      on_process_full_media_requested: () => void this.process_full_media(),
      on_video_metadata_loaded: () => void this.handle_video_metadata_loaded(),
      on_video_played: () => this.handle_video_played(),
      on_video_paused: () => void this.handle_video_paused(),
      on_video_seeked: () => void this.handle_video_seeked(),
      on_video_ended: () => this.stop_preview_loop(),
      on_sequence_index_changed: () => void this.handle_sequence_index_changed(),
      on_hint_sequence_index_changed: () => void this.handle_hint_sequence_index_changed(),
    });
    window.addEventListener("beforeunload", () => this.dispose_resources());
    this.m_view.set_model_catalog(BROWSER_MODEL_CATALOG, DEFAULT_BROWSER_MODEL_ID);
    this.m_view.set_resolution_presets(PROCESSING_RESOLUTION_PRESETS, DEFAULT_RESOLUTION_PRESET_ID);
    this.m_view.reset_stage_aspect_ratio();
    this.m_view.set_sequence_state({ visible: false, current_index: 0, total_count: 1 });
    this.m_view.set_hint_sequence_state({ visible: false, current_index: 0, total_count: 1 });
    this.apply_resolution_state();
    this.m_view.set_processing_state({
      visible: false,
      stage_label: "idle",
      detail: "Waiting for a source frame.",
      progress_ratio: 0,
    });
    this.m_view.set_status("Browser POC booting. Loading the default bundled model.");
    this.set_metrics(ROUGH_MATTE_LABEL, "none", null, "idle");
    this.m_view.set_model_state({
      active_model_label: "bootstrapping",
      state_label: "loading",
      progress_ratio: 0,
      detail: "Preparing the default bundled model.",
    });
    this.m_view.set_download_artifacts({ preview: null, alpha: null });
    this.m_view.set_export_capability(this.m_export_capability.reason);
    this.refresh_button_state();
    void this.initialize_export_capability();
    void this.load_browser_model(DEFAULT_BROWSER_MODEL_ID, true);
  }

  private async initialize_export_capability(): Promise<void> {
    this.m_export_capability = await detect_browser_export_capability();
    this.m_view.set_export_capability(this.m_export_capability.reason);
    this.refresh_button_state();
  }

  private set_metrics(
    mode: string,
    backend: string,
    frame_time_ms: number | null,
    export_state: string,
  ): void {
    this.m_view.set_metrics({ mode, backend, frame_time_ms, export_state });
  }

  private current_selected_model(): BrowserModelDefinition | null {
    return find_browser_model_definition(this.m_view.selected_model_id());
  }

  private active_model_definition(): BrowserModelDefinition | null {
    return this.m_active_model_id === null
      ? null
      : find_browser_model_definition(this.m_active_model_id);
  }

  private active_model_label(): string {
    return this.active_model_definition()?.filename ?? "rough matte fallback";
  }

  private active_model_id_for_manifest(): string {
    return this.m_active_model_id ?? "rough_matte_fallback";
  }

  private source_ready(): boolean {
    if (this.m_source.kind === "video") {
      return this.m_view.video_has_metadata();
    }
    if (this.m_source.kind === "image") {
      return true;
    }
    if (this.m_source.kind === "sequence") {
      return this.m_source.current_bitmap !== null;
    }

    return false;
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
      resolution_preset_detail(this.m_view.selected_resolution_preset_id(), auto_resolution),
    );
    this.m_view.sync_stage_resolution(this.resolved_target_resolution());
  }

  private refresh_button_state(): void {
    const blocked =
      this.m_model_load_in_flight || this.m_frame_in_flight || this.m_full_media_job_active;
    const can_run_full_export =
      this.source_ready() &&
      (this.m_source.kind === "image" || this.m_export_capability.supported);
    const state: ViewButtonsState = {
      can_change_source: !blocked,
      can_change_hint: !blocked,
      can_change_model: !blocked,
      can_change_resolution: !blocked,
      can_process_full_media: can_run_full_export && !blocked,
      can_scrub_source_sequence: this.m_source.kind === "sequence" && !blocked,
      can_scrub_hint_sequence: this.m_hint.kind === "sequence" && !blocked,
    };
    this.m_view.set_button_state(state);
  }

  private clear_download_artifacts(): void {
    if (this.m_preview_download_url !== null) {
      URL.revokeObjectURL(this.m_preview_download_url);
      this.m_preview_download_url = null;
    }
    if (this.m_alpha_download_url !== null) {
      URL.revokeObjectURL(this.m_alpha_download_url);
      this.m_alpha_download_url = null;
    }
    this.m_view.set_download_artifacts({ preview: null, alpha: null });
  }

  private set_download_artifacts(result: FullMediaExportResult): void {
    this.clear_download_artifacts();
    const artifacts: ViewDownloadArtifacts = { preview: null, alpha: null };
    if (result.preview_artifact !== null) {
      this.m_preview_download_url = URL.createObjectURL(result.preview_artifact.blob);
      artifacts.preview = {
        url: this.m_preview_download_url,
        filename: result.preview_artifact.filename,
        label:
          result.preview_artifact.kind === "preview_png"
            ? "Download Preview PNG"
            : "Download Preview Video",
      };
    }
    this.m_alpha_download_url = URL.createObjectURL(result.alpha_artifact.blob);
    artifacts.alpha = {
      url: this.m_alpha_download_url,
      filename: result.alpha_artifact.filename,
      label:
        result.alpha_artifact.kind === "alpha_png"
          ? "Download Alpha PNG"
          : "Download Alpha ZIP",
    };
    this.m_view.set_download_artifacts(artifacts);
  }

  private dispose_resources(): void {
    this.stop_preview_loop();
    this.release_source();
    this.clear_download_artifacts();
  }

  private release_video_url(): void {
    if (this.m_video_url !== null) {
      URL.revokeObjectURL(this.m_video_url);
      this.m_video_url = null;
    }
  }

  private release_source(): void {
    if (this.m_source.kind === "video") {
      this.release_video_url();
      this.m_view.clear_video_source();
    } else if (this.m_source.kind === "image") {
      safe_close_bitmap(this.m_source.bitmap);
    } else if (this.m_source.kind === "sequence") {
      safe_close_bitmap(this.m_source.current_bitmap);
    }

    this.m_source = { kind: "none" };
    this.m_view.reset_stage_aspect_ratio();
    this.m_view.set_sequence_state({ visible: false, current_index: 0, total_count: 1 });
  }

  private release_hint(): void {
    if (this.m_hint.kind === "video") {
      URL.revokeObjectURL(this.m_hint.url);
      this.m_view.clear_hint_video_source();
    } else if (this.m_hint.kind === "image") {
      safe_close_bitmap(this.m_hint.bitmap);
    } else if (this.m_hint.kind === "sequence") {
      safe_close_bitmap(this.m_hint.current_bitmap);
    }

    this.m_hint = { kind: "none" };
    this.m_view.set_hint_sequence_state({ visible: false, current_index: 0, total_count: 1 });
  }

  private prepare_for_source_change(): void {
    this.stop_preview_loop();
    this.release_source();
  }

  private async create_bitmap(file: File): Promise<Result<ImageBitmap, AppError>> {
    try {
      return ok(await createImageBitmap(file));
    } catch (cause) {
      return err(app_error("source_load_failed", `Failed to decode ${file.name}.`, cause));
    }
  }

  private async load_video_file(file: File): Promise<void> {
    this.prepare_for_source_change();
    this.clear_download_artifacts();
    this.m_video_url = URL.createObjectURL(file);
    this.m_source = { kind: "video", file };
    this.m_view.load_video_source(this.m_video_url);
    this.m_view.set_status(`Loading source video ${file.name}...`);
    this.refresh_button_state();
  }

  private async load_image_file(file: File): Promise<void> {
    this.prepare_for_source_change();
    this.clear_download_artifacts();
    const bitmap_result = await this.create_bitmap(file);
    if (!bitmap_result.ok) {
      this.m_view.set_status(bitmap_result.error.message);
      this.refresh_button_state();
      return;
    }
    this.m_source = { kind: "image", file, bitmap: bitmap_result.value };
    this.m_view.set_stage_aspect_ratio(bitmap_result.value.width, bitmap_result.value.height);
    this.m_view.set_status(`Loaded source image ${file.name}.`);
    await this.process_current_frame();
    this.refresh_button_state();
  }

  private async set_sequence_frame(index: number, process_frame: boolean): Promise<Result<void, AppError>> {
    if (this.m_source.kind !== "sequence") {
      return err(app_error("invalid_state", "No image sequence is loaded."));
    }
    const normalized_index = Math.min(Math.max(0, index), this.m_source.files.length - 1);
    const bitmap_result = await this.create_bitmap(this.m_source.files[normalized_index]);
    if (!bitmap_result.ok) {
      return bitmap_result;
    }
    safe_close_bitmap(this.m_source.current_bitmap);
    this.m_source.current_bitmap = bitmap_result.value;
    this.m_source.current_index = normalized_index;
    this.m_view.set_stage_aspect_ratio(bitmap_result.value.width, bitmap_result.value.height);
    this.m_view.set_sequence_state({
      visible: true,
      current_index: normalized_index,
      total_count: this.m_source.files.length,
    });
    const hint_sync_result = await this.sync_hint_sequence_to_source_index(normalized_index, false);
    if (!hint_sync_result.ok) {
      return hint_sync_result;
    }
    if (process_frame) {
      await this.process_current_frame();
    }
    return ok(undefined);
  }

  private async load_image_sequence(files: File[]): Promise<void> {
    this.prepare_for_source_change();
    this.clear_download_artifacts();
    this.m_source = { kind: "sequence", files, current_index: 0, current_bitmap: null };
    this.m_view.set_sequence_state({ visible: true, current_index: 0, total_count: files.length });
    const frame_result = await this.set_sequence_frame(0, true);
    if (!frame_result.ok) {
      this.m_view.set_status(frame_result.error.message);
      this.refresh_button_state();
      return;
    }
    this.m_view.set_status(`Loaded source sequence with ${files.length} frames.`);
    this.refresh_button_state();
  }

  private async load_hint_video(file: File): Promise<void> {
    this.release_hint();
    const url = URL.createObjectURL(file);
    this.m_hint = { kind: "video", file, url };
    this.m_view.load_hint_video_source(url);
    this.m_view.set_status(`Loaded hint video ${file.name}.`);
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
    this.m_hint = { kind: "image", file, bitmap: bitmap_result.value };
    this.m_view.set_status(`Loaded hint image ${file.name}.`);
    this.refresh_button_state();
  }

  private async set_hint_sequence_frame(index: number, process_frame: boolean): Promise<Result<void, AppError>> {
    if (this.m_hint.kind !== "sequence") {
      return err(app_error("invalid_state", "No hint sequence is loaded."));
    }
    const normalized_index = Math.min(Math.max(0, index), this.m_hint.files.length - 1);
    const bitmap_result = await this.create_bitmap(this.m_hint.files[normalized_index]);
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
    return ok(undefined);
  }

  private async load_hint_sequence(files: File[]): Promise<void> {
    this.release_hint();
    this.m_hint = { kind: "sequence", files, current_index: 0, current_bitmap: null };
    this.m_view.set_hint_sequence_state({ visible: true, current_index: 0, total_count: files.length });
    const frame_result = await this.set_hint_sequence_frame(0, false);
    if (!frame_result.ok) {
      this.m_view.set_status(frame_result.error.message);
      this.refresh_button_state();
      return;
    }
    this.m_view.set_status(`Loaded hint sequence with ${files.length} frames.`);
    this.refresh_button_state();
  }

  private async sync_hint_sequence_to_source_index(source_index: number, process_frame: boolean): Promise<Result<void, AppError>> {
    if (this.m_hint.kind !== "sequence") {
      return ok(undefined);
    }
    const hint_index = Math.min(Math.max(0, source_index), this.m_hint.files.length - 1);
    return this.set_hint_sequence_frame(hint_index, process_frame);
  }

  private progress_ratio(progress: BrowserModelLoadProgress): number | null {
    if (progress.phase === "initialize") {
      return 0.95;
    }
    return progress.ratio === null ? null : Math.min(0.9, progress.ratio * 0.9);
  }

  private progress_detail(progress: BrowserModelLoadProgress): string {
    if (progress.phase === "initialize") {
      return `Initializing ONNX Runtime Web for ${progress.model.filename}.`;
    }
    return progress.total_bytes === null || progress.ratio === null
      ? `Fetching ${progress.model.filename}...`
      : `Fetching ${progress.model.filename}: ${Math.round(progress.ratio * 100)}%.`;
  }

  private update_model_loading_view(progress: BrowserModelLoadProgress): void {
    this.m_view.set_model_state({
      active_model_label:
        this.m_active_model_id === null ? "bootstrapping" : this.active_model_label(),
      state_label: progress.phase === "download" ? "loading" : "initializing",
      progress_ratio: this.progress_ratio(progress),
      detail: this.progress_detail(progress),
    });
  }

  private async load_browser_model(model_id: string, is_bootstrap: boolean): Promise<void> {
    const model = find_browser_model_definition(model_id);
    if (model === null || this.m_model_load_in_flight) {
      return;
    }
    this.m_model_load_in_flight = true;
    this.m_view.set_status(
      is_bootstrap
        ? `Bootstrapping ${model.filename}...`
        : `Loading ${model.filename} from the browser catalog...`,
    );
    this.m_view.set_model_state({
      active_model_label:
        this.m_active_model_id === null ? "bootstrapping" : this.active_model_label(),
      state_label: "loading",
      progress_ratio: 0,
      detail: `Fetching ${model.filename}.`,
    });
    this.refresh_button_state();

    const session_result = await this.m_model_loader.load(model, (progress) =>
      this.update_model_loading_view(progress),
    );
    this.m_model_load_in_flight = false;
    if (!session_result.ok) {
      if (this.m_processing_service.has_model_session()) {
        this.m_view.set_model_state({
          active_model_label: this.active_model_label(),
          state_label: "ready",
          progress_ratio: 1,
          detail: `Failed to load ${model.filename}. Keeping ${this.active_model_label()} active.`,
        });
        this.m_view.set_status(`Model switch failed. ${session_result.error.message}`);
      } else {
        this.m_processing_service.clear_model_session();
        this.set_metrics(ROUGH_MATTE_LABEL, "none", null, this.m_full_media_job_active ? "exporting" : "idle");
        this.m_view.set_model_state({
          active_model_label: "rough matte fallback",
          state_label: "fallback",
          progress_ratio: 0,
          detail: `Default model failed to load. ${session_result.error.message}`,
        });
        this.m_view.set_status(
          `Default model unavailable. Falling back to rough matte. ${session_result.error.message}`,
        );
      }
      this.refresh_button_state();
      return;
    }

    this.m_processing_service.set_model_session(session_result.value);
    this.m_active_model_id = model.id;
    this.apply_resolution_state();
    this.set_metrics(
      MODEL_LABEL,
      session_result.value.backend_label,
      null,
      this.m_full_media_job_active ? "exporting" : "idle",
    );
    this.m_view.set_model_state({
      active_model_label: model.filename,
      state_label: "ready",
      progress_ratio: 1,
      detail: `${model.label} active on ${session_result.value.backend_label}.`,
    });
    this.m_view.set_status(`${model.filename} ready on ${session_result.value.backend_label}.`);
    if (this.source_ready()) {
      await this.process_current_frame();
    }
    this.refresh_button_state();
  }

  private async handle_source_video_selected(): Promise<void> {
    const files = this.m_view.selected_source_video_files();
    if (files.length > 0) {
      await this.load_video_file(files[0]);
    }
  }

  private async handle_source_stills_selected(): Promise<void> {
    const selection = classify_source_files(this.m_view.selected_source_still_files());
    if (!selection.ok) {
      this.m_view.set_status(selection.error.message);
      this.refresh_button_state();
      return;
    }
    if (selection.value.kind === "image") {
      await this.load_image_file(selection.value.files[0]);
      return;
    }
    await this.load_image_sequence(selection.value.files);
  }

  private async handle_hint_files_selected(): Promise<void> {
    const selection = classify_source_files(this.m_view.selected_hint_files());
    if (!selection.ok) {
      this.m_view.set_status(selection.error.message);
      this.refresh_button_state();
      return;
    }
    if (selection.value.kind === "video") {
      await this.load_hint_video(selection.value.files[0]);
    } else if (selection.value.kind === "image") {
      await this.load_hint_image(selection.value.files[0]);
    } else {
      await this.load_hint_sequence(selection.value.files);
    }
    if (this.source_ready() && this.m_hint.kind !== "video") {
      await this.process_current_frame();
    }
  }

  private async handle_hint_video_ready(): Promise<void> {
    if (this.source_ready()) {
      await this.process_current_frame();
    }
  }

  private async handle_model_selection_changed(): Promise<void> {
    this.apply_resolution_state();
    const selected_model = this.current_selected_model();
    if (
      selected_model !== null &&
      selected_model.id !== this.m_active_model_id &&
      !this.m_model_load_in_flight
    ) {
      await this.load_browser_model(selected_model.id, false);
      return;
    }
    if (this.source_ready()) {
      await this.process_current_frame();
    }
    this.refresh_button_state();
  }

  private async handle_resolution_changed(): Promise<void> {
    this.apply_resolution_state();
    if (this.source_ready()) {
      await this.process_current_frame();
    }
    this.refresh_button_state();
  }

  private async handle_video_metadata_loaded(): Promise<void> {
    if (this.m_source.kind !== "video") {
      return;
    }
    const dimensions = this.m_view.video_dimensions();
    this.m_view.set_stage_aspect_ratio(dimensions.width, dimensions.height);
    this.m_view.set_status(
      `Loaded source video ${this.m_source.file.name}: ${dimensions.width}x${dimensions.height}.`,
    );
    await this.process_current_frame();
    this.refresh_button_state();
  }

  private handle_video_played(): void {
    if (this.m_source.kind === "video" && !this.m_full_media_job_active) {
      this.start_preview_loop();
    }
  }

  private async handle_video_paused(): Promise<void> {
    this.stop_preview_loop();
    if (this.m_source.kind === "video" && this.source_ready()) {
      await this.process_current_frame();
    }
  }

  private async handle_video_seeked(): Promise<void> {
    if (this.m_source.kind === "video" && this.source_ready()) {
      await this.process_current_frame();
    }
  }

  private async handle_sequence_index_changed(): Promise<void> {
    if (this.m_source.kind !== "sequence") {
      return;
    }
    const result = await this.set_sequence_frame(this.m_view.selected_sequence_index(), true);
    if (!result.ok) {
      this.m_view.set_status(result.error.message);
    }
    this.refresh_button_state();
  }

  private async handle_hint_sequence_index_changed(): Promise<void> {
    if (this.m_hint.kind !== "sequence") {
      return;
    }
    const result = await this.set_hint_sequence_frame(this.m_view.selected_hint_sequence_index(), true);
    if (!result.ok) {
      this.m_view.set_status(result.error.message);
    }
    this.refresh_button_state();
  }

  private async capture_source_frame(): Promise<Result<RgbaFrame, AppError>> {
    if (this.m_source.kind === "video") {
      return this.m_view.draw_source_video_frame();
    }
    if (this.m_source.kind === "image") {
      return this.m_view.draw_canvas_frame(this.m_source.bitmap);
    }
    if (this.m_source.kind === "sequence" && this.m_source.current_bitmap !== null) {
      return this.m_view.draw_canvas_frame(this.m_source.current_bitmap);
    }
    return err(
      app_error("missing_input", "Load a source video, image, or image sequence before processing."),
    );
  }

  private async sync_hint_video_to_preview_time(): Promise<Result<void, AppError>> {
    if (this.m_hint.kind !== "video" || !this.m_view.hint_video_has_metadata()) {
      return ok(undefined);
    }

    const target_time =
      this.m_source.kind === "video" ? this.m_view.video_current_time() : 0;
    return this.m_view.seek_hint_video(target_time);
  }

  private async capture_hint_normalized(): Promise<Result<Float32Array | null, AppError>> {
    if (this.m_hint.kind === "video") {
      const sync_result =
        this.m_preview_loop_active && this.m_source.kind === "video"
          ? ok(undefined)
          : await this.sync_hint_video_to_preview_time();
      if (!sync_result.ok) {
        return sync_result;
      }
      return this.m_view.draw_hint_video_frame();
    }
    if (this.m_hint.kind === "image") {
      return ok(this.m_view.draw_hint_canvas(this.m_hint.bitmap));
    }
    if (this.m_hint.kind === "sequence" && this.m_hint.current_bitmap !== null) {
      return ok(this.m_view.draw_hint_canvas(this.m_hint.current_bitmap));
    }
    return ok(null);
  }

  private async run_processing(
    frame: RgbaFrame,
    alpha_hint: Float32Array | null,
    render_output: boolean,
    report_status: boolean,
  ): Promise<Result<ProcessedFrame, AppError>> {
    const started_at = performance.now();
    const result = await this.m_processing_service.process_frame(
      frame,
      alpha_hint ?? undefined,
      (progress: FrameProcessingProgress) => {
        this.m_view.set_processing_state({
          visible: true,
          stage_label: progress.stage === "inference" ? "inference" : "post-process",
          detail: progress.detail,
          progress_ratio: progress.progress_ratio,
        });
      },
    );
    if (!result.ok) {
      return result;
    }
    if (render_output) {
      this.m_view.render_output(result.value.output_frame);
    }
    const elapsed_ms = performance.now() - started_at;
    this.set_metrics(
      mode_label(result.value.result.mode),
      result.value.backend_label,
      elapsed_ms,
      this.m_full_media_job_active ? "exporting" : "idle",
    );
    if (report_status) {
      this.m_view.set_status(
        `Processed ${result.value.result.width}x${result.value.result.height} frame via ${result.value.source_label} in ${elapsed_ms.toFixed(1)} ms.`,
      );
    }
    return result;
  }

  async process_current_frame(): Promise<Result<void, AppError>> {
    if (this.m_frame_in_flight || !this.source_ready()) {
      return err(app_error("invalid_state", "A source frame is not ready for processing."));
    }
    this.m_frame_in_flight = true;
    this.apply_resolution_state();
    this.refresh_button_state();
    this.m_view.set_processing_state({
      visible: true,
      stage_label: "capture",
      detail: `Capturing a ${this.resolved_target_resolution()}x${this.resolved_target_resolution()} source frame.`,
      progress_ratio: 0.12,
    });
    try {
      const frame_result = await this.capture_source_frame();
      if (!frame_result.ok) {
        this.m_view.set_status(frame_result.error.message);
        return frame_result;
      }
      const hint_result = await this.capture_hint_normalized();
      if (!hint_result.ok) {
        this.m_view.set_status(hint_result.error.message);
        return hint_result;
      }
      const processed_result = await this.run_processing(frame_result.value, hint_result.value, true, true);
      if (!processed_result.ok) {
        this.m_view.set_status(processed_result.error.message);
        return processed_result;
      }
      return ok(undefined);
    } finally {
      this.m_frame_in_flight = false;
      this.m_view.set_processing_state({
        visible: false,
        stage_label: "idle",
        detail: "Waiting for a source frame.",
        progress_ratio: 0,
      });
      this.refresh_button_state();
    }
  }

  private async process_canvas_sources_for_export(
    source: CanvasImageSource,
    hint: CanvasImageSource | null,
  ): Promise<Result<ProcessedFrame, AppError>> {
    const frame_result = this.m_view.draw_canvas_frame(source);
    if (!frame_result.ok) {
      return frame_result;
    }
    const hint_plane = hint === null ? null : this.m_view.draw_hint_canvas(hint);
    return this.run_processing(frame_result.value, hint_plane, true, false);
  }

  private async resolve_video_export_hint(_timestamp_seconds: number): Promise<Result<CanvasImageSource | null, AppError>> {
    if (this.m_hint.kind === "none") {
      return ok(null);
    }
    if (this.m_hint.kind === "image") {
      return ok(this.m_hint.bitmap);
    }
    if (this.m_hint.kind === "video") {
      return ok(null);
    }
    return err(
      app_error(
        "unsupported_capability",
        "Hint image sequences are only supported with still-image sequence exports.",
      ),
    );
  }

  private async resolve_sequence_export_hint(index: number): Promise<Result<CanvasImageSource | null, AppError>> {
    if (this.m_hint.kind === "none") {
      return ok(null);
    }
    if (this.m_hint.kind === "image") {
      return ok(this.m_hint.bitmap);
    }
    if (this.m_hint.kind === "sequence") {
      const result = await this.set_hint_sequence_frame(index, false);
      if (!result.ok || this.m_hint.current_bitmap === null) {
        return err(result.ok ? app_error("invalid_state", "The hint sequence has no frame ready.") : result.error);
      }
      return ok(this.m_hint.current_bitmap);
    }
    return err(
      app_error(
        "unsupported_capability",
        "Hint videos are only supported for full video exports in this phase.",
      ),
    );
  }

  private async resolve_image_export_hint(): Promise<Result<CanvasImageSource | null, AppError>> {
    if (this.m_hint.kind === "none") {
      return ok(null);
    }
    if (this.m_hint.kind === "image") {
      return ok(this.m_hint.bitmap);
    }
    if (this.m_hint.kind === "sequence" && this.m_hint.current_bitmap !== null) {
      return ok(this.m_hint.current_bitmap);
    }
    return err(
      app_error(
        "unsupported_capability",
        "Hint videos are only supported for full video exports in this phase.",
      ),
    );
  }

  async process_full_media(): Promise<void> {
    if (this.m_full_media_job_active || this.m_frame_in_flight || !this.source_ready()) {
      return;
    }
    if (
      this.m_source.kind !== "image" &&
      (!this.m_export_capability.supported ||
        this.m_export_capability.preview_format === null ||
        this.m_export_capability.codec === null)
    ) {
      this.m_view.set_status(
        this.m_export_capability.reason ?? "This browser cannot run full export right now.",
      );
      return;
    }

    this.m_full_media_job_active = true;
    this.stop_preview_loop();
    this.apply_resolution_state();
    this.clear_download_artifacts();
    this.refresh_button_state();
    this.apply_export_progress({
      stage_label: "export",
      detail: "Preparing the full media export pipeline.",
      progress_ratio: 0,
    });
    this.set_metrics(
      this.m_processing_service.has_model_session() ? MODEL_LABEL : ROUGH_MATTE_LABEL,
      this.m_processing_service.current_backend_label(),
      null,
      "exporting",
    );

    try {
      let result: Result<FullMediaExportResult, AppError>;
      if (this.m_source.kind === "image") {
        result = await this.m_export_service.export_image({
          source_bitmap: this.m_source.bitmap,
          source_name: this.m_source.file.name,
          model_id: this.active_model_id_for_manifest(),
          resolution: this.resolved_target_resolution(),
          on_progress: (progress) => this.apply_export_progress(progress),
          resolve_hint: async () => this.resolve_image_export_hint(),
          process_frame: async (source, hint) => this.process_canvas_sources_for_export(source, hint),
        });
      } else if (this.m_source.kind === "sequence") {
        result = await this.m_export_service.export_sequence({
          source_files: this.m_source.files,
          source_name: this.m_source.files[0]?.name ?? "sequence",
          model_id: this.active_model_id_for_manifest(),
          resolution: this.resolved_target_resolution(),
          preview_format: this.m_export_capability.preview_format!,
          preview_codec: this.m_export_capability.codec!,
          on_progress: (progress) => this.apply_export_progress(progress),
          resolve_hint_at_index: async (index) => this.resolve_sequence_export_hint(index),
          process_frame: async (source, hint) => this.process_canvas_sources_for_export(source, hint),
        });
      } else if (this.m_source.kind === "video") {
        result = await this.m_export_service.export_video({
          source_file: this.m_source.file,
          hint_video_file: this.m_hint.kind === "video" ? this.m_hint.file : undefined,
          source_name: this.m_source.file.name,
          model_id: this.active_model_id_for_manifest(),
          resolution: this.resolved_target_resolution(),
          preview_format: this.m_export_capability.preview_format!,
          preview_codec: this.m_export_capability.codec!,
          on_progress: (progress) => this.apply_export_progress(progress),
          resolve_hint_at_timestamp: async (timestamp_seconds) => this.resolve_video_export_hint(timestamp_seconds),
          process_frame: async (source, hint) => this.process_canvas_sources_for_export(source, hint),
        });
      } else {
        this.m_view.set_status("No exportable source is loaded.");
        return;
      }

      if (!result.ok) {
        this.m_view.set_status(result.error.message);
        return;
      }
      this.set_download_artifacts(result.value);
      this.m_view.set_status(
        result.value.preview_artifact === null
          ? "Preview PNG and alpha PNG are ready."
          : `Preview ${result.value.preview_format.toUpperCase()} and alpha archive are ready.`,
      );
    } finally {
      this.m_full_media_job_active = false;
      this.m_view.set_processing_state({
        visible: false,
        stage_label: "idle",
        detail: "Waiting for a source frame.",
        progress_ratio: 0,
      });
      this.set_metrics(
        this.m_processing_service.has_model_session() ? MODEL_LABEL : ROUGH_MATTE_LABEL,
        this.m_processing_service.current_backend_label(),
        null,
        "ready",
      );
      this.refresh_button_state();
    }
  }

  private apply_export_progress(progress: FullMediaExportProgress): void {
    this.m_view.set_processing_state({
      visible: true,
      stage_label: progress.stage_label,
      detail: progress.detail,
      progress_ratio: progress.progress_ratio,
    });
  }

  private preview_tick = async (): Promise<void> => {
    if (!this.m_preview_loop_active || this.m_source.kind !== "video") {
      return;
    }
    if (!this.m_view.video_paused() && !this.m_view.video_ended() && !this.m_frame_in_flight) {
      await this.process_current_frame();
    }
    if (this.m_preview_loop_active) {
      requestAnimationFrame(() => void this.preview_tick());
    }
  };

  private start_preview_loop(): void {
    if (this.m_preview_loop_active || this.m_source.kind !== "video" || !this.source_ready()) {
      return;
    }
    this.m_preview_loop_active = true;
    requestAnimationFrame(() => void this.preview_tick());
  }

  private stop_preview_loop(): void {
    this.m_preview_loop_active = false;
  }
}
