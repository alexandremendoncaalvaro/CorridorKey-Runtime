export const DEFAULT_STAGE_CANVAS_ASPECT_RATIO = "1 / 1";
export const DEFAULT_STAGE_VIDEO_ASPECT_RATIO = "16 / 9";

export interface StageCanvasDimensions {
  width: number;
  height: number;
}

export interface StageDrawRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

function normalize_stage_dimensions(
  width: number,
  height: number,
): StageCanvasDimensions | null {
  if (!Number.isFinite(width) || !Number.isFinite(height)) {
    return null;
  }

  const normalized_width = Math.round(width);
  const normalized_height = Math.round(height);
  if (normalized_width <= 0 || normalized_height <= 0) {
    return null;
  }

  return {
    width: normalized_width,
    height: normalized_height,
  };
}

function normalize_stage_extent(value: number): number {
  if (!Number.isFinite(value)) {
    return 1;
  }

  return Math.max(1, Math.round(value));
}

export function resolve_stage_aspect_ratio(
  width: number,
  height: number,
  fallback = DEFAULT_STAGE_CANVAS_ASPECT_RATIO,
): string {
  const dimensions = normalize_stage_dimensions(width, height);
  if (dimensions === null) {
    return fallback;
  }

  return `${dimensions.width} / ${dimensions.height}`;
}

export function resolve_stage_canvas_dimensions(
  target_resolution: number,
  width: number,
  height: number,
): StageCanvasDimensions {
  const normalized_resolution = Math.max(1, Math.round(target_resolution));
  const dimensions = normalize_stage_dimensions(width, height);
  if (dimensions === null) {
    return {
      width: normalized_resolution,
      height: normalized_resolution,
    };
  }

  if (dimensions.width >= dimensions.height) {
    return {
      width: normalized_resolution,
      height: Math.max(
        1,
        Math.round(
          normalized_resolution * (dimensions.height / dimensions.width),
        ),
      ),
    };
  }

  return {
    width: Math.max(
      1,
      Math.round(
        normalized_resolution * (dimensions.width / dimensions.height),
      ),
    ),
    height: normalized_resolution,
  };
}

export function resolve_stage_draw_rect(
  canvas_width: number,
  canvas_height: number,
  source_width: number,
  source_height: number,
): StageDrawRect {
  const normalized_canvas_width = normalize_stage_extent(canvas_width);
  const normalized_canvas_height = normalize_stage_extent(canvas_height);
  const source_dimensions = normalize_stage_dimensions(source_width, source_height);

  if (source_dimensions === null) {
    return {
      x: 0,
      y: 0,
      width: normalized_canvas_width,
      height: normalized_canvas_height,
    };
  }

  const scale = Math.min(
    normalized_canvas_width / source_dimensions.width,
    normalized_canvas_height / source_dimensions.height,
  );
  const width = Math.max(1, Math.round(source_dimensions.width * scale));
  const height = Math.max(1, Math.round(source_dimensions.height * scale));

  return {
    x: Math.floor((normalized_canvas_width - width) / 2),
    y: Math.floor((normalized_canvas_height - height) / 2),
    width,
    height,
  };
}
