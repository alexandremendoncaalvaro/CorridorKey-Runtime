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
    return info.devices.find(d => d.backend === "tensorrt") || 
           info.devices.find(d => d.backend === "cuda") ||
           info.devices.find(d => d.backend === "dml") ||
           info.devices[0];
  },

  refreshInfo: async () => {
    set({ isLoading: true, error: null });
    try {
      const info = await getEngineInfo();
      set({ info, isLoading: false });
    } catch (err: any) {
      set({ error: err.message, isLoading: false });
    }
  }
}));
