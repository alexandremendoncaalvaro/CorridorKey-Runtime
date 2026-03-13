import { open, save } from "@tauri-apps/plugin-dialog";
import { useJobStore } from "@/lib/job";
import { Button } from "@/components/ui/button";
import { 
  FileVideo, 
  FolderDown, 
  Zap, 
  AlertCircle, 
  CheckCircle2,
  ChevronRight,
  Terminal,
  Layers
} from "lucide-react";
import { useState } from "react";
import { cn } from "@/lib/utils";

interface JobMetrics {
  ram_usage_mb?: number;
  cpu_usage_percent?: number;
}

export function ProcessFlow() {
  const { 
    inputPath, setInput, 
    outputPath, setOutput,
    hintPath, setHint,
    startJob, isProcessing, 
    currentProgress, statusMessage, 
    error, activeBackend,
    logs
  } = useJobStore();

  const [showLogs, setShowLogs] = useState(false);
  
  // Extract metrics from the last log entry if possible
  const lastLog = logs[logs.length - 1];
  let metrics: JobMetrics = {};
  if (lastLog) {
    try {
      const parsed = JSON.parse(lastLog);
      if (parsed.metrics) metrics = parsed.metrics;
    } catch (e) {}
  }

  const handleSelectInput = async () => {
    const selected = await open({
      multiple: false,
      filters: [{
        name: 'Video/Images',
        extensions: ['mp4', 'mov', 'exr', 'png', 'jpg']
      }]
    });
    if (selected && !Array.isArray(selected)) {
      setInput(selected);
    }
  };

  const handleSelectHint = async () => {
    const selected = await open({
      multiple: false,
      filters: [{
        name: 'Video/Images',
        extensions: ['mp4', 'mov', 'exr', 'png', 'jpg']
      }]
    });
    if (selected && !Array.isArray(selected)) {
      setHint(selected);
    }
  };

  const handleSelectOutput = async () => {
    const selected = await save({
      defaultPath: outputPath || undefined,
      filters: [{
        name: 'Video',
        extensions: ['mov', 'mkv', 'avi', 'mp4']
      }]
    });
    if (selected) {
      setOutput(selected);
    }
  };

  return (
    <div className="space-y-8 animate-in fade-in slide-in-from-bottom-4 duration-700">
      
      {/* Steps Container */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        
        {/* Step 1: Input */}
        <div 
          onClick={!isProcessing ? handleSelectInput : undefined}
          className={cn(
            "p-6 rounded-2xl border-2 border-dashed transition-all cursor-pointer flex flex-col items-center text-center space-y-4",
            inputPath 
              ? "border-brand/40 bg-brand/5" 
              : "border-muted-foreground/20 bg-accent/5 hover:border-brand/50 hover:bg-brand/5",
            isProcessing && "opacity-50 cursor-not-allowed"
          )}
        >
          <div className="p-3 rounded-full bg-background shadow-sm">
            <FileVideo className={cn("w-6 h-6", inputPath ? "text-brand" : "text-muted-foreground")} />
          </div>
          <div className="space-y-1">
            <h4 className="font-semibold text-sm text-foreground">1. Source</h4>
            <p className="text-[10px] text-muted-foreground truncate max-w-[150px]">
              {inputPath ? (inputPath.split(/[\\/]/).pop()) : "Select footage"}
            </p>
          </div>
        </div>

        {/* Step 2: Alpha Hint (Optional) */}
        <div 
          onClick={!isProcessing ? handleSelectHint : undefined}
          className={cn(
            "p-6 rounded-2xl border-2 border-dashed transition-all cursor-pointer flex flex-col items-center text-center space-y-4 relative",
            hintPath 
              ? "border-brand/40 bg-brand/5" 
              : "border-muted-foreground/20 bg-accent/5 hover:border-brand/50 hover:bg-brand/5",
            isProcessing && "opacity-50 cursor-not-allowed"
          )}
        >
          <div className="absolute top-2 right-3 text-[8px] font-bold text-muted-foreground/50 uppercase tracking-tighter">Optional</div>
          <div className="p-3 rounded-full bg-background shadow-sm">
            <Layers className={cn("w-6 h-6", hintPath ? "text-brand" : "text-muted-foreground")} />
          </div>
          <div className="space-y-1">
            <h4 className="font-semibold text-sm text-foreground">2. Alpha Hint</h4>
            <p className="text-[10px] text-muted-foreground truncate max-w-[150px]">
              {hintPath ? (hintPath.split(/[\\/]/).pop()) : "Select rough mask"}
            </p>
          </div>
        </div>

        {/* Step 3: Output */}
        <div 
          onClick={!isProcessing ? handleSelectOutput : undefined}
          className={cn(
            "p-6 rounded-2xl border-2 border-dashed transition-all cursor-pointer flex flex-col items-center text-center space-y-4",
            outputPath 
              ? "border-brand/40 bg-brand/5" 
              : "border-muted-foreground/20 bg-accent/5 hover:border-brand/50 hover:bg-brand/5",
            isProcessing && "opacity-50 cursor-not-allowed"
          )}
        >
          <div className="p-3 rounded-full bg-background shadow-sm">
            <FolderDown className={cn("w-6 h-6", outputPath ? "text-brand" : "text-muted-foreground")} />
          </div>
          <div className="space-y-1">
            <h4 className="font-semibold text-sm text-foreground">3. Destination</h4>
            <p className="text-[10px] text-muted-foreground truncate max-w-[150px]">
              {outputPath ? (outputPath.split(/[\\/]/).pop()) : "Choose where to save"}
            </p>
          </div>
        </div>

      </div>

      {/* Progress Section */}
      {(isProcessing || currentProgress > 0 || error) && (
        <div className="p-8 rounded-2xl bg-card border border-zinc-800 shadow-apple space-y-6">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              {error ? (
                <AlertCircle className="w-5 h-5 text-destructive" />
              ) : currentProgress === 100 ? (
                <CheckCircle2 className="w-5 h-5 text-green-500" />
              ) : (
                <Zap className="w-5 h-5 text-brand animate-pulse" />
              )}
              <div>
                <h4 className="font-bold text-sm leading-none text-foreground">
                  {error ? "Processing Failed" : currentProgress === 100 ? "Complete" : "In Progress"}
                </h4>
                <p className={cn(
                  "text-xs mt-1 font-medium",
                  error ? "text-destructive" : "text-muted-foreground"
                )}>
                  {statusMessage}
                </p>
              </div>
            </div>
            
            <div className="flex gap-2">
              {activeBackend && (
                <div className="px-2 py-1 rounded bg-zinc-800 text-zinc-400 text-[10px] font-mono font-bold uppercase tracking-wider">
                  {activeBackend}
                </div>
              )}
              {metrics.ram_usage_mb && (
                <div className="px-2 py-1 rounded bg-brand/10 text-brand text-[10px] font-mono font-bold">
                  {metrics.ram_usage_mb}MB RAM
                </div>
              )}
            </div>
          </div>

          {!error && (
            <div className="space-y-2">
              <div className="h-2 w-full bg-zinc-900 rounded-full overflow-hidden">
                <div 
                  className="h-full bg-brand transition-all duration-500 ease-out shadow-[0_0_10px_rgba(14,165,233,0.5)]" 
                  style={{ width: `${currentProgress}%` }}
                />
              </div>
              <div className="flex justify-end">
                <span className="text-[10px] font-mono text-muted-foreground">{Math.round(currentProgress)}%</span>
              </div>
            </div>
          )}

          {error && (
            <div className="p-4 rounded-lg bg-destructive/5 border border-destructive/10 text-xs text-destructive font-mono overflow-auto max-h-32">
              {error}
            </div>
          )}

          {/* Log Toggle */}
          <button 
            onClick={() => setShowLogs(!showLogs)}
            className="flex items-center gap-2 text-[10px] text-muted-foreground hover:text-foreground transition-colors"
          >
            <Terminal className="w-3 h-3" />
            {showLogs ? "Hide Console Output" : "View Technical Logs"}
          </button>

          {showLogs && (
            <div className="mt-4 p-4 rounded-lg bg-black/90 text-[10px] font-mono text-green-400/80 overflow-auto max-h-48 whitespace-pre-wrap">
              {logs.length > 0 ? logs.join('\n') : "Waiting for engine output..."}
            </div>
          )}
        </div>
      )}

      {/* Action Area */}
      <div className="flex items-center justify-center pt-4">
        <Button 
          size="lg" 
          disabled={!inputPath || !outputPath || isProcessing} 
          onClick={startJob}
          className="group px-12 relative overflow-hidden"
        >
          {isProcessing ? (
            <div className="flex items-center gap-2">
              <div className="w-4 h-4 border-2 border-t-transparent border-white rounded-full animate-spin" />
              Processing...
            </div>
          ) : (
            <div className="flex items-center gap-2">
              Run Neural Keyer
              <ChevronRight className="w-4 h-4 group-hover:translate-x-1 transition-transform" />
            </div>
          )}
        </Button>
      </div>

    </div>
  );
}
