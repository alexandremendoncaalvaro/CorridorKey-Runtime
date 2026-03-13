import { cn } from "@/lib/utils";
import { 
  Home, 
  Activity, 
  Cpu, 
  Settings, 
  FolderOpen, 
  HelpCircle
} from "lucide-react";

interface SidebarItemProps {
  icon: React.ElementType;
  label: string;
  active?: boolean;
}

function SidebarItem({ icon: Icon, label, active }: SidebarItemProps) {
  return (
    <button
      className={cn(
        "flex items-center w-full gap-3 px-3 py-2 transition-all rounded-lg text-sm font-medium",
        active 
          ? "bg-primary/10 text-primary" 
          : "text-muted-foreground hover:bg-accent hover:text-accent-foreground"
      )}
    >
      <Icon className="w-4 h-4" />
      {label}
    </button>
  );
}

export function Sidebar() {
  return (
    <aside className="flex flex-col w-64 h-screen border-r bg-background/60 backdrop-blur-xl">
      <div className="flex items-center h-14 px-6 border-b">
        <span className="font-bold tracking-tight text-lg">CorridorKey</span>
      </div>
      
      <div className="flex-1 px-4 py-6 space-y-1">
        <SidebarItem icon={Home} label="Import" active />
        <SidebarItem icon={Activity} label="Process" />
        <SidebarItem icon={FolderOpen} label="History" />
      </div>

      <div className="px-4 py-6 space-y-1 mt-auto">
        <SidebarItem icon={Cpu} label="GPU Status" />
        <SidebarItem icon={Settings} label="Settings" />
        <SidebarItem icon={HelpCircle} label="Support" />
      </div>
    </aside>
  );
}
