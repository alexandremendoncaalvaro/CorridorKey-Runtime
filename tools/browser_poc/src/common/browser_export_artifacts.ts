export type BrowserPreviewFormat = "mp4" | "webm";

export interface BrowserExportArtifact {
  kind: "preview_video" | "alpha_archive" | "preview_png" | "alpha_png";
  filename: string;
  mime_type: string;
  blob: Blob;
}

export interface BrowserExportManifest {
  source_name: string;
  model_id: string;
  resolution: number;
  frame_count: number;
  width: number;
  height: number;
  preview_format: BrowserPreviewFormat | "png";
}

export function file_stem(file_name: string): string {
  const extension_index = file_name.lastIndexOf(".");
  return extension_index > 0 ? file_name.slice(0, extension_index) : file_name;
}

export function create_preview_video_filename(
  source_name: string,
  format: BrowserPreviewFormat,
): string {
  return `${file_stem(source_name)}_corridorkey_preview.${format}`;
}

export function create_alpha_archive_filename(source_name: string): string {
  return `${file_stem(source_name)}_corridorkey_alpha.zip`;
}

export function create_preview_png_filename(source_name: string): string {
  return `${file_stem(source_name)}_corridorkey_preview.png`;
}

export function create_alpha_png_filename(source_name: string): string {
  return `${file_stem(source_name)}_corridorkey_alpha.png`;
}

export function create_alpha_frame_filename(frame_index: number): string {
  const normalized_index = Math.max(0, Math.floor(frame_index));
  return `alpha_${(normalized_index + 1).toString().padStart(6, "0")}.png`;
}
