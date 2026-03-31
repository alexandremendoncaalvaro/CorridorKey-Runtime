import JSZip from "jszip";
import {
  ALL_FORMATS,
  BlobSource,
  BufferTarget,
  CanvasSink,
  CanvasSource,
  Input,
  Mp4OutputFormat,
  Output,
  QUALITY_HIGH,
  WebMOutputFormat,
  type VideoCodec,
} from "mediabunny";
import { SEQUENCE_PREVIEW_FPS } from "../common/constants";
import { app_error, type AppError } from "../common/errors";
import {
  create_alpha_archive_filename,
  create_alpha_frame_filename,
  create_alpha_png_filename,
  create_preview_png_filename,
  create_preview_video_filename,
  type BrowserExportArtifact,
  type BrowserExportManifest,
  type BrowserPreviewFormat,
} from "../common/browser_export_artifacts";
import { err, ok, type Result } from "../common/result";
import type { ProcessedFrame } from "../core/image_types";
import { resolve_stage_draw_rect } from "../ui/stage_aspect_ratio";

export interface FullMediaExportProgress {
  stage_label: string;
  detail: string;
  progress_ratio: number | null;
}

export interface FullMediaExportResult {
  preview_artifact: BrowserExportArtifact | null;
  alpha_artifact: BrowserExportArtifact;
  preview_format: BrowserPreviewFormat | "png";
  frame_count: number;
}

export interface VideoExportOptions {
  source_file: File;
  hint_video_file?: File;
  source_name: string;
  model_id: string;
  resolution: number;
  preview_format: BrowserPreviewFormat;
  preview_codec: VideoCodec;
  on_progress?: (progress: FullMediaExportProgress) => void;
  process_frame: (
    source: CanvasImageSource,
    hint: CanvasImageSource | null,
  ) => Promise<Result<ProcessedFrame, AppError>>;
  resolve_hint_at_timestamp: (
    timestamp_seconds: number,
  ) => Promise<Result<CanvasImageSource | null, AppError>>;
}

export interface SequenceExportOptions {
  source_files: File[];
  source_name: string;
  model_id: string;
  resolution: number;
  preview_format: BrowserPreviewFormat;
  preview_codec: VideoCodec;
  on_progress?: (progress: FullMediaExportProgress) => void;
  process_frame: (
    source: CanvasImageSource,
    hint: CanvasImageSource | null,
  ) => Promise<Result<ProcessedFrame, AppError>>;
  resolve_hint_at_index: (
    frame_index: number,
  ) => Promise<Result<CanvasImageSource | null, AppError>>;
}

export interface ImageExportOptions {
  source_bitmap: ImageBitmap;
  source_name: string;
  model_id: string;
  resolution: number;
  on_progress?: (progress: FullMediaExportProgress) => void;
  process_frame: (
    source: CanvasImageSource,
    hint: CanvasImageSource | null,
  ) => Promise<Result<ProcessedFrame, AppError>>;
  resolve_hint: () => Promise<Result<CanvasImageSource | null, AppError>>;
}

function require_2d_context(
  canvas: HTMLCanvasElement,
): CanvasRenderingContext2D {
  const context = canvas.getContext("2d", {
    alpha: true,
    willReadFrequently: true,
  });
  if (context === null) {
    throw new Error("Failed to acquire a 2D canvas context.");
  }

  return context;
}

function create_work_canvas(width: number, height: number): {
  canvas: HTMLCanvasElement;
  context: CanvasRenderingContext2D;
} {
  const canvas = document.createElement("canvas");
  canvas.width = Math.max(1, Math.round(width));
  canvas.height = Math.max(1, Math.round(height));

  return {
    canvas,
    context: require_2d_context(canvas),
  };
}

async function canvas_to_png_blob(
  canvas: HTMLCanvasElement,
): Promise<Result<Blob, AppError>> {
  return new Promise<Result<Blob, AppError>>((resolve) => {
    canvas.toBlob((blob) => {
      if (blob === null) {
        resolve(
          err(
            app_error(
              "recording_failed",
              "Failed to encode the export canvas as PNG.",
            ),
          ),
        );
        return;
      }

      resolve(ok(blob));
    }, "image/png");
  });
}

function render_processed_frame_to_canvas(
  frame: ProcessedFrame["output_frame"],
  display_width: number,
  display_height: number,
  target_canvas: HTMLCanvasElement,
  target_context: CanvasRenderingContext2D,
  scratch_canvas: HTMLCanvasElement,
  scratch_context: CanvasRenderingContext2D,
): void {
  const width = Math.max(1, Math.round(display_width));
  const height = Math.max(1, Math.round(display_height));
  target_canvas.width = width;
  target_canvas.height = height;
  scratch_canvas.width = frame.width;
  scratch_canvas.height = frame.height;
  scratch_context.putImageData(
    new ImageData(frame.data, frame.width, frame.height),
    0,
    0,
  );
  target_context.clearRect(0, 0, width, height);
  const draw_rect = resolve_stage_draw_rect(
    width,
    height,
    display_width,
    display_height,
  );
  target_context.drawImage(
    scratch_canvas,
    draw_rect.x,
    draw_rect.y,
    draw_rect.width,
    draw_rect.height,
  );
}

async function encode_alpha_plane_png(
  alpha: Float32Array,
  width: number,
  height: number,
): Promise<Result<Blob, AppError>> {
  const canvas_bundle = create_work_canvas(width, height);
  const image_data = canvas_bundle.context.createImageData(width, height);

  for (let index = 0; index < width * height; index += 1) {
    const value = Math.max(0, Math.min(255, Math.round(alpha[index] * 255)));
    const rgba_index = index * 4;
    image_data.data[rgba_index] = value;
    image_data.data[rgba_index + 1] = value;
    image_data.data[rgba_index + 2] = value;
    image_data.data[rgba_index + 3] = 255;
  }

  canvas_bundle.context.putImageData(image_data, 0, 0);
  return canvas_to_png_blob(canvas_bundle.canvas);
}

function create_output(
  preview_format: BrowserPreviewFormat,
): {
  target: BufferTarget;
  output: Output;
} {
  const target = new BufferTarget();
  const output = new Output({
    format:
      preview_format === "mp4"
        ? new Mp4OutputFormat()
        : new WebMOutputFormat(),
    target,
  });

  return { target, output };
}

function create_manifest(
  source_name: string,
  model_id: string,
  resolution: number,
  frame_count: number,
  width: number,
  height: number,
  preview_format: BrowserPreviewFormat | "png",
): BrowserExportManifest {
  return {
    source_name,
    model_id,
    resolution,
    frame_count,
    width,
    height,
    preview_format,
  };
}

function target_buffer_to_blob(
  target: BufferTarget,
  mime_type: string,
): Result<Blob, AppError> {
  if (target.buffer === null) {
    return err(
      app_error(
        "recording_failed",
        "The preview encoder finished without producing a file buffer.",
      ),
    );
  }

  return ok(new Blob([target.buffer], { type: mime_type }));
}

export async function initialize_video_output(
  output: Output,
  source: CanvasSource,
  metadata?: { frameRate?: number },
): Promise<void> {
  if (metadata === undefined) {
    output.addVideoTrack(source);
  } else {
    output.addVideoTrack(source, metadata);
  }
  await output.start();
}

export class FullMediaExportService {
  async export_video(
    options: VideoExportOptions,
  ): Promise<Result<FullMediaExportResult, AppError>> {
    const input = new Input({
      source: new BlobSource(options.source_file),
      formats: ALL_FORMATS,
    });
    const source_track = await input.getPrimaryVideoTrack();
    if (source_track === null) {
      return err(
        app_error(
          "missing_input",
          "The selected source file does not contain a readable video track.",
        ),
      );
    }

    const source_sink = new CanvasSink(source_track);
    const hint_sink = await this.create_video_hint_resolver(
      options.hint_video_file,
      options.resolve_hint_at_timestamp,
    );
    const preview_canvas_bundle = create_work_canvas(
      source_track.displayWidth,
      source_track.displayHeight,
    );
    const scratch_canvas_bundle = create_work_canvas(
      source_track.displayWidth,
      source_track.displayHeight,
    );
    const preview_source = new CanvasSource(preview_canvas_bundle.canvas, {
      codec: options.preview_codec,
      bitrate: QUALITY_HIGH,
    });
    const output_bundle = create_output(options.preview_format);
    await initialize_video_output(output_bundle.output, preview_source);
    const alpha_zip = new JSZip();
    let frame_count = 0;
    let alpha_width = 0;
    let alpha_height = 0;

    options.on_progress?.({
      stage_label: "decode",
      detail: "Preparing the source video for deterministic export.",
      progress_ratio: 0,
    });

    for await (const wrapped_canvas of source_sink.canvases()) {
      const hint_result = await hint_sink(wrapped_canvas.timestamp);
      if (!hint_result.ok) {
        return hint_result;
      }

      const processed_result = await options.process_frame(
        wrapped_canvas.canvas,
        hint_result.value,
      );
      if (!processed_result.ok) {
        return processed_result;
      }

      frame_count += 1;
      alpha_width = processed_result.value.result.width;
      alpha_height = processed_result.value.result.height;
      render_processed_frame_to_canvas(
        processed_result.value.output_frame,
        source_track.displayWidth,
        source_track.displayHeight,
        preview_canvas_bundle.canvas,
        preview_canvas_bundle.context,
        scratch_canvas_bundle.canvas,
        scratch_canvas_bundle.context,
      );
      await preview_source.add(
        wrapped_canvas.timestamp,
        wrapped_canvas.duration > 0 ? wrapped_canvas.duration : undefined,
      );

      const alpha_blob_result = await encode_alpha_plane_png(
        processed_result.value.result.alpha,
        processed_result.value.result.width,
        processed_result.value.result.height,
      );
      if (!alpha_blob_result.ok) {
        return alpha_blob_result;
      }

      alpha_zip.file(
        create_alpha_frame_filename(frame_count - 1),
        alpha_blob_result.value,
      );

      options.on_progress?.({
        stage_label: "export",
        detail: `Processed video frame ${frame_count}.`,
        progress_ratio: null,
      });
    }

    if (frame_count === 0) {
      return err(
        app_error(
          "recording_failed",
          "The source video did not yield any decodable frames for export.",
        ),
      );
    }

    alpha_zip.file(
      "manifest.json",
      JSON.stringify(
        create_manifest(
          options.source_name,
          options.model_id,
          options.resolution,
          frame_count,
          alpha_width,
          alpha_height,
          options.preview_format,
        ),
        null,
        2,
      ),
    );

    await output_bundle.output.finalize();
    const preview_mime_type = await output_bundle.output.getMimeType();
    const preview_blob_result = target_buffer_to_blob(
      output_bundle.target,
      preview_mime_type,
    );
    if (!preview_blob_result.ok) {
      return preview_blob_result;
    }

    options.on_progress?.({
      stage_label: "archive",
      detail: "Packing the alpha sequence archive.",
      progress_ratio: 0.98,
    });
    const alpha_archive_blob = await alpha_zip.generateAsync({ type: "blob" });

    return ok({
      preview_artifact: {
        kind: "preview_video",
        filename: create_preview_video_filename(
          options.source_name,
          options.preview_format,
        ),
        mime_type: preview_mime_type,
        blob: preview_blob_result.value,
      },
      alpha_artifact: {
        kind: "alpha_archive",
        filename: create_alpha_archive_filename(options.source_name),
        mime_type: "application/zip",
        blob: alpha_archive_blob,
      },
      preview_format: options.preview_format,
      frame_count,
    });
  }

  async export_sequence(
    options: SequenceExportOptions,
  ): Promise<Result<FullMediaExportResult, AppError>> {
    const first_bitmap_result = await this.create_bitmap(options.source_files[0]);
    if (!first_bitmap_result.ok) {
      return first_bitmap_result;
    }

    const preview_canvas_bundle = create_work_canvas(
      first_bitmap_result.value.width,
      first_bitmap_result.value.height,
    );
    const scratch_canvas_bundle = create_work_canvas(
      first_bitmap_result.value.width,
      first_bitmap_result.value.height,
    );
    const preview_source = new CanvasSource(preview_canvas_bundle.canvas, {
      codec: options.preview_codec,
      bitrate: QUALITY_HIGH,
    });
    const output_bundle = create_output(options.preview_format);
    await initialize_video_output(output_bundle.output, preview_source, {
      frameRate: SEQUENCE_PREVIEW_FPS,
    });
    const alpha_zip = new JSZip();
    let alpha_width = 0;
    let alpha_height = 0;

    for (let index = 0; index < options.source_files.length; index += 1) {
      const bitmap_result =
        index === 0
          ? first_bitmap_result
          : await this.create_bitmap(options.source_files[index]);
      if (!bitmap_result.ok) {
        return bitmap_result;
      }

      const hint_result = await options.resolve_hint_at_index(index);
      if (!hint_result.ok) {
        return hint_result;
      }

      const processed_result = await options.process_frame(
        bitmap_result.value,
        hint_result.value,
      );
      if (!processed_result.ok) {
        return processed_result;
      }

      alpha_width = processed_result.value.result.width;
      alpha_height = processed_result.value.result.height;
      render_processed_frame_to_canvas(
        processed_result.value.output_frame,
        bitmap_result.value.width,
        bitmap_result.value.height,
        preview_canvas_bundle.canvas,
        preview_canvas_bundle.context,
        scratch_canvas_bundle.canvas,
        scratch_canvas_bundle.context,
      );
      await preview_source.add(index / SEQUENCE_PREVIEW_FPS, 1 / SEQUENCE_PREVIEW_FPS);

      const alpha_blob_result = await encode_alpha_plane_png(
        processed_result.value.result.alpha,
        processed_result.value.result.width,
        processed_result.value.result.height,
      );
      if (!alpha_blob_result.ok) {
        return alpha_blob_result;
      }

      alpha_zip.file(create_alpha_frame_filename(index), alpha_blob_result.value);
      bitmap_result.value.close();
      options.on_progress?.({
        stage_label: "export",
        detail: `Processed sequence frame ${index + 1} / ${options.source_files.length}.`,
        progress_ratio: (index + 1) / options.source_files.length,
      });
    }

    alpha_zip.file(
      "manifest.json",
      JSON.stringify(
        create_manifest(
          options.source_name,
          options.model_id,
          options.resolution,
          options.source_files.length,
          alpha_width,
          alpha_height,
          options.preview_format,
        ),
        null,
        2,
      ),
    );

    await output_bundle.output.finalize();
    const preview_mime_type = await output_bundle.output.getMimeType();
    const preview_blob_result = target_buffer_to_blob(
      output_bundle.target,
      preview_mime_type,
    );
    if (!preview_blob_result.ok) {
      return preview_blob_result;
    }

    return ok({
      preview_artifact: {
        kind: "preview_video",
        filename: create_preview_video_filename(
          options.source_name,
          options.preview_format,
        ),
        mime_type: preview_mime_type,
        blob: preview_blob_result.value,
      },
      alpha_artifact: {
        kind: "alpha_archive",
        filename: create_alpha_archive_filename(options.source_name),
        mime_type: "application/zip",
        blob: await alpha_zip.generateAsync({ type: "blob" }),
      },
      preview_format: options.preview_format,
      frame_count: options.source_files.length,
    });
  }

  async export_image(
    options: ImageExportOptions,
  ): Promise<Result<FullMediaExportResult, AppError>> {
    options.on_progress?.({
      stage_label: "image",
      detail: "Rendering the browser preview and alpha output.",
      progress_ratio: 0.2,
    });
    const hint_result = await options.resolve_hint();
    if (!hint_result.ok) {
      return hint_result;
    }

    const processed_result = await options.process_frame(
      options.source_bitmap,
      hint_result.value,
    );
    if (!processed_result.ok) {
      return processed_result;
    }

    const preview_canvas_bundle = create_work_canvas(
      options.source_bitmap.width,
      options.source_bitmap.height,
    );
    const scratch_canvas_bundle = create_work_canvas(
      processed_result.value.output_frame.width,
      processed_result.value.output_frame.height,
    );
    render_processed_frame_to_canvas(
      processed_result.value.output_frame,
      options.source_bitmap.width,
      options.source_bitmap.height,
      preview_canvas_bundle.canvas,
      preview_canvas_bundle.context,
      scratch_canvas_bundle.canvas,
      scratch_canvas_bundle.context,
    );
    const preview_blob_result = await canvas_to_png_blob(preview_canvas_bundle.canvas);
    if (!preview_blob_result.ok) {
      return preview_blob_result;
    }

    const alpha_blob_result = await encode_alpha_plane_png(
      processed_result.value.result.alpha,
      processed_result.value.result.width,
      processed_result.value.result.height,
    );
    if (!alpha_blob_result.ok) {
      return alpha_blob_result;
    }

    return ok({
      preview_artifact: {
        kind: "preview_png",
        filename: create_preview_png_filename(options.source_name),
        mime_type: "image/png",
        blob: preview_blob_result.value,
      },
      alpha_artifact: {
        kind: "alpha_png",
        filename: create_alpha_png_filename(options.source_name),
        mime_type: "image/png",
        blob: alpha_blob_result.value,
      },
      preview_format: "png",
      frame_count: 1,
    });
  }

  private async create_video_hint_resolver(
    hint_video_file: File | undefined,
    resolve_hint_at_timestamp: (
      timestamp_seconds: number,
    ) => Promise<Result<CanvasImageSource | null, AppError>>,
  ): Promise<
    (
      timestamp_seconds: number,
    ) => Promise<Result<CanvasImageSource | null, AppError>>
  > {
    if (hint_video_file === undefined) {
      return async (timestamp_seconds: number) =>
        resolve_hint_at_timestamp(timestamp_seconds);
    }

    const input = new Input({
      source: new BlobSource(hint_video_file),
      formats: ALL_FORMATS,
    });
    const hint_track = await input.getPrimaryVideoTrack();
    if (hint_track === null) {
      return async () =>
        err(
          app_error(
            "missing_input",
            "The selected hint file does not contain a readable video track.",
          ),
        );
    }

    const hint_sink = new CanvasSink(hint_track);
    return async (timestamp_seconds: number) =>
      ok((await hint_sink.getCanvas(timestamp_seconds))?.canvas ?? null);
  }

  private async create_bitmap(file: File): Promise<Result<ImageBitmap, AppError>> {
    try {
      return ok(await createImageBitmap(file));
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
}
