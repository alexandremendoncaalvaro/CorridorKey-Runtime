import { invoke } from "@tauri-apps/api/core";

export interface DeviceInfo {
  name: string;
  memory_mb: number;
  backend: string;
}

export interface SystemInfo {
  version: string;
  devices: DeviceInfo[];
  capabilities: {
    tensorrt_rtx_available: boolean;
    multi_gpu_available: boolean;
    cpu_fallback_available: boolean;
  };
}

/**
 * Fetches system information from the native engine via Rust bridge.
 */
export async function getEngineInfo(): Promise<SystemInfo> {
  try {
    const jsonStr = await invoke<string>("get_engine_status");
    return JSON.parse(jsonStr) as SystemInfo;
  } catch (error) {
    console.error("Failed to fetch engine info:", error);
    throw error;
  }
}
