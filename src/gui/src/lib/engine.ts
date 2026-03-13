import { Command } from "@tauri-apps/plugin-shell";

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
 * Invokes the CorridorKey native engine sidecar.
 * @param args Arguments to pass to the CLI
 * @returns Parsed JSON output or raw string
 */
export async function invokeEngine<T = any>(args: string[]): Promise<T> {
  try {
    const command = Command.sidecar("bin/corridorkey", args);
    const output = await command.execute();
    
    if (output.code !== 0) {
      throw new Error(`Engine exited with code ${output.code}: ${output.stderr}`);
    }

    try {
      return JSON.parse(output.stdout) as T;
    } catch {
      return output.stdout as unknown as T;
    }
  } catch (error) {
    console.error("Failed to invoke engine:", error);
    throw error;
  }
}

/**
 * Fetches system information from the native engine.
 */
export async function getEngineInfo(): Promise<SystemInfo> {
  // Using 'info --json' command we implemented earlier
  return await invokeEngine<SystemInfo>(["info", "--json"]);
}
