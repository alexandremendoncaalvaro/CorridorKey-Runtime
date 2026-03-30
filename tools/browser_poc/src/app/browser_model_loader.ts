import type { AppError } from "../common/errors";
import type { Result } from "../common/result";
import { load_binary_asset, type BinaryAssetProgress } from "../core/asset_loader";
import type { BrowserModelDefinition, ModelSession } from "../core/image_types";
import { OnnxRuntimeModelSession } from "../core/onnx_runtime_session";

export interface BrowserModelLoadProgress extends BinaryAssetProgress {
  phase: "download" | "initialize";
  model: BrowserModelDefinition;
}

export class BrowserModelLoader {
  async load(
    model: BrowserModelDefinition,
    on_progress?: (progress: BrowserModelLoadProgress) => void,
  ): Promise<Result<ModelSession, AppError>> {
    const model_bytes = await load_binary_asset(model.url, (progress) => {
      on_progress?.({
        ...progress,
        phase: "download",
        model,
      });
    });

    if (!model_bytes.ok) {
      return model_bytes;
    }

    on_progress?.({
      loaded_bytes: model_bytes.value.byteLength,
      total_bytes: model_bytes.value.byteLength,
      ratio: 0.95,
      phase: "initialize",
      model,
    });

    return OnnxRuntimeModelSession.create(model_bytes.value);
  }
}
