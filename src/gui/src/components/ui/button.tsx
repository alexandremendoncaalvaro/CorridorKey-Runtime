import * as React from "react";
import { Slot } from "@radix-ui/react-slot";
import { cn } from "@/lib/utils";

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement> {
  asChild?: boolean;
  variant?: "primary" | "secondary" | "ghost" | "destructive";
  size?: "sm" | "md" | "lg" | "icon";
}

const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant = "primary", size = "md", asChild = false, ...props }, ref) => {
    const Comp = asChild ? Slot : "button";

    const variants = {
      primary: "bg-primary text-primary-foreground shadow-apple hover:bg-primary/90 active:scale-[0.98]",
      secondary: "bg-secondary text-secondary-foreground hover:bg-secondary/80 active:scale-[0.98]",
      ghost: "hover:bg-accent hover:text-accent-foreground",
      destructive: "bg-destructive text-destructive-foreground hover:bg-destructive/90 active:scale-[0.98]",
    };

    const sizes = {
      sm: "h-8 px-3 text-xs rounded-md",
      md: "h-10 px-4 py-2 rounded-lg",
      lg: "h-12 px-8 rounded-xl text-base",
      icon: "h-10 w-10 rounded-lg flex items-center justify-center",
    };

    return (
      <Comp
        className={cn(
          "inline-flex items-center justify-center whitespace-nowrap font-medium transition-all focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50",
          variants[variant],
          sizes[size],
          className
        )}
        ref={ref}
        {...props}
      />
    );
  }
);
Button.displayName = "Button";

export { Button };
