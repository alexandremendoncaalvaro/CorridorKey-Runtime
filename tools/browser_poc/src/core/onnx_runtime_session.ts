import * as ort from "onnxruntime-web";
import { MAX_WASM_THREADS } from "../common/constants";
import { app_error, type AppError } from "../common/errors";
import { err, ok, type Result } from "../common/result";
import type { ModelSession, ProcessResult, RgbaFrame, InferenceStrategy, TileProgress } from "./image_types";
import { create_rough_matte } from "./rough_matte";
import { prepare_model_input } from "./model_input";
import { process_frame_tiled } from "./tiling";

let runtime_configured = false;
type SessionBackend = "webgpu" | "wasm";
const ORT_WASM_URL = new URL(
  "../../node_modules/onnxruntime-web/dist/ort-wasm-simd-threaded.jsep.wasm",
  import.meta.url,
).href;

interface SessionAttemptFailure {
  backend: SessionBackend;
  cause: unknown;
}

function configure_runtime(): void {
  if (runtime_configured) {
    return;
  }

  ort.env.wasm.simd = true;
  ort.env.wasm.proxy = false;
  // Pin the wasm asset path because the dev server rewrites module URLs in a
  // way that can make ONNX Runtime fetch HTML instead of the binary.
  ort.env.wasm.wasmPaths = {
    wasm: ORT_WASM_URL,
  };
  ort.env.wasm.numThreads =
    globalThis.crossOriginIsolated && navigator.hardwareConcurrency > 1
      ? Math.min(MAX_WASM_THREADS, navigator.hardwareConcurrency)
      : 1;

  runtime_configured = true;
}

function tensor_to_float32(data: Float32Array | ArrayLike<number>): Float32Array {
  return data instanceof Float32Array ? data : Float32Array.from(data);
}

function webgpu_available(): boolean {
  const current_navigator = globalThis.navigator as
    | (Navigator & { gpu?: unknown })
    | undefined;

  return (
    typeof current_navigator !== "undefined" &&
    typeof current_navigator.gpu !== "undefined"
  );
}

function session_backends(): readonly SessionBackend[] {
  return webgpu_available() ? ["webgpu", "wasm"] : ["wasm"];
}

function error_message(cause: unknown): string {
  if (cause instanceof AggregateError) {
    const messages = cause.errors
      .map((entry) => error_message(entry))
      .filter((message) => message.length > 0);

    if (messages.length > 0) {
      return messages.join(" | ");
    }
  }

  if (cause instanceof Error) {
    return cause.message;
  }

  if (typeof cause === "string") {
    return cause;
  }

  return "";
}

async function create_session_with_backend(
  model_bytes: Uint8Array,
  backend: SessionBackend,
): Promise<
  Result<
    {
      session: ort.InferenceSession;
      backend: SessionBackend;
    },
    SessionAttemptFailure
  >
> {
  try {
    const session = await ort.InferenceSession.create(model_bytes, {
      executionProviders: [backend],
    });

    return ok({
      session,
      backend,
    });
  } catch (cause) {
    return err({
      backend,
      cause,
    });
  }
}

function model_load_failure(
  failures: readonly SessionAttemptFailure[],
): AppError {
  const details = failures
    .map((failure) => {
      const message = error_message(failure.cause);
      return message.length > 0
        ? `${failure.backend}: ${message}`
        : failure.backend;
    })
    .join("; ");
  const suffix =
    details.length > 0 ? ` Attempted ${details}.` : "";

  return app_error(
    "model_load_failed",
    `Failed to create the ONNX Runtime Web session.${suffix}`,
    failures.map((failure) => failure.cause),
  );
}

export class OnnxRuntimeModelSession implements ModelSession {
  private constructor(
    private readonly m_session: ort.InferenceSession,
    readonly backend_label: SessionBackend,
  ) {}

  static async create(
    model_bytes: Uint8Array,
  ): Promise<Result<OnnxRuntimeModelSession, AppError>> {
    configure_runtime();

    const failures: SessionAttemptFailure[] = [];

    for (const backend of session_backends()) {
      const session_result = await create_session_with_backend(
        model_bytes,
        backend,
      );

      if (session_result.ok) {
        return ok(
          new OnnxRuntimeModelSession(
            session_result.value.session,
            session_result.value.backend,
          ),
        );
      }

      failures.push(session_result.error);
    }

    return err(model_load_failure(failures));
  }

  private async _run_single_pass(
    frame: RgbaFrame,
    alpha_hint?: Float32Array,
  ): Promise<Result<ProcessResult, AppError>> {
    const fallback_hint = create_rough_matte(frame);
    const hint = alpha_hint ?? fallback_hint;
    const input = prepare_model_input(frame, hint);
    const dims = [1, 4, frame.height, frame.width];
    const tensor = new ort.Tensor("float32", input, dims);
    const feeds: Record<string, ort.Tensor> = {
      [this.m_session.inputNames[0] ?? "input_rgb_hint"]: tensor,
    };

    try {
      const outputs = await this.m_session.run(feeds);
      tensor.dispose();

      const alpha_tensor = outputs[this.m_session.outputNames[0] ?? "alpha"];

      if (alpha_tensor === undefined) {
        return err(
          app_error("inference_failed", "Model produced no alpha output."),
        );
      }

      const fg_tensor = outputs[this.m_session.outputNames[1] ?? "fg"];
      const width = Number(alpha_tensor.dims[3] ?? frame.width);
      const height = Number(alpha_tensor.dims[2] ?? frame.height);

      const result = ok({
        alpha: tensor_to_float32(
          alpha_tensor.data as Float32Array | ArrayLike<number>,
        ),
        foreground:
          fg_tensor === undefined
            ? null
            : tensor_to_float32(
                fg_tensor.data as Float32Array | ArrayLike<number>,
              ),
        width,
        height,
        mode: "model" as const,
      });

      alpha_tensor.dispose();
      if (fg_tensor) fg_tensor.dispose();

      return result;
    } catch (cause) {
      tensor.dispose();
      return err(
        app_error(
          "inference_failed",
          "Inference failed while processing the current frame.",
          cause,
        ),
      );
    }
  }

  async run_frame(
    frame: RgbaFrame,
    alpha_hint?: Float32Array,
    strategy?: InferenceStrategy,
    on_progress?: (progress: TileProgress) => void,
  ): Promise<Result<ProcessResult, AppError>> {
    if (strategy?.type === "tiling") {
      return process_frame_tiled(
        frame,
        alpha_hint,
        strategy.tile_size,
        strategy.overlap,
        (patch_frame, patch_hint) => this._run_single_pass(patch_frame, patch_hint),
        on_progress
      );
    }

    // Default to single pass
    return this._run_single_pass(frame, alpha_hint);
  }

  dispose(): void {}
}
