import { Sidebar } from "./components/layout/sidebar";
import { TopBar } from "./components/layout/topbar";
import { Button } from "./components/ui/button";
import { UploadCloud, Zap } from "lucide-react";

function App() {
  return (
    <div className="flex h-screen w-screen overflow-hidden bg-transparent text-foreground selection:bg-brand/20">
      <Sidebar />
      
      <main className="flex flex-col flex-1 min-w-0 bg-background/30 backdrop-blur-sm">
        <TopBar />
        
        <div className="flex-1 overflow-y-auto p-8 lg:p-12">
          <div className="max-w-4xl mx-auto space-y-12">
            
            {/* Hero Section */}
            <div className="space-y-4">
              <h1 className="text-4xl font-bold tracking-tight lg:text-5xl">
                Neural Keying <br />
                <span className="text-muted-foreground">Redefined.</span>
              </h1>
              <p className="text-lg text-muted-foreground max-w-xl">
                Studio-grade green screen removal powered by high-performance native inference.
              </p>
            </div>

            {/* Drop Zone Card */}
            <div className="group relative rounded-2xl border-2 border-dashed border-muted-foreground/20 bg-accent/5 px-12 py-20 transition-all hover:border-brand/50 hover:bg-brand/5 flex flex-col items-center justify-center text-center space-y-4">
              <div className="p-4 rounded-full bg-background shadow-apple transition-transform group-hover:scale-110">
                <UploadCloud className="w-8 h-8 text-brand" />
              </div>
              <div className="space-y-1">
                <h3 className="text-xl font-semibold">Drop your footage</h3>
                <p className="text-sm text-muted-foreground">
                  Support for MP4, MOV, and high-bitrate EXR sequences.
                </p>
              </div>
              <Button variant="secondary" className="mt-4">
                Select Files
              </Button>
            </div>

            {/* Quick Actions / Presets */}
            <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
              {[
                { title: "Balanced", desc: "Optimal speed and quality for 1080p", icon: Zap },
                { title: "High Quality", desc: "Maximum precision at 4K resolution", icon: ShieldCheck },
                { title: "Preview", desc: "Fast draft for timing and layout", icon: Play }
              ].map((item, i) => (
                <div key={i} className="p-6 rounded-xl border bg-card/50 hover:bg-card transition-colors cursor-pointer space-y-3">
                  <item.icon className="w-5 h-5 text-brand" />
                  <div className="space-y-1">
                    <h4 className="font-semibold">{item.title}</h4>
                    <p className="text-xs text-muted-foreground">{item.desc}</p>
                  </div>
                </div>
              ))}
            </div>

          </div>
        </div>
      </main>
    </div>
  );
}

// Re-using icon from topbar for consistency
import { ShieldCheck, Play } from "lucide-react";

export default App;
