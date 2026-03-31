import path from "node:path";
import { defineConfig } from "vite";

const repo_root = path.resolve(__dirname, "../..");

export default defineConfig({
  assetsInclude: ["**/*.onnx"],
  server: {
    host: "0.0.0.0",
    port: 4175,
    strictPort: true,
    fs: {
      allow: [repo_root],
    },
  },
  preview: {
    host: "0.0.0.0",
    port: 4175,
    strictPort: true,
  },
});
