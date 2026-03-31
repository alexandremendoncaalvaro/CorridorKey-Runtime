import { app_error, type AppError } from "../common/errors";
import { err, ok, type Result } from "../common/result";

export interface BinaryAssetProgress {
  loaded_bytes: number;
  total_bytes: number | null;
  ratio: number | null;
}

function parse_total_bytes(response: Response): number | null {
  const content_length = response.headers.get("content-length");
  if (content_length === null) {
    return null;
  }

  const parsed = Number.parseInt(content_length, 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : null;
}

function combine_chunks(
  chunks: readonly Uint8Array[],
  total_bytes: number,
): Uint8Array {
  const data = new Uint8Array(total_bytes);
  let offset = 0;

  for (const chunk of chunks) {
    data.set(chunk, offset);
    offset += chunk.length;
  }

  return data;
}

export async function load_binary_asset(
  asset_url: string,
  on_progress?: (progress: BinaryAssetProgress) => void,
): Promise<Result<Uint8Array, AppError>> {
  try {
    const response = await fetch(asset_url);
    if (!response.ok) {
      return err(
        app_error(
          "model_load_failed",
          `Failed to fetch model asset: HTTP ${response.status}.`,
        ),
      );
    }

    const total_bytes = parse_total_bytes(response);
    if (response.body === null) {
      const data = new Uint8Array(await response.arrayBuffer());
      on_progress?.({
        loaded_bytes: data.byteLength,
        total_bytes: data.byteLength,
        ratio: 1,
      });
      return ok(data);
    }

    const reader = response.body.getReader();
    const chunks: Uint8Array[] = [];
    let loaded_bytes = 0;

    while (true) {
      const { done, value } = await reader.read();
      if (done) {
        break;
      }

      if (value === undefined) {
        continue;
      }

      chunks.push(value);
      loaded_bytes += value.byteLength;
      on_progress?.({
        loaded_bytes,
        total_bytes,
        ratio: total_bytes === null ? null : loaded_bytes / total_bytes,
      });
    }

    const data = combine_chunks(chunks, loaded_bytes);
    on_progress?.({
      loaded_bytes,
      total_bytes: total_bytes ?? loaded_bytes,
      ratio: 1,
    });
    return ok(data);
  } catch (cause) {
    return err(
      app_error(
        "model_load_failed",
        "Failed to fetch the bundled model asset.",
        cause,
      ),
    );
  }
}
