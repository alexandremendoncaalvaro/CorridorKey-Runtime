import { create } from "zustand";
import { SystemInfo, DeviceInfo, getEngineInfo } from "@/lib/engine";

interface EngineState {
  info: SystemInfo | null;
  isLoading: boolean;
  error: string | null;
  
  // Computed helpers
  getPrimaryGpu: () => DeviceInfo | null;
  
  // Actions
  refreshInfo: () => Promise<void>;
}

export const useEngineStore = create<EngineState>((set, get) => ({
  info: null,
  isLoading: false,
  error: null,

  getPrimaryGpu: () => {
    const { info } = get();
    if (!info || info.devices.length === 0) return null;
    // Prefer TensorRT, then CUDA, then DirectML
    return info.devices.find((d: DeviceInfo) => d.backend === "tensorrt") || 
           info.devices.find((d: DeviceInfo) => d.backend === "cuda") ||
           info.devices.find((d: DeviceInfo) => d.backend === "dml") ||
           info.devices[0];
  },

  refreshInfo: async () => {
    set({ isLoading: true, error: null });
    
    // Add a safety timeout for the sidecar call
    const timeoutPromise = new Promise((_, reject) => 
      setTimeout(() => reject(new Error("Engine Probe Timeout")), 5000)
    );

    try {
      const info = await Promise.race([getEngineInfo(), timeoutPromise]) as SystemInfo;
      set({ info, isLoading: false });
    } catch (err: any) {
      console.error("Hardware probe failed, using fallback:", err);
      const msg = err && err.message ? err.message : String(err);
      set({ 
        error: `Hardware Probe Failed: ${msg}`, 
        isLoading: false,
        info: {
          version: "0.4.0",
          devices: [{ name: "CPU Baseline (Fallback)", memory_mb: 0, backend: "cpu" }],
          capabilities: { tensorrt_rtx_available: false, multi_gpu_available: false, cpu_fallback_available: true }
        }
      });
    }
  }
}));
