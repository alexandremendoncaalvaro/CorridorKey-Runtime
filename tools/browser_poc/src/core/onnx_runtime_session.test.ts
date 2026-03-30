import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("onnxruntime-web", () => {
  return {
    env: {
      wasm: {
        simd: false,
        proxy: true,
        numThreads: 1,
      },
    },
    InferenceSession: {
      create: vi.fn(),
    },
    Tensor: class {},
  };
});

import * as ort from "onnxruntime-web";
import { OnnxRuntimeModelSession } from "./onnx_runtime_session";

function create_mock() {
  return vi.mocked(ort.InferenceSession.create);
}

function fake_session() {
  return {
    inputNames: ["input_rgb_hint"],
    outputNames: ["alpha", "fg"],
    run: vi.fn(),
  } as unknown as ort.InferenceSession;
}

describe("OnnxRuntimeModelSession.create", () => {
  beforeEach(() => {
    create_mock().mockReset();
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("falls back to wasm when webgpu initialization fails", async () => {
    vi.stubGlobal("navigator", {
      hardwareConcurrency: 8,
      gpu: {},
    });
    create_mock()
      .mockRejectedValueOnce(new Error("WebGPU unavailable"))
      .mockResolvedValueOnce(fake_session());

    const result = await OnnxRuntimeModelSession.create(new Uint8Array([1]));

    expect(create_mock()).toHaveBeenCalledTimes(2);
    expect(create_mock()).toHaveBeenNthCalledWith(
      1,
      expect.any(Uint8Array),
      {
        executionProviders: ["webgpu"],
      },
    );
    expect(create_mock()).toHaveBeenNthCalledWith(
      2,
      expect.any(Uint8Array),
      {
        executionProviders: ["wasm"],
      },
    );
    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.backend_label).toBe("wasm");
  });

  it("skips webgpu when the browser does not expose navigator.gpu", async () => {
    vi.stubGlobal("navigator", {
      hardwareConcurrency: 8,
    });
    create_mock().mockResolvedValueOnce(fake_session());

    const result = await OnnxRuntimeModelSession.create(new Uint8Array([1]));

    expect(create_mock()).toHaveBeenCalledTimes(1);
    expect(create_mock()).toHaveBeenCalledWith(expect.any(Uint8Array), {
      executionProviders: ["wasm"],
    });
    expect(result.ok).toBe(true);
    if (!result.ok) {
      return;
    }

    expect(result.value.backend_label).toBe("wasm");
  });

  it("surfaces the backend-specific failure chain when all providers fail", async () => {
    vi.stubGlobal("navigator", {
      hardwareConcurrency: 8,
      gpu: {},
    });
    create_mock()
      .mockRejectedValueOnce(new Error("WebGPU unavailable"))
      .mockRejectedValueOnce(new Error("WASM bootstrap failed"));

    const result = await OnnxRuntimeModelSession.create(new Uint8Array([1]));

    expect(result.ok).toBe(false);
    if (result.ok) {
      return;
    }

    expect(result.error.code).toBe("model_load_failed");
    expect(result.error.message).toContain("webgpu: WebGPU unavailable");
    expect(result.error.message).toContain("wasm: WASM bootstrap failed");
  });
});
