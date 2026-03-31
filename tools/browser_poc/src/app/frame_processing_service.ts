import { composite_over_checker } from "../core/compositor";
import { create_fallback_process_result } from "../core/fallback_process";
import { apply_post_process } from "../core/post_process";
import type {
  ModelSession,
  ProcessedFrame,
  RgbaFrame,
} from "../core/image_types";
import { ok, type Result } from "../common/result";
import type { AppError } from "../common/errors";

export interface FrameProcessingProgress {
  stage: "inference" | "post_process";
  progress_ratio: number;
  detail: string;
}

export class FrameProcessingService {
  private m_model_session: ModelSession | null = null;

  set_model_session(model_session: ModelSession | null): void {
    this.m_model_session?.dispose();
    this.m_model_session = model_session;
  }

  clear_model_session(): void {
    this.set_model_session(null);
  }

  has_model_session(): boolean {
    return this.m_model_session !== null;
  }

  current_backend_label(): string {
    return this.m_model_session?.backend_label ?? "fallback";
  }

  async process_frame(
    frame: RgbaFrame,
    alpha_hint?: Float32Array,
    on_progress?: (progress: FrameProcessingProgress) => void,
  ): Promise<Result<ProcessedFrame, AppError>> {
    on_progress?.({
      stage: "inference",
      progress_ratio: 0.62,
      detail:
        this.m_model_session === null
          ? "Running the rough matte fallback."
          : `Running ${this.current_backend_label()} inference in the browser.`,
    });

    const result =
      this.m_model_session === null
        ? ok(create_fallback_process_result(frame))
        : await this.m_model_session.run_frame(frame, alpha_hint);

    if (!result.ok) {
      return result;
    }

    on_progress?.({
      stage: "post_process",
      progress_ratio: 0.84,
      detail: "Applying browser-side despill and output cleanup.",
    });

    const processed_result = apply_post_process(frame, result.value);

    return ok({
      output_frame: composite_over_checker(frame, processed_result),
      result: processed_result,
      source_label:
        this.m_model_session === null
          ? "rough matte fallback"
          : "onnxruntime-web",
      backend_label: this.current_backend_label(),
    });
  }
}
