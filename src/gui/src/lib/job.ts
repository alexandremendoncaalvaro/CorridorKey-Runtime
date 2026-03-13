import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

export interface JobProgress {
  type: "job_started" | "backend_selected" | "progress" | "warning" | "artifact_written" | "completed" | "failed" | "cancelled";
  message?: string;
  progress?: number;
  phase?: string;
  backend?: string;
  artifact_path?: string;
}

interface JobState {
  inputPath: string | null;
  outputPath: string | null;
  hintPath: string | null;
  isProcessing: boolean;
  currentProgress: number;
  statusMessage: string;
  activeBackend: string | null;
  error: string | null;
  logs: string[];

  // Actions
  setInput: (path: string | null) => void;
  setOutput: (path: string | null) => void;
  setHint: (path: string | null) => void;
  startJob: () => Promise<void>;
  reset: () => void;
}

export const useJobStore = create<JobState>((set, get) => ({
  inputPath: null,
  outputPath: null,
  hintPath: null,
  isProcessing: false,
  currentProgress: 0,
  statusMessage: "Ready",
  activeBackend: null,
  error: null,
  logs: [],

  setInput: (path) => set({ inputPath: path, error: null }),
  setOutput: (path) => set({ outputPath: path, error: null }),
  setHint: (path) => set({ hintPath: path }),

  reset: () => set({
    isProcessing: false,
    currentProgress: 0,
    statusMessage: "Ready",
    activeBackend: null,
    error: null,
    logs: []
  }),

  startJob: async () => {
    const { inputPath, outputPath, hintPath } = get();
    if (!inputPath || !outputPath) return;

    set({ 
      isProcessing: true, 
      currentProgress: 0, 
      statusMessage: "Starting engine...", 
      error: null,
      logs: [] 
    });

    try {
      // Listen for events from the Rust backend
      const unlisten = await listen<string>("engine-event", (event) => {
        const line = event.payload;
        try {
          const payload = JSON.parse(line) as JobProgress;
          
          set((state) => ({ 
            logs: [...state.logs, line]
          }));

          switch (payload.type) {
            case "backend_selected":
              set({ activeBackend: payload.backend || null });
              break;
            case "progress":
              set({ 
                currentProgress: (payload.progress || 0) * 100,
                statusMessage: payload.message || `Processing...`
              });
              break;
            case "completed":
              set({ isProcessing: false, currentProgress: 100, statusMessage: "Finished" });
              unlisten();
              break;
            case "failed":
              set({ isProcessing: false, error: payload.message || "Failed" });
              unlisten();
              break;
          }
        } catch (e) {
          set((state) => ({ logs: [...state.logs, line] }));
        }
      });

      await invoke("start_processing", { 
        input: inputPath, 
        output: outputPath, 
        hint: hintPath 
      });
      
    } catch (err: any) {
      set({ isProcessing: false, error: err.toString() });
    }
  }
}));
