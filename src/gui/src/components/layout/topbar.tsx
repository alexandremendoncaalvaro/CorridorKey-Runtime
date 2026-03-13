import { ShieldCheck, Monitor, Loader2 } from "lucide-react";
import { useEngineStore } from "@/lib/store";

export function TopBar() {
  const { info, isLoading, getPrimaryGpu } = useEngineStore();
  const gpu = getPrimaryGpu();

  return (
    <header className="flex items-center justify-between h-14 px-8 border-b bg-background/40 backdrop-blur-md">
      <div className="flex items-center gap-2">
        <div className={cn(
          "flex h-2 w-2 rounded-full",
          isLoading ? "bg-amber-500 animate-spin" : "bg-green-500 animate-pulse"
        )} />
        <span className="text-xs font-medium text-muted-foreground uppercase tracking-widest">
          {isLoading ? "Probing Hardware..." : "Engine Standby"}
        </span>
      </div>
      
      <div className="flex items-center gap-4">
        <div className="flex items-center gap-1.5 px-3 py-1 rounded-full bg-accent/50 text-[10px] font-bold border border-border/50">
          <ShieldCheck className="w-3 h-3 text-brand" />
          <span>PRODUCTION READY {info?.version ? `v${info.version}` : ""}</span>
        </div>
        
        <div className="flex items-center gap-1.5 px-3 py-1 rounded-full bg-accent/50 text-[10px] font-bold border border-border/50">
          {isLoading ? (
            <Loader2 className="w-3 h-3 animate-spin" />
          ) : (
            <Monitor className="w-3 h-3" />
          )}
          <span>{gpu?.name || "CPU Baseline"}</span>
        </div>
      </div>
    </header>
  );
}

import { cn } from "@/lib/utils";
