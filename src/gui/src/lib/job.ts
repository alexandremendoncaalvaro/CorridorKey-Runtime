import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { downloadDir } from "@tauri-apps/api/path";

export interface JobProgress {
  type: "job_started" | "backend_selected" | "progress" | "warning" | "artifact_written" | "completed" | "failed" | "cancelled";
  message?: string;
  progress?: number;
  phase?: string;
  backend?: string;
  artifact_path?: string;
}

export interface JobRecord {
  id: string;
  timestamp: string;
  input: string;
  output: string;
  status: "success" | "failed";
  backend: string | null;
  duration_ms?: number;
}

interface JobState {
  inputPath: string | null;
  outputPath: string | null;
  hintPath: string | null;
  videoEncodeMode: "lossless" | "balanced";
  defaultOutputDir: string | null;
  isProcessing: boolean;
  currentProgress: number;
  statusMessage: string;
  activeBackend: string | null;
  error: string | null;
  logs: string[];
  history: JobRecord[];

  // Actions
  setInput: (path: string | null) => void;
  setOutput: (path: string | null) => void;
  setHint: (path: string | null) => void;
  setVideoEncodeMode: (mode: "lossless" | "balanced") => void;
  initDefaults: () => Promise<void>;
  startJob: () => Promise<void>;
  reset: () => void;
  loadHistory: () => void;
  clearHistory: () => void;
}

export const useJobStore = create<JobState>((set, get) => ({
  inputPath: null,
  outputPath: null,
  hintPath: null,
  videoEncodeMode: "lossless",
  defaultOutputDir: null,
  isProcessing: false,
  currentProgress: 0,
  statusMessage: "Ready",
  activeBackend: null,
  error: null,
  logs: [],
  history: [],

  setInput: (path) => set({ inputPath: path, error: null }),
  setOutput: (path) => set({ outputPath: path, error: null }),
  setHint: (path) => set({ hintPath: path }),
  setVideoEncodeMode: (mode) => set({ videoEncodeMode: mode }),
  initDefaults: async () => {
    try {
      const dir = await downloadDir();
      if (!dir) return;
      set((state) =>
        state.outputPath
          ? { defaultOutputDir: dir }
          : { outputPath: dir, defaultOutputDir: dir }
      );
    } catch (e) {}
  },

  loadHistory: () => {
    const saved = localStorage.getItem("corridorkey_history");
    if (saved) set({ history: JSON.parse(saved) });
  },

  clearHistory: () => {
    localStorage.removeItem("corridorkey_history");
    set({ history: [] });
  },

  reset: () => set({
    isProcessing: false,
    currentProgress: 0,
    statusMessage: "Ready",
    activeBackend: null,
    error: null,
    logs: []
  }),

  startJob: async () => {
    const { inputPath, outputPath, hintPath, history, videoEncodeMode } = get();
    if (!inputPath || !outputPath) return;

    const startTime = Date.now();
    set({
      isProcessing: true,
      currentProgress: 0,
      statusMessage: "Starting engine...",
      error: null,
      logs: []
    });

    try {
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
            case "warning":
              if (payload.message) {
                set({ statusMessage: payload.message });
              }
              break;
            case "artifact_written":
              if (payload.artifact_path) {
                set({ outputPath: payload.artifact_path });
              }
              break;
            case "completed":
              const duration = Date.now() - startTime;
              const finalOutputPath = get().outputPath || outputPath;
              if (finalOutputPath) {
                try {
                  void invoke("reveal_in_folder", { path: finalOutputPath });
                } catch (e) {}
              }
              const newRecord: JobRecord = {
                id: Math.random().toString(36).substr(2, 9),
                timestamp: new Date().toISOString(),
                input: inputPath,
                output: finalOutputPath || "",
                status: "success",
                backend: get().activeBackend,
                duration_ms: duration
              };
              const updatedHistory = [newRecord, ...history].slice(0, 50);
              localStorage.setItem("corridorkey_history", JSON.stringify(updatedHistory));
              set({
                isProcessing: false,
                currentProgress: 100,
                statusMessage: "Finished",
                history: updatedHistory
              });
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
        hint: hintPath,
        video_encode: videoEncodeMode
      });

    } catch (err: any) {
      set({ isProcessing: false, error: err.toString() });
    }
  }
}));
