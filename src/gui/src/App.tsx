import { useEffect } from "react";
import { Sidebar } from "./components/layout/sidebar";
import { TopBar } from "./components/layout/topbar";
import { ProcessFlow } from "./components/workflow/ProcessFlow";
import { useEngineStore } from "./lib/store";

function App() {
  const { refreshInfo, error: engineError } = useEngineStore();

  useEffect(() => {
    refreshInfo();
  }, [refreshInfo]);

  return (
    <div className="flex h-screen w-screen overflow-hidden bg-transparent text-foreground selection:bg-brand/20">
      <Sidebar />
      
      <main className="flex flex-col flex-1 min-w-0 bg-background/30 backdrop-blur-sm">
        <TopBar />
        
        <div className="flex-1 overflow-y-auto p-8 lg:p-12">
          {engineError && (
            <div className="mb-8 p-4 rounded-lg bg-destructive/10 border border-destructive/20 text-destructive text-sm animate-in shake-1 duration-500">
              <strong>Engine Error:</strong> {engineError}
            </div>
          )}
          
          <div className="max-w-4xl mx-auto space-y-12">
            
            {/* Hero Section */}
            <div className="space-y-4">
              <h1 className="text-4xl font-bold tracking-tight lg:text-5xl">
                Neural Keying <br />
                <span className="text-muted-foreground font-medium">Native Runtime.</span>
              </h1>
              <p className="text-lg text-muted-foreground max-w-xl">
                The fastest way to run CorridorKey locally on Windows and Mac.
              </p>
            </div>

            {/* Main Workflow */}
            <ProcessFlow />

          </div>
        </div>
      </main>
    </div>
  );
}

export default App;
