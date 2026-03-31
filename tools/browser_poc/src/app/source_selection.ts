import { app_error, type AppError } from "../common/errors";
import { err, ok, type Result } from "../common/result";

export interface SourceFileLike {
  name: string;
  type: string;
}

export type SourceSelectionKind = "video" | "image" | "sequence";

export interface SourceSelection<TFile extends SourceFileLike> {
  kind: SourceSelectionKind;
  files: TFile[];
}

const IMAGE_EXTENSIONS = [".png", ".jpg", ".jpeg", ".webp", ".bmp"];
const VIDEO_EXTENSIONS = [".mp4", ".mov", ".webm", ".m4v"];
const NAME_COLLATOR = new Intl.Collator(undefined, {
  numeric: true,
  sensitivity: "base",
});

function file_extension(name: string): string {
  const normalized_name = name.toLowerCase();
  const extension_index = normalized_name.lastIndexOf(".");
  return extension_index >= 0 ? normalized_name.slice(extension_index) : "";
}

function is_image_file(file: SourceFileLike): boolean {
  return (
    file.type.startsWith("image/") ||
    IMAGE_EXTENSIONS.includes(file_extension(file.name))
  );
}

function is_video_file(file: SourceFileLike): boolean {
  return (
    file.type.startsWith("video/") ||
    VIDEO_EXTENSIONS.includes(file_extension(file.name))
  );
}

function sort_sequence_files<TFile extends SourceFileLike>(
  files: readonly TFile[],
): TFile[] {
  return [...files].sort((left, right) =>
    NAME_COLLATOR.compare(left.name, right.name),
  );
}

export function classify_source_files<TFile extends SourceFileLike>(
  files: readonly TFile[],
): Result<SourceSelection<TFile>, AppError> {
  if (files.length === 0) {
    return err(
      app_error("missing_input", "Pick an image, image sequence, or video file."),
    );
  }

  const all_images = files.every((file) => is_image_file(file));
  if (all_images) {
    if (files.length === 1) {
      return ok({
        kind: "image",
        files: [...files],
      });
    }

    return ok({
      kind: "sequence",
      files: sort_sequence_files(files),
    });
  }

  const all_videos = files.every((file) => is_video_file(file));
  if (all_videos && files.length === 1) {
    return ok({
      kind: "video",
      files: [...files],
    });
  }

  return err(
    app_error(
      "source_load_failed",
      "Use exactly one video file or one-or-more image files.",
    ),
  );
}
