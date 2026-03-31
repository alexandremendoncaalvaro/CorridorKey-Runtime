import { describe, expect, it } from "vitest";
import { app_error } from "../common/errors";
import { err, ok } from "../common/result";
import type { ModelSession, ProcessResult, RgbaFrame } from "../core/image_types";
import { FrameProcessingService } from "./frame_processing_service";

function make_frame(red: number, green: number, blue: number): RgbaFrame {
  return {
    width: 1,
    height: 1,
    data: new Uint8ClampedArray([red, green, blue, 255]),
  };
}

describe("FrameProcessingService", () => {
  it("uses the fallback path when no model is loaded", async () => {
    const service = new FrameProcessingService();
    const frame = make_frame(0, 255, 0);
    const result = await service.process_frame(frame);

    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.result.mode).toBe("rough_matte");
    expect(result.value.backend_label).toBe("fallback");
    expect(result.value.source_label).toBe("rough matte fallback");
    expect(result.value.output_frame.data[1]).toBeLessThan(255);
  });

  it("uses the model session when one is present", async () => {
    const service = new FrameProcessingService();
    const model_result: ProcessResult = {
      alpha: new Float32Array([1]),
      foreground: null,
      width: 1,
      height: 1,
      mode: "model",
    };
    const session: ModelSession = {
      backend_label: "fake",
      run_frame: async (_frame, _hint) => ok(model_result),
      dispose: () => {},
    };

    service.set_model_session(session);
    const result = await service.process_frame(make_frame(12, 34, 56));

    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.result.mode).toBe("model");
    expect(result.value.backend_label).toBe("fake");
    expect(result.value.output_frame.data[0]).toBe(12);
  });

  it("propagates model errors without silently hiding them", async () => {
    const service = new FrameProcessingService();
    const session: ModelSession = {
      backend_label: "fake",
      run_frame: async (_frame, _hint) =>
        err(app_error("inference_failed", "frame run failed")),
      dispose: () => {},
    };

    service.set_model_session(session);
    const result = await service.process_frame(make_frame(12, 34, 56));

    expect(result.ok).toBe(false);
    if (result.ok) {
      return;
    }

    expect(result.error.code).toBe("inference_failed");
  });

  it("reports inference and post-process progress stages", async () => {
    const service = new FrameProcessingService();
    const stages: string[] = [];
    const session: ModelSession = {
      backend_label: "fake",
      run_frame: async (_frame, _hint) =>
        ok({
          alpha: new Float32Array([1]),
          foreground: null,
          width: 1,
          height: 1,
          mode: "model",
        }),
      dispose: () => {},
    };

    service.set_model_session(session);
    const result = await service.process_frame(
      make_frame(12, 34, 56),
      undefined,
      (progress) => {
        stages.push(progress.stage);
      },
    );

    expect(result.ok).toBe(true);
    expect(stages).toEqual(["inference", "post_process"]);
  });
});
