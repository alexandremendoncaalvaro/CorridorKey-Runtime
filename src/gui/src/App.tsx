import { useEffect, useState } from "react";
import { Sidebar } from "./components/layout/sidebar";
import { TopBar } from "./components/layout/topbar";
import { ProcessFlow } from "./components/workflow/ProcessFlow";
import { useEngineStore } from "./lib/store";
import { Activity, Settings as SettingsIcon, History as HistoryIcon, Cpu } from "lucide-react";

function App() {
  const { refreshInfo, error: engineError, info } = useEngineStore();
  const [activeTab, setActiveTab] = useState("Import");

  useEffect(() => {
    refreshInfo();
  }, [refreshInfo]);

  const renderContent = () => {
    switch (activeTab) {
      case "Import":
      case "Process":
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
            <h2 className="text-2xl font-bold flex items-center gap-2">
              <HistoryIcon className="w-6 h-6 text-brand" /> Job History
            </h2>
            <div className="p-12 rounded-2xl border border-dashed border-zinc-800 text-center text-muted-foreground">
              No recent jobs found. Process a video to see your history here.
            </div>
          </div>
        );
      case "GPU Status":
        return (
          <div className="max-w-4xl mx-auto space-y-6">
            <h2 className="text-2xl font-bold flex items-center gap-2">
              <Cpu className="w-6 h-6 text-brand" /> Hardware Diagnostics
            </h2>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              {info?.devices.map((d, i) => (
                <div key={i} className="p-6 rounded-xl border bg-card/50 space-y-2">
                  <div className="text-xs font-bold text-brand uppercase tracking-widest">{d.backend}</div>
                  <div className="text-lg font-semibold">{d.name}</div>
                  <div className="text-sm text-muted-foreground">{d.memory_mb > 0 ? `${d.memory_mb} MB VRAM` : "System RAM"}</div>
                </div>
              ))}
            </div>
          </div>
        );
      case "Settings":
        return (
          <div className="max-w-4xl mx-auto space-y-6">
            <h2 className="text-2xl font-bold flex items-center gap-2">
              <SettingsIcon className="w-6 h-6 text-brand" /> Preferences
            </h2>
            <div className="p-6 rounded-xl border bg-card/50 space-y-4">
              <div className="flex items-center justify-between">
                <div>
                  <div className="font-medium">Output Quality</div>
                  <div className="text-sm text-muted-foreground">Force visual lossless VFX output (CRF 10)</div>
                </div>
                <div className="px-3 py-1 rounded bg-brand/20 text-brand text-xs font-bold">ALWAYS ON</div>
              </div>
            </div>
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
