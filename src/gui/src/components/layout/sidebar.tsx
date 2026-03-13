import { cn } from "@/lib/utils";
import { 
  Zap, 
  History, 
  Cpu, 
  Settings, 
  HelpCircle
} from "lucide-react";

interface SidebarItemProps {
  icon: React.ElementType;
  label: string;
  active?: boolean;
  onClick?: () => void;
}

function SidebarItem({ icon: Icon, label, active, onClick }: SidebarItemProps) {
  return (
    <button
      onClick={onClick}
      className={cn(
        "flex items-center w-full gap-3 px-3 py-2 transition-all rounded-lg text-sm font-medium",
        active 
          ? "bg-brand/10 text-brand" 
          : "text-muted-foreground hover:bg-zinc-900 hover:text-zinc-100"
      )}
    >
      <Icon className="w-4 h-4" />
      {label}
    </button>
  );
}

interface SidebarProps {
  activeTab: string;
  onTabChange: (tab: string) => void;
}

export function Sidebar({ activeTab, onTabChange }: SidebarProps) {
  return (
    <aside className="flex flex-col w-64 h-screen border-r border-zinc-800 bg-background/60 backdrop-blur-xl">
      <div className="flex items-center gap-3 h-14 px-6 border-b border-zinc-800">
        <img src="/logo.png" className="w-6 h-6 object-contain shrink-0" alt="Logo" />
        <span className="font-bold tracking-tight text-lg">CorridorKey</span>
      </div>
      
      <div className="flex-1 px-4 py-6 space-y-1">
        <SidebarItem icon={Zap} label="Workflow" active={activeTab === "Workflow"} onClick={() => onTabChange("Workflow")} />
        <SidebarItem icon={History} label="History" active={activeTab === "History"} onClick={() => onTabChange("History")} />
      </div>

      <div className="px-4 py-6 space-y-1 mt-auto border-t border-zinc-800">
        <SidebarItem icon={Cpu} label="Hardware" active={activeTab === "Hardware"} onClick={() => onTabChange("Hardware")} />
        <SidebarItem icon={Settings} label="Settings" active={activeTab === "Settings"} onClick={() => onTabChange("Settings")} />
        <SidebarItem icon={HelpCircle} label="Support" active={activeTab === "Support"} onClick={() => onTabChange("Support")} />
      </div>
    </aside>
  );
}
