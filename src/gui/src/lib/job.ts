import { create } from "zustand";
import { Command } from "@tauri-apps/plugin-shell";

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
  isProcessing: boolean;
  currentProgress: number;
  statusMessage: string;
  activeBackend: string | null;
  error: string | null;
  logs: string[];

  // Actions
  setInput: (path: string | null) => void;
  setOutput: (path: string | null) => void;
  startJob: () => Promise<void>;
  reset: () => void;
}

export const useJobStore = create<JobState>((set, get) => ({
  inputPath: null,
  outputPath: null,
  isProcessing: false,
  currentProgress: 0,
  statusMessage: "Ready",
  activeBackend: null,
  error: null,
  logs: [],

  setInput: (path) => set({ inputPath: path, error: null }),
  setOutput: (path) => set({ outputPath: path, error: null }),

  reset: () => set({
    isProcessing: false,
    currentProgress: 0,
    statusMessage: "Ready",
    activeBackend: null,
    error: null,
    logs: []
  }),

  startJob: async () => {
    const { inputPath, outputPath } = get();
    if (!inputPath || !outputPath) return;

    set({ 
      isProcessing: true, 
      currentProgress: 0, 
      statusMessage: "Initializing engine...", 
      error: null,
      logs: [] 
    });

    try {
      // Build sidecar command
      const args = [
        "process",
        "--input", inputPath,
        "--output", outputPath,
        "--json" // Crucial for NDJSON parsing
      ];

      const command = Command.sidecar("bin/corridorkey", args);

      // Listen to stdout for NDJSON events
      command.stdout.on("data", (line) => {
        try {
          const event = JSON.parse(line) as JobProgress;
          
          set((state) => ({ 
            logs: [...state.logs, line]
          }));

          switch (event.type) {
            case "backend_selected":
              set({ activeBackend: event.backend || null });
              break;
            case "progress":
              set({ 
                currentProgress: (event.progress || 0) * 100,
                statusMessage: event.message || `Processing ${event.phase || ""}...`
              });
              break;
            case "completed":
              set({ isProcessing: false, currentProgress: 100, statusMessage: "Finished successfully" });
              break;
            case "failed":
              set({ isProcessing: false, error: event.message || "Unknown engine failure" });
              break;
          }
        } catch (e) {
          // If it's not valid JSON, treat as raw log
          set((state) => ({ logs: [...state.logs, line] }));
        }
      });

      command.stderr.on("data", (line) => {
        set((state) => ({ logs: [...state.logs, `[ERROR] ${line}`] }));
      });

      const child = await command.spawn();
      
      // We don't await the full execution here so the UI stays responsive
      // The events will update the store.
    } catch (err: any) {
      set({ isProcessing: false, error: err.toString() });
    }
  }
}));
