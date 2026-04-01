import { err, ok, type Result } from "../common/result";
import { app_error, type AppError } from "../common/errors";
import type { ProcessResult, RgbaFrame, TileProgress } from "./image_types";

export type TileInferenceCallback = (
  frame_patch: RgbaFrame,
  hint_patch: Float32Array | undefined,
) => Promise<Result<ProcessResult, AppError>>;

function generate_hann_window(size: number): Float32Array {
  const window = new Float32Array(size * size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const wy = 0.5 * (1 - Math.cos((2 * Math.PI * y) / (size - 1)));
      const wx = 0.5 * (1 - Math.cos((2 * Math.PI * x) / (size - 1)));
      window[y * size + x] = Math.max(1e-5, wx * wy);
    }
  }
  return window;
}

export async function process_frame_tiled(
  frame: RgbaFrame,
  alpha_hint: Float32Array | undefined,
  tile_size: number,
  overlap: number,
  inference_fn: TileInferenceCallback,
  on_progress?: (progress: TileProgress) => void,
): Promise<Result<ProcessResult, AppError>> {
  if (overlap >= tile_size) {
    return err(
      app_error("invalid_state", "Tiling overlap must be strictly less than tile_size."),
    );
  }

  const stride = tile_size - overlap;
  const width = frame.width;
  const height = frame.height;

  const num_tiles_x = Math.ceil(Math.max(1, (width - tile_size) / stride + 1));
  const num_tiles_y = Math.ceil(Math.max(1, (height - tile_size) / stride + 1));
  const total_tiles = num_tiles_x * num_tiles_y;

  const window_weights = generate_hann_window(tile_size);

  const out_alpha = new Float32Array(width * height);
  const out_fg = new Float32Array(width * height * 3);
  const weight_den = new Float32Array(width * height);
  
  let has_fg = false;
  let tiles_done = 0;

  for (let ty = 0; ty < num_tiles_y; ty++) {
    for (let tx = 0; tx < num_tiles_x; tx++) {
      let x0 = tx * stride;
      let y0 = ty * stride;

      // Adjust the last tiles to tightly fit the bottom/right edges
      // This ensures we always process a full tile_size block if possible without zero-padding,
      // which improves quality drastically on the borders.
      if (x0 + tile_size > width) x0 = Math.max(0, width - tile_size);
      if (y0 + tile_size > height) y0 = Math.max(0, height - tile_size);

      const actual_w = Math.min(tile_size, width - x0);
      const actual_h = Math.min(tile_size, height - y0);

      const patch_frame: RgbaFrame = {
        width: tile_size,
        height: tile_size,
        data: new Uint8ClampedArray(tile_size * tile_size * 4),
      };

      let patch_hint: Float32Array | undefined = undefined;
      if (alpha_hint !== undefined) {
        patch_hint = new Float32Array(tile_size * tile_size);
      }

      for (let py = 0; py < actual_h; py++) {
        for (let px = 0; px < actual_w; px++) {
          const src_idx = ((y0 + py) * width + (x0 + px)) * 4;
          const dst_idx = (py * tile_size + px) * 4;
          
          patch_frame.data[dst_idx] = frame.data[src_idx];
          patch_frame.data[dst_idx + 1] = frame.data[src_idx + 1];
          patch_frame.data[dst_idx + 2] = frame.data[src_idx + 2];
          patch_frame.data[dst_idx + 3] = frame.data[src_idx + 3];

          if (alpha_hint !== undefined && patch_hint !== undefined) {
            patch_hint[py * tile_size + px] = alpha_hint[(y0 + py) * width + (x0 + px)] ?? 0;
          }
        }
      }

      const result = await inference_fn(patch_frame, patch_hint);
      if (!result.ok) {
        return result; 
      }

      // Add to accumulator
      for (let py = 0; py < actual_h; py++) {
        for (let px = 0; px < actual_w; px++) {
          const g_y = y0 + py;
          const g_x = x0 + px;
          const map_idx = g_y * width + g_x;
          const local_idx = py * tile_size + px;
          const w = window_weights[local_idx] ?? 0;

          out_alpha[map_idx] += (result.value.alpha[local_idx] ?? 0) * w;
          weight_den[map_idx] += w;
          
          if (result.value.foreground !== null) {
            has_fg = true;
            const fg_plane = tile_size * tile_size;
            const full_plane = width * height;
            out_fg[map_idx] += (result.value.foreground[local_idx] ?? 0) * w; // R
            out_fg[full_plane + map_idx] += (result.value.foreground[fg_plane + local_idx] ?? 0) * w; // G
            out_fg[2 * full_plane + map_idx] += (result.value.foreground[2 * fg_plane + local_idx] ?? 0) * w; // B
          }
        }
      }

      tiles_done++;
      if (on_progress) {
        on_progress({ current_tile: tiles_done, total_tiles });
      }
    }
  }

  // Normalize by weight denominator
  for (let i = 0; i < width * height; i++) {
    const den = Math.max(1e-5, weight_den[i] ?? 1);
    out_alpha[i] = (out_alpha[i] ?? 0) / den;
    
    if (has_fg) {
      const full_plane = width * height;
      out_fg[i] = (out_fg[i] ?? 0) / den;
      out_fg[full_plane + i] = (out_fg[full_plane + i] ?? 0) / den;
      out_fg[2 * full_plane + i] = (out_fg[2 * full_plane + i] ?? 0) / den;
    }
  }

  return ok({
    width,
    height,
    alpha: out_alpha,
    foreground: has_fg ? out_fg : null,
    mode: "model",
  });
}
