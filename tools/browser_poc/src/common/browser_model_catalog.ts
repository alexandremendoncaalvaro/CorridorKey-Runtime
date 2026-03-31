import type { BrowserModelDefinition } from "../core/image_types";

const DEFAULT_BROWSER_MODEL_BASE_URL =
  "https://pub-d7b1a78244e7440c9b8ca8a98a44e542.r2.dev";

function build_browser_model_url(filename: string): string {
  const base_url =
    import.meta.env.VITE_BROWSER_MODEL_BASE_URL ??
    DEFAULT_BROWSER_MODEL_BASE_URL;
  return `${base_url}/${filename}`;
}

export const DEFAULT_BROWSER_MODEL_ID = "corridorkey_int8_512";

export const BROWSER_MODEL_CATALOG: readonly BrowserModelDefinition[] = [
  {
    id: "corridorkey_int8_512",
    filename: "corridorkey_int8_512.onnx",
    label: "Draft",
    description:
      "INT8 512. Fastest bundled browser model and the default for quick local iteration.",
    resolution: 512,
    size_label: "76 MB",
    url: build_browser_model_url("corridorkey_int8_512.onnx"),
  },
  {
    id: "corridorkey_int8_1024",
    filename: "corridorkey_int8_1024.onnx",
    label: "High",
    description:
      "INT8 1024. Highest bundled browser model for desktop-class testing.",
    resolution: 1024,
    size_label: "97 MB",
    url: build_browser_model_url("corridorkey_int8_1024.onnx"),
  },
];

export function find_browser_model_definition(
  model_id: string,
): BrowserModelDefinition | null {
  return BROWSER_MODEL_CATALOG.find((model) => model.id === model_id) ?? null;
}
