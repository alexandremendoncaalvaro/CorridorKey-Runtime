import { useEffect, useState } from "react";
import { Sidebar } from "./components/layout/sidebar";
import { TopBar } from "./components/layout/topbar";
import { ProcessFlow } from "./components/workflow/ProcessFlow";
import { useEngineStore } from "./lib/store";
import { useJobStore } from "./lib/job";
import { 
  Settings as SettingsIcon, 
  History as HistoryIcon, 
  Cpu, 
  ExternalLink,
  Trash2,
  Clock,
  Box,
  CheckCircle2,
  HelpCircle
} from "lucide-react";
import { Button } from "./components/ui/button";

function App() {
  const { refreshInfo, error: engineError, info } = useEngineStore();
  const { history, loadHistory, clearHistory } = useJobStore();
  const [activeTab, setActiveTab] = useState("Workflow");

  useEffect(() => {
    refreshInfo();
    loadHistory();
  }, [refreshInfo, loadHistory]);

  const renderContent = () => {
    switch (activeTab) {
      case "Workflow":
        return (
          <div className="max-w-4xl mx-auto space-y-12">
            <div className="space-y-4 text-center">
              <h1 className="text-4xl font-bold tracking-tight lg:text-5xl text-foreground">
                Neural Keying <br />
                <span className="text-muted-foreground font-medium underline decoration-brand decoration-4 underline-offset-8">Native Engine</span>
              </h1>
            </div>
            <ProcessFlow />
          </div>
        );
      case "History":
        return (
          <div className="max-w-4xl mx-auto space-y-6">
            <div className="flex items-center justify-between">
              <h2 className="text-2xl font-bold flex items-center gap-2">
                <HistoryIcon className="w-6 h-6 text-brand" /> Job History
              </h2>
              {history.length > 0 && (
                <Button variant="ghost" size="sm" onClick={clearHistory} className="text-destructive hover:text-destructive hover:bg-destructive/10">
                  <Trash2 className="w-4 h-4 mr-2" /> Clear All
                </Button>
              )}
            </div>
            
            {history.length === 0 ? (
              <div className="p-20 rounded-2xl border border-dashed border-zinc-800 text-center space-y-4">
                <div className="mx-auto w-12 h-12 rounded-full bg-zinc-900 flex items-center justify-center">
                  <HistoryIcon className="w-6 h-6 text-muted-foreground" />
                </div>
                <p className="text-muted-foreground">No recent jobs found. Process a video to see your history here.</p>
              </div>
            ) : (
              <div className="space-y-3">
                {history.map((record) => (
                  <div key={record.id} className="p-4 rounded-xl border bg-card/50 flex items-center justify-between group">
                    <div className="flex items-center gap-4">
                      <div className="p-2 rounded-lg bg-brand/10 text-brand">
                        <CheckCircle2 className="w-5 h-5" />
                      </div>
                      <div className="space-y-0.5">
                        <div className="font-medium text-sm truncate max-w-xs">{record.input.split(/[\\/]/).pop()}</div>
                        <div className="flex items-center gap-3 text-[10px] text-muted-foreground uppercase font-bold tracking-wider">
                          <span className="flex items-center gap-1"><Clock className="w-3 h-3" /> {(record.duration_ms! / 1000).toFixed(1)}s</span>
                          <span className="flex items-center gap-1"><Box className="w-3 h-3" /> {record.backend}</span>
                          <span>{new Date(record.timestamp).toLocaleDateString()}</span>
                        </div>
                      </div>
                    </div>
                    <Button variant="ghost" size="icon" className="opacity-0 group-hover:opacity-100 transition-opacity">
                      <ExternalLink className="w-4 h-4" />
                    </Button>
                  </div>
                ))}
              </div>
            )}
          </div>
        );
      case "Hardware":
        return (
          <div className="max-w-4xl mx-auto space-y-6">
            <h2 className="text-2xl font-bold flex items-center gap-2">
              <Cpu className="w-6 h-6 text-brand" /> Hardware Diagnostics
            </h2>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              {info?.devices.map((d, i) => (
                <div key={i} className="p-6 rounded-2xl border bg-gradient-to-br from-card to-zinc-900/50 space-y-4 shadow-apple">
                  <div className="flex items-center justify-between">
                    <div className="px-2 py-1 rounded bg-brand/20 text-brand text-[10px] font-bold uppercase tracking-widest">
                      {d.backend}
                    </div>
                    {i === 0 && <span className="text-[10px] font-bold text-green-500 uppercase">Primary</span>}
                  </div>
                  <div className="space-y-1">
                    <div className="text-xl font-bold tracking-tight">{d.name}</div>
                    <div className="text-sm text-muted-foreground">
                      {d.memory_mb > 0 ? `${d.memory_mb} MB Dedicated VRAM` : "Integrated Graphics / System RAM"}
                    </div>
                  </div>
                  <div className="pt-4 flex items-center gap-4 text-[10px] font-bold text-muted-foreground uppercase tracking-wider">
                    <span>Precision: FP16</span>
                    <span>Tiling: Supported</span>
                  </div>
                </div>
              ))}
            </div>
          </div>
        );
      case "Settings":
        return (
          <div className="max-w-4xl mx-auto space-y-6">
            <h2 className="text-2xl font-bold flex items-center gap-2">
              <SettingsIcon className="w-6 h-6 text-brand" /> Engine Preferences
            </h2>
            <div className="space-y-4">
              <div className="p-6 rounded-2xl border bg-card/50 flex items-center justify-between">
                <div className="space-y-1">
                  <div className="font-bold text-lg">Output Encoding</div>
                  <div className="text-sm text-muted-foreground text-balance max-w-md">
                    Choose between standard compression and professional VFX-grade lossless output.
                  </div>
                </div>
                <div className="flex bg-zinc-900 p-1 rounded-lg border border-zinc-800">
                  <button className="px-4 py-1.5 rounded-md text-xs font-bold bg-brand text-white shadow-lg">LOSSLESS</button>
                  <button className="px-4 py-1.5 rounded-md text-xs font-bold text-muted-foreground hover:text-zinc-100">BALANCED</button>
                </div>
              </div>

              <div className="p-6 rounded-2xl border bg-card/50 flex items-center justify-between">
                <div className="space-y-1">
                  <div className="font-bold text-lg">Tiling Strategy</div>
                  <div className="text-sm text-muted-foreground text-balance max-w-md">
                    Automatically split high-resolution inputs (4K+) to fit available VRAM.
                  </div>
                </div>
                <div className="px-3 py-1 rounded-full bg-green-500/10 text-green-500 text-[10px] font-bold border border-green-500/20 uppercase tracking-widest">
                  Auto-Enabled
                </div>
              </div>
            </div>
          </div>
        );
      case "Support":
        return (
          <div className="max-w-4xl mx-auto space-y-6 text-center py-20">
            <div className="mx-auto w-16 h-16 rounded-full bg-zinc-900 flex items-center justify-center mb-6">
              <HelpCircle className="w-8 h-8 text-brand" />
            </div>
            <h2 className="text-3xl font-bold">Need assistance?</h2>
            <p className="text-muted-foreground max-w-md mx-auto text-lg">
              CorridorKey Runtime is a production engine. For bug reports or feature requests, visit the official repository.
            </p>
            <Button variant="secondary" className="mt-8">
              Open Documentation
            </Button>
          </div>
        );
      default:
        return null;
    }
  };

  return (
    <div className="flex h-screen w-screen overflow-hidden bg-transparent text-foreground selection:bg-brand/20">
      <Sidebar activeTab={activeTab} onTabChange={setActiveTab} />
      
      <main className="flex flex-col flex-1 min-w-0 bg-background/30 backdrop-blur-sm">
        <TopBar />
        
        <div className="flex-1 overflow-y-auto p-8 lg:p-12">
          {engineError && (
            <div className="mb-8 p-4 rounded-lg bg-destructive/10 border border-destructive/20 text-destructive text-sm animate-in shake-1 duration-500">
              <strong>Engine Error:</strong> {engineError}
            </div>
          )}
          
          {renderContent()}
        </div>
      </main>
    </div>
  );
}

export default App;
