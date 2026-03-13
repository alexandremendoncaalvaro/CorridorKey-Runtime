import { ShieldCheck, Monitor } from "lucide-react";

export function TopBar() {
  return (
    <header className="flex items-center justify-between h-14 px-8 border-b bg-background/40 backdrop-blur-md">
      <div className="flex items-center gap-2">
        <div className="flex h-2 w-2 rounded-full bg-green-500 animate-pulse" />
        <span className="text-xs font-medium text-muted-foreground uppercase tracking-widest">
          Engine Standby
        </span>
      </div>
      
      <div className="flex items-center gap-4">
        <div className="flex items-center gap-1.5 px-3 py-1 rounded-full bg-accent/50 text-[10px] font-bold border border-border/50">
          <ShieldCheck className="w-3 h-3 text-brand" />
          <span>PRODUCTION READY</span>
        </div>
        
        <div className="flex items-center gap-1.5 px-3 py-1 rounded-full bg-accent/50 text-[10px] font-bold border border-border/50">
          <Monitor className="w-3 h-3" />
          <span>RTX 3080</span>
        </div>
      </div>
    </header>
  );
}
